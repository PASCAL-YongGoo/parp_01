/**
 * @file password_storage.c
 * @brief EEPROM-based Password Storage Implementation
 *
 * EEPROM Data Structure (48 bytes):
 *   Offset  Size  Description
 *   0x0000  4     Magic number (0x50415250 = "PARP")
 *   0x0004  1     Version (0x01)
 *   0x0005  1     Flags (bit 0: master_used)
 *   0x0006  2     Reserved
 *   0x0008  32    User password (null-terminated)
 *   0x0028  2     CRC-16
 *   0x002A  6     Reserved
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "password_storage.h"
#include "shell_login.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(password_storage, LOG_LEVEL_INF);

/* EEPROM layout constants */
#define EEPROM_MAGIC          0x50415250  /* "PARP" */
#define EEPROM_VERSION        0x01
#define EEPROM_BASE_ADDR      0x0000

/* Field offsets */
#define OFF_MAGIC             0
#define OFF_VERSION           4
#define OFF_FLAGS             5
#define OFF_FAILED_ATTEMPTS   6   /* Use first byte of Reserved1 */
#define OFF_RESERVED1         7   /* Remaining reserved byte */
#define OFF_PASSWORD          8
#define OFF_CRC               40  /* 0x28 */
#define OFF_RESERVED2         42
#define STORAGE_SIZE          48

/* Flag bits */
#define FLAG_MASTER_USED      0x01
#define FLAG_PASSWORD_CHANGED 0x02  /* User has changed initial password */

/* Local storage (RAM backup) */
static char current_password[32];
static bool eeprom_available;
static uint8_t current_flags;
static uint8_t failed_attempts;

/* EEPROM device */
static const struct device *eeprom_dev;

/**
 * @brief Load default password into RAM
 */
static void load_default_password(void)
{
	strncpy(current_password, SHELL_LOGIN_DEFAULT_PASSWORD,
		sizeof(current_password) - 1);
	current_password[sizeof(current_password) - 1] = '\0';
}

/**
 * @brief CRC-16-CCITT calculation
 */
static uint16_t crc16_ccitt(const uint8_t *data, size_t len)
{
	uint16_t crc = 0xFFFF;

	for (size_t i = 0; i < len; i++) {
		crc ^= (uint16_t)data[i] << 8;
		for (int j = 0; j < 8; j++) {
			if (crc & 0x8000) {
				crc = (crc << 1) ^ 0x1021;
			} else {
				crc <<= 1;
			}
		}
	}

	return crc;
}

/**
 * @brief Read storage structure from EEPROM
 */
static int eeprom_read_storage(uint8_t *buf, size_t len)
{
	if (!eeprom_dev) {
		return -ENODEV;
	}

	return eeprom_read(eeprom_dev, EEPROM_BASE_ADDR, buf, len);
}

/**
 * @brief Write storage structure to EEPROM
 */
static int eeprom_write_storage(const uint8_t *buf, size_t len)
{
	if (!eeprom_dev) {
		return -ENODEV;
	}

	return eeprom_write(eeprom_dev, EEPROM_BASE_ADDR, buf, len);
}

/**
 * @brief Write to EEPROM with read-back verification
 *
 * Writes data to EEPROM and reads back to verify integrity.
 * This is critical for production to ensure data persistence.
 *
 * @param buf Data buffer to write
 * @param len Length of data
 * @return 0 on success, -EIO on verification failure, other negative on error
 */
static int eeprom_write_verified(const uint8_t *buf, size_t len)
{
	uint8_t verify[STORAGE_SIZE];
	int ret;

	/* Write to EEPROM */
	ret = eeprom_write_storage(buf, len);
	if (ret < 0) {
		return ret;
	}

	/* Small delay for EEPROM internal write cycle (M24C64: 5ms max) */
	k_msleep(5);

	/* Read back for verification */
	ret = eeprom_read_storage(verify, len);
	if (ret < 0) {
		LOG_ERR("EEPROM verify read failed: %d", ret);
		return ret;
	}

	/* Compare written data with read-back data */
	if (memcmp(buf, verify, len) != 0) {
		LOG_ERR("EEPROM write verification failed");
		return -EIO;
	}

	return 0;
}

/**
 * @brief Initialize EEPROM with default values
 */
static int init_eeprom_defaults(void)
{
	uint8_t buf[STORAGE_SIZE] = {0};
	uint32_t magic = EEPROM_MAGIC;
	uint16_t crc;
	int ret;

	/* Set magic number (little-endian) */
	buf[OFF_MAGIC + 0] = (magic >> 0) & 0xFF;
	buf[OFF_MAGIC + 1] = (magic >> 8) & 0xFF;
	buf[OFF_MAGIC + 2] = (magic >> 16) & 0xFF;
	buf[OFF_MAGIC + 3] = (magic >> 24) & 0xFF;

	/* Set version */
	buf[OFF_VERSION] = EEPROM_VERSION;

	/* Set flags (none set initially) */
	buf[OFF_FLAGS] = 0;

	/* Copy default password */
	strncpy((char *)&buf[OFF_PASSWORD], SHELL_LOGIN_DEFAULT_PASSWORD,
		PASSWORD_MAX_LEN);
	buf[OFF_PASSWORD + PASSWORD_MAX_LEN] = '\0';

	/* Calculate CRC over magic through password (40 bytes) */
	crc = crc16_ccitt(buf, OFF_CRC);
	buf[OFF_CRC + 0] = (crc >> 0) & 0xFF;
	buf[OFF_CRC + 1] = (crc >> 8) & 0xFF;

	/* Write to EEPROM with verification */
	ret = eeprom_write_verified(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to write EEPROM defaults: %d", ret);
		return ret;
	}

	LOG_INF("EEPROM initialized with default password");
	return 0;
}

