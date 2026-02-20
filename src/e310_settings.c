/*
 * SPDX-License-Identifier: Apache-2.0
 * E310 RFID Reader Settings - EEPROM Persistent Storage
 */

#include "e310_settings.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/eeprom.h>
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <string.h>

LOG_MODULE_REGISTER(e310_settings, LOG_LEVEL_INF);

#define CRC_DATA_SIZE  (E310_SETTINGS_SIZE - 2)

static e310_settings_t settings;
static bool eeprom_available;
static const struct device *eeprom_dev;

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

static void load_defaults(void)
{
	memset(&settings, 0, sizeof(settings));
	settings.magic = E310_SETTINGS_MAGIC;
	settings.version = E310_SETTINGS_VERSION;
	settings.flags = 0;
	settings.rf_power = E310_DEFAULT_RF_POWER;
	settings.antenna_config = E310_DEFAULT_ANTENNA;
	settings.freq_region = E310_DEFAULT_FREQ_REGION;
	settings.freq_start = E310_DEFAULT_FREQ_START;
	settings.freq_end = E310_DEFAULT_FREQ_END;
	settings.inventory_time = E310_DEFAULT_INVENTORY_TIME;
	settings.reader_addr = E310_DEFAULT_READER_ADDR;
	settings.typing_speed = E310_DEFAULT_TYPING_SPEED;
}

static int eeprom_read_settings(void)
{
	if (!eeprom_dev) {
		return -ENODEV;
	}

	return eeprom_read(eeprom_dev, E310_SETTINGS_EEPROM_OFFSET,
			   &settings, sizeof(settings));
}

static int eeprom_write_settings(void)
{
	if (!eeprom_dev) {
		return -ENODEV;
	}

	return eeprom_write(eeprom_dev, E310_SETTINGS_EEPROM_OFFSET,
			    &settings, sizeof(settings));
}

static int eeprom_write_verified(void)
{
	e310_settings_t verify;
	int ret;

	ret = eeprom_write_settings();
	if (ret < 0) {
		return ret;
	}

	k_msleep(5);

	ret = eeprom_read(eeprom_dev, E310_SETTINGS_EEPROM_OFFSET,
			  &verify, sizeof(verify));
	if (ret < 0) {
		LOG_ERR("Settings verify read failed: %d", ret);
		return ret;
	}

	if (memcmp(&settings, &verify, sizeof(settings)) != 0) {
		LOG_ERR("Settings write verification failed");
		return -EIO;
	}

	return 0;
}

static void update_crc(void)
{
	settings.crc16 = crc16_ccitt((const uint8_t *)&settings, CRC_DATA_SIZE);
}

static bool verify_crc(void)
{
	uint16_t calc_crc = crc16_ccitt((const uint8_t *)&settings, CRC_DATA_SIZE);
	return settings.crc16 == calc_crc;
}

static int init_eeprom_defaults(void)
{
	int ret;

	load_defaults();
	update_crc();

	ret = eeprom_write_verified();
	if (ret < 0) {
		LOG_ERR("Failed to write settings defaults: %d", ret);
		return ret;
	}

	LOG_INF("E310 settings initialized with defaults");
	return 0;
}

int e310_settings_init(void)
{
	int ret;

	eeprom_dev = DEVICE_DT_GET(DT_ALIAS(eeprom_0));
	if (!device_is_ready(eeprom_dev)) {
		LOG_WRN("EEPROM not ready, using default settings");
		eeprom_dev = NULL;
		eeprom_available = false;
		load_defaults();
		return 0;
	}

	ret = eeprom_read_settings();
	if (ret < 0) {
		LOG_ERR("Failed to read settings: %d", ret);
		eeprom_available = false;
		load_defaults();
		return 0;
	}

	if (settings.magic != E310_SETTINGS_MAGIC) {
		LOG_INF("E310 settings not initialized (magic=0x%08x)", settings.magic);
		ret = init_eeprom_defaults();
		if (ret < 0) {
			eeprom_available = false;
			load_defaults();
			return 0;
		}
	}

	if (!verify_crc()) {
		LOG_ERR("E310 settings CRC mismatch");
		LOG_WRN("Using default settings due to CRC error");
		ret = init_eeprom_defaults();
		if (ret < 0) {
			eeprom_available = false;
			load_defaults();
			return 0;
		}
	}

	if (settings.version != E310_SETTINGS_VERSION) {
		LOG_WRN("Settings version mismatch (got %d, expected %d), resetting",
			settings.version, E310_SETTINGS_VERSION);
		ret = init_eeprom_defaults();
		if (ret < 0) {
			eeprom_available = false;
			load_defaults();
			return 0;
		}
	}

	eeprom_available = true;
	LOG_INF("E310 settings loaded: RF=%d dBm, Ant=0x%02x, Freq=%d/%d-%d, InvTime=%d, Speed=%d",
		settings.rf_power, settings.antenna_config,
		settings.freq_region, settings.freq_start, settings.freq_end,
		settings.inventory_time, settings.typing_speed);

	return 0;
}

const e310_settings_t *e310_settings_get(void)
{
	return &settings;
}

