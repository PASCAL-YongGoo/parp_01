/*
 * SPDX-License-Identifier: Apache-2.0
 *
 * E310 RFID Reader Settings - Persistent Storage
 *
 * Stores E310 configuration in EEPROM for persistence across power cycles.
 * Uses M24C64 EEPROM at offset 0x0030 (after password_storage area).
 */

#ifndef E310_SETTINGS_H
#define E310_SETTINGS_H

#include <zephyr/kernel.h>
#include <stdint.h>
#include <stdbool.h>

struct shell;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * EEPROM Layout:
 * 0x0000-0x002F: Password Storage (48 bytes) - DO NOT TOUCH
 * 0x0030-0x005F: E310 Settings (48 bytes)
 * 0x0060-0x1FFF: Reserved for future use
 */
#define E310_SETTINGS_EEPROM_OFFSET  0x0030
#define E310_SETTINGS_SIZE           48

/* Magic number: "E310" in little-endian */
#define E310_SETTINGS_MAGIC          0x30313345
#define E310_SETTINGS_VERSION        0x01

/* Default values */
#define E310_DEFAULT_RF_POWER        20      /* 20 dBm (mid-range) */
#define E310_DEFAULT_ANTENNA         0x00    /* Default antenna config */
#define E310_DEFAULT_FREQ_REGION     4       /* Korea */
#define E310_DEFAULT_FREQ_START      0       /* First frequency index */
#define E310_DEFAULT_FREQ_END        19      /* Last frequency index */
#define E310_DEFAULT_INVENTORY_TIME  50      /* 5 seconds (50 x 100ms) */
#define E310_DEFAULT_READER_ADDR     0xFF    /* Broadcast address */
#define E310_DEFAULT_TYPING_SPEED    600     /* 600 CPM */

/* Valid ranges */
#define E310_RF_POWER_MIN            0
#define E310_RF_POWER_MAX            30
#define E310_FREQ_REGION_MIN         1
#define E310_FREQ_REGION_MAX         4
#define E310_FREQ_INDEX_MIN          0
#define E310_FREQ_INDEX_MAX          62
#define E310_INVENTORY_TIME_MIN      1
#define E310_INVENTORY_TIME_MAX      255
#define E310_TYPING_SPEED_MIN        100
#define E310_TYPING_SPEED_MAX        1500

/* Frequency region codes */
#define E310_FREQ_REGION_CHINA       1
#define E310_FREQ_REGION_US          2
#define E310_FREQ_REGION_EUROPE      3
#define E310_FREQ_REGION_KOREA       4

/**
 * @brief E310 persistent settings structure
 *
 * This structure is stored in EEPROM and loaded on boot.
 * Total size: 48 bytes (matches EEPROM page size for atomic write)
 */
typedef struct __packed {
    /* Header (6 bytes) */
    uint32_t magic;              /* 0x30313345 ("E310") */
    uint8_t  version;            /* Structure version (0x01) */
    uint8_t  flags;              /* Bit 0: settings_changed */

    /* RF Settings (2 bytes) */
    uint8_t  rf_power;           /* 0-30 dBm */
    uint8_t  antenna_config;     /* Antenna selection (0x00-0xFF) */

    /* Frequency Settings (3 bytes) */
    uint8_t  freq_region;        /* 1=CN, 2=US, 3=EU, 4=KR */
    uint8_t  freq_start;         /* Start frequency index (0-62) */
    uint8_t  freq_end;           /* End frequency index (0-62) */

    /* Inventory Settings (2 bytes) */
    uint8_t  inventory_time;     /* Scan time (1-255 x 100ms) */
    uint8_t  reader_addr;        /* Reader address (0x00-0xFF) */

    /* HID Settings (2 bytes) */
    uint16_t typing_speed;       /* Typing speed (100-1500 CPM) */

    /* Reserved for future use (29 bytes) */
    uint8_t  reserved[29];

    /* Integrity check (2 bytes) */
    uint16_t crc16;              /* CRC-16-CCITT over bytes 0-45 */
} e310_settings_t;