int password_storage_init(void)
{
	uint8_t buf[STORAGE_SIZE];
	uint32_t magic;
	uint16_t stored_crc, calc_crc;
	int ret;

	/* Get EEPROM device */
	eeprom_dev = DEVICE_DT_GET(DT_ALIAS(eeprom_0));
	if (!device_is_ready(eeprom_dev)) {
		LOG_WRN("EEPROM device not ready, using default password");
		eeprom_dev = NULL;
		eeprom_available = false;
		load_default_password();
		return 0;
	}

	/* Read current storage */
	ret = eeprom_read_storage(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to read EEPROM: %d", ret);
		eeprom_available = false;
		load_default_password();
		return 0;
	}

	/* Check magic number */
	magic = (uint32_t)buf[OFF_MAGIC + 0] |
		((uint32_t)buf[OFF_MAGIC + 1] << 8) |
		((uint32_t)buf[OFF_MAGIC + 2] << 16) |
		((uint32_t)buf[OFF_MAGIC + 3] << 24);

	if (magic != EEPROM_MAGIC) {
		LOG_INF("EEPROM not initialized (magic=0x%08x), initializing...",
			magic);
		ret = init_eeprom_defaults();
		if (ret < 0) {
			eeprom_available = false;
			load_default_password();
			return 0;
		}

		/* Re-read after initialization */
		ret = eeprom_read_storage(buf, STORAGE_SIZE);
		if (ret < 0) {
			LOG_ERR("Failed to re-read EEPROM: %d", ret);
			eeprom_available = false;
			load_default_password();
			return 0;
		}
	}

	/* Verify CRC */
	stored_crc = (uint16_t)buf[OFF_CRC + 0] |
		     ((uint16_t)buf[OFF_CRC + 1] << 8);
	calc_crc = crc16_ccitt(buf, OFF_CRC);

	if (stored_crc != calc_crc) {
		LOG_ERR("EEPROM CRC mismatch (stored=0x%04x, calc=0x%04x)",
			stored_crc, calc_crc);
		LOG_WRN("Using default password due to CRC error");
		eeprom_available = false;
		load_default_password();
		return 0;
	}

	/* Check version */
	if (buf[OFF_VERSION] != EEPROM_VERSION) {
		LOG_WRN("EEPROM version mismatch (got %d, expected %d)",
			buf[OFF_VERSION], EEPROM_VERSION);
		/* Could add migration logic here in future */
	}

	/* Load password */
	strncpy(current_password, (char *)&buf[OFF_PASSWORD],
		sizeof(current_password) - 1);
	current_password[sizeof(current_password) - 1] = '\0';

	/* Ensure null-termination (defense in depth) */
	if (strlen(current_password) == 0) {
		LOG_WRN("Empty password in EEPROM, using default");
		load_default_password();
	}

	/* Load flags */
	current_flags = buf[OFF_FLAGS];

	/* Load failed attempts count */
	failed_attempts = buf[OFF_FAILED_ATTEMPTS];

	eeprom_available = true;
	LOG_INF("Password loaded from EEPROM");

	if (current_flags & FLAG_MASTER_USED) {
		LOG_WRN("Master password was previously used");
	}

	return 0;
}

const char *password_storage_get(void)
{
	return current_password;
}

int password_storage_save(const char *new_password)
{
	uint8_t buf[STORAGE_SIZE];
	char backup[32];
	uint16_t crc;
	int ret;
	size_t len;

	if (!new_password) {
		return -EINVAL;
	}

	len = strlen(new_password);
	if (len == 0 || len > PASSWORD_MAX_LEN) {
		return -EINVAL;
	}

	/* Backup current password for rollback on failure */
	strncpy(backup, current_password, sizeof(backup) - 1);
	backup[sizeof(backup) - 1] = '\0';

	/* Update RAM */
	strncpy(current_password, new_password, sizeof(current_password) - 1);
	current_password[sizeof(current_password) - 1] = '\0';

	/* If EEPROM not available, just keep in RAM */
	if (!eeprom_available) {
		LOG_WRN("EEPROM not available, password stored in RAM only");
		return 0;
	}

	/* Read current storage to preserve other fields */
	ret = eeprom_read_storage(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to read EEPROM: %d", ret);
		/* Rollback RAM to previous value */
		strncpy(current_password, backup, sizeof(current_password) - 1);
		current_password[sizeof(current_password) - 1] = '\0';
		return ret;
	}

	/* Update password in buffer */
	memset(&buf[OFF_PASSWORD], 0, 32);
	strncpy((char *)&buf[OFF_PASSWORD], new_password, PASSWORD_MAX_LEN);

	/* Set password changed flag */
	buf[OFF_FLAGS] |= FLAG_PASSWORD_CHANGED;

	/* Recalculate CRC */
	crc = crc16_ccitt(buf, OFF_CRC);
	buf[OFF_CRC + 0] = (crc >> 0) & 0xFF;
	buf[OFF_CRC + 1] = (crc >> 8) & 0xFF;

	/* Write back to EEPROM with verification */
	ret = eeprom_write_verified(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to write EEPROM: %d", ret);
		/* Rollback RAM to previous value */
		strncpy(current_password, backup, sizeof(current_password) - 1);
		current_password[sizeof(current_password) - 1] = '\0';
		LOG_WRN("Password change rolled back");
		return ret;
	}

	/* Update RAM flags */
	current_flags |= FLAG_PASSWORD_CHANGED;

	LOG_INF("Password saved to EEPROM");
	return 0;
}