int e310_settings_save(void)
{
	int ret;

	if (!eeprom_available) {
		LOG_WRN("EEPROM not available, settings in RAM only");
		return 0;
	}

	settings.flags |= E310_FLAG_SETTINGS_CHANGED;
	update_crc();

	ret = eeprom_write_verified();
	if (ret < 0) {
		LOG_ERR("Failed to save settings: %d", ret);
		return ret;
	}

	LOG_DBG("Settings saved to EEPROM");
	return 0;
}

int e310_settings_reset(void)
{
	int ret;

	load_defaults();

	if (!eeprom_available) {
		LOG_WRN("EEPROM not available, settings reset in RAM only");
		return 0;
	}

	update_crc();
	ret = eeprom_write_verified();
	if (ret < 0) {
		LOG_ERR("Failed to reset settings: %d", ret);
		return ret;
	}

	LOG_INF("E310 settings reset to defaults");
	return 0;
}

bool e310_settings_is_available(void)
{
	return eeprom_available;
}

int e310_settings_set_rf_power(uint8_t power)
{
	if (power > E310_RF_POWER_MAX) {
		return -EINVAL;
	}

	settings.rf_power = power;
	return e310_settings_save();
}

uint8_t e310_settings_get_rf_power(void)
{
	return settings.rf_power;
}

int e310_settings_set_antenna(uint8_t config)
{
	settings.antenna_config = config;
	return e310_settings_save();
}

uint8_t e310_settings_get_antenna(void)
{
	return settings.antenna_config;
}

int e310_settings_set_frequency(uint8_t region, uint8_t start, uint8_t end)
{
	if (region < E310_FREQ_REGION_MIN || region > E310_FREQ_REGION_MAX) {
		return -EINVAL;
	}
	if (start > E310_FREQ_INDEX_MAX || end > E310_FREQ_INDEX_MAX) {
		return -EINVAL;
	}

	settings.freq_region = region;
	settings.freq_start = start;
	settings.freq_end = end;
	return e310_settings_save();
}

void e310_settings_get_frequency(uint8_t *region, uint8_t *start, uint8_t *end)
{
	if (region) {
		*region = settings.freq_region;
	}
	if (start) {
		*start = settings.freq_start;
	}
	if (end) {
		*end = settings.freq_end;
	}
}

int e310_settings_set_inventory_time(uint8_t time)
{
	if (time < E310_INVENTORY_TIME_MIN) {
		return -EINVAL;
	}

	settings.inventory_time = time;
	return e310_settings_save();
}

uint8_t e310_settings_get_inventory_time(void)
{
	return settings.inventory_time;
}

int e310_settings_set_reader_addr(uint8_t addr)
{
	settings.reader_addr = addr;
	return e310_settings_save();
}

uint8_t e310_settings_get_reader_addr(void)
{
	return settings.reader_addr;
}

int e310_settings_set_typing_speed(uint16_t cpm)
{
	if (cpm < E310_TYPING_SPEED_MIN || cpm > E310_TYPING_SPEED_MAX) {
		return -EINVAL;
	}

	settings.typing_speed = cpm;
	return e310_settings_save();
}

uint16_t e310_settings_get_typing_speed(void)
{
	return settings.typing_speed;
}

void e310_settings_print(const struct shell *sh)
{
	const char *region_str;

	switch (settings.freq_region) {
	case E310_FREQ_REGION_CHINA:
		region_str = "China";
		break;
	case E310_FREQ_REGION_US:
		region_str = "US";
		break;
	case E310_FREQ_REGION_EUROPE:
		region_str = "Europe";
		break;
	case E310_FREQ_REGION_KOREA:
		region_str = "Korea";
		break;
	default:
		region_str = "Unknown";
		break;
	}

	if (sh) {
		shell_print(sh, "=== E310 Settings ===");
		shell_print(sh, "  EEPROM:      %s", eeprom_available ? "Available" : "Not available");
		shell_print(sh, "  RF Power:    %d dBm", settings.rf_power);
		shell_print(sh, "  Antenna:     0x%02x", settings.antenna_config);
		shell_print(sh, "  Freq Region: %s (%d)", region_str, settings.freq_region);
		shell_print(sh, "  Freq Range:  %d - %d", settings.freq_start, settings.freq_end);
		shell_print(sh, "  Inv Time:    %d (%.1f sec)", settings.inventory_time,
			    (double)(settings.inventory_time * 0.1f));
		shell_print(sh, "  Reader Addr: 0x%02x", settings.reader_addr);
		shell_print(sh, "  Typing Speed: %d CPM", settings.typing_speed);
		shell_print(sh, "  Changed:     %s",
			    (settings.flags & E310_FLAG_SETTINGS_CHANGED) ? "Yes" : "No");
	} else {
		LOG_INF("E310 Settings: RF=%d dBm, Ant=0x%02x, Freq=%s/%d-%d, Inv=%d, Speed=%d",
			settings.rf_power, settings.antenna_config,
			region_str, settings.freq_start, settings.freq_end,
			settings.inventory_time, settings.typing_speed);
	}
}