/* Flags bit definitions */
#define E310_FLAG_SETTINGS_CHANGED   (1 << 0)

/**
 * @brief Initialize E310 settings module
 *
 * Loads settings from EEPROM. If EEPROM is unavailable or data is
 * corrupted, initializes with default values.
 *
 * @return 0 on success, negative error code on failure
 */
int e310_settings_init(void);

/**
 * @brief Get current settings (read-only)
 *
 * @return Pointer to current settings structure (do not modify directly)
 */
const e310_settings_t *e310_settings_get(void);

/**
 * @brief Save current settings to EEPROM
 *
 * @return 0 on success, negative error code on failure
 */
int e310_settings_save(void);

/**
 * @brief Reset settings to factory defaults
 *
 * Resets all settings to default values and saves to EEPROM.
 *
 * @return 0 on success, negative error code on failure
 */
int e310_settings_reset(void);

/**
 * @brief Check if EEPROM is available for settings storage
 *
 * @return true if EEPROM is available, false otherwise
 */
bool e310_settings_is_available(void);

/*
 * Individual setting accessors with automatic save
 */

/**
 * @brief Set RF power and save to EEPROM
 *
 * @param power RF power in dBm (0-30)
 * @return 0 on success, -EINVAL if out of range, negative error on save failure
 */
int e310_settings_set_rf_power(uint8_t power);

/**
 * @brief Get current RF power setting
 *
 * @return RF power in dBm (0-30)
 */
uint8_t e310_settings_get_rf_power(void);

/**
 * @brief Set antenna configuration and save to EEPROM
 *
 * @param config Antenna configuration byte
 * @return 0 on success, negative error on save failure
 */
int e310_settings_set_antenna(uint8_t config);

/**
 * @brief Get current antenna configuration
 *
 * @return Antenna configuration byte
 */
uint8_t e310_settings_get_antenna(void);

/**
 * @brief Set frequency region and range, save to EEPROM
 *
 * @param region Frequency region (1-4)
 * @param start Start frequency index (0-62)
 * @param end End frequency index (0-62)
 * @return 0 on success, -EINVAL if out of range, negative error on save failure
 */
int e310_settings_set_frequency(uint8_t region, uint8_t start, uint8_t end);

/**
 * @brief Get current frequency settings
 *
 * @param region Pointer to store region (can be NULL)
 * @param start Pointer to store start index (can be NULL)
 * @param end Pointer to store end index (can be NULL)
 */
void e310_settings_get_frequency(uint8_t *region, uint8_t *start, uint8_t *end);

/**
 * @brief Set inventory time and save to EEPROM
 *
 * @param time Inventory time in 100ms units (1-255)
 * @return 0 on success, -EINVAL if out of range, negative error on save failure
 */
int e310_settings_set_inventory_time(uint8_t time);

/**
 * @brief Get current inventory time setting
 *
 * @return Inventory time in 100ms units (1-255)
 */
uint8_t e310_settings_get_inventory_time(void);

/**
 * @brief Set reader address and save to EEPROM
 *
 * @param addr Reader address (0x00-0xFF)
 * @return 0 on success, negative error on save failure
 */
int e310_settings_set_reader_addr(uint8_t addr);

/**
 * @brief Get current reader address setting
 *
 * @return Reader address (0x00-0xFF)
 */
uint8_t e310_settings_get_reader_addr(void);

/**
 * @brief Set HID typing speed and save to EEPROM
 *
 * @param cpm Typing speed in characters per minute (100-1500)
 * @return 0 on success, -EINVAL if out of range, negative error on save failure
 */
int e310_settings_set_typing_speed(uint16_t cpm);

/**
 * @brief Get current HID typing speed setting
 *
 * @return Typing speed in characters per minute (100-1500)
 */
uint16_t e310_settings_get_typing_speed(void);

/**
 * @brief Print current settings to shell
 *
 * @param sh Shell instance (can be NULL for LOG output)
 */
void e310_settings_print(const struct shell *sh);

#ifdef __cplusplus
}
#endif

#endif /* E310_SETTINGS_H */