int password_storage_reset(void)
{
	int ret;

	/* Reset to defaults */
	load_default_password();
	current_flags = 0;

	if (!eeprom_available) {
		LOG_WRN("EEPROM not available, password reset in RAM only");
		return 0;
	}

	/* Reinitialize EEPROM with defaults */
	ret = init_eeprom_defaults();
	if (ret < 0) {
		LOG_ERR("Failed to reset EEPROM: %d", ret);
		return ret;
	}

	LOG_INF("Password reset to default");
	return 0;
}

bool password_storage_is_available(void)
{
	return eeprom_available;
}

void password_storage_set_master_used(void)
{
	uint8_t buf[STORAGE_SIZE];
	uint16_t crc;
	int ret;

	/* Already set - skip EEPROM write to reduce wear */
	if (current_flags & FLAG_MASTER_USED) {
		return;
	}

	/* Set flag in RAM */
	current_flags |= FLAG_MASTER_USED;

	if (!eeprom_available) {
		return;
	}

	/* Read current storage */
	ret = eeprom_read_storage(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to read EEPROM for master flag: %d", ret);
		return;
	}

	/* Update flags */
	buf[OFF_FLAGS] |= FLAG_MASTER_USED;

	/* Recalculate CRC */
	crc = crc16_ccitt(buf, OFF_CRC);
	buf[OFF_CRC + 0] = (crc >> 0) & 0xFF;
	buf[OFF_CRC + 1] = (crc >> 8) & 0xFF;

	/* Write back with verification */
	ret = eeprom_write_verified(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to write master flag: %d", ret);
		return;
	}

	LOG_WRN("Master password usage recorded");
}

bool password_storage_was_master_used(void)
{
	return (current_flags & FLAG_MASTER_USED) != 0;
}

uint8_t password_storage_get_failed_attempts(void)
{
	return failed_attempts;
}

void password_storage_inc_failed_attempts(void)
{
	uint8_t buf[STORAGE_SIZE];
	uint16_t crc;
	int ret;

	/* Increment in RAM (cap at 255) */
	if (failed_attempts < 255) {
		failed_attempts++;
	}

	if (!eeprom_available) {
		return;
	}

	/* Read current storage */
	ret = eeprom_read_storage(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to read EEPROM for failed attempts: %d", ret);
		return;
	}

	/* Update failed attempts */
	buf[OFF_FAILED_ATTEMPTS] = failed_attempts;

	/* Recalculate CRC */
	crc = crc16_ccitt(buf, OFF_CRC);
	buf[OFF_CRC + 0] = (crc >> 0) & 0xFF;
	buf[OFF_CRC + 1] = (crc >> 8) & 0xFF;

	/* Write back (without verification for speed - this is frequently updated) */
	ret = eeprom_write_storage(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to save failed attempts: %d", ret);
	}
}

void password_storage_clear_failed_attempts(void)
{
	uint8_t buf[STORAGE_SIZE];
	uint16_t crc;
	int ret;

	/* Already zero - skip EEPROM write */
	if (failed_attempts == 0) {
		return;
	}

	/* Clear in RAM */
	failed_attempts = 0;

	if (!eeprom_available) {
		return;
	}

	/* Read current storage */
	ret = eeprom_read_storage(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to read EEPROM for clear attempts: %d", ret);
		return;
	}

	/* Clear failed attempts */
	buf[OFF_FAILED_ATTEMPTS] = 0;

	/* Recalculate CRC */
	crc = crc16_ccitt(buf, OFF_CRC);
	buf[OFF_CRC + 0] = (crc >> 0) & 0xFF;
	buf[OFF_CRC + 1] = (crc >> 8) & 0xFF;

	/* Write back with verification (this is important for security) */
	ret = eeprom_write_verified(buf, STORAGE_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to clear failed attempts: %d", ret);
	}
}

bool password_storage_is_password_changed(void)
{
	return (current_flags & FLAG_PASSWORD_CHANGED) != 0;
}
