/**
 * @file password_storage.h
 * @brief EEPROM-based Password Storage
 *
 * Provides persistent password storage using M24C64 EEPROM.
 * Falls back to default password if EEPROM is unavailable.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef PASSWORD_STORAGE_H
#define PASSWORD_STORAGE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/** Maximum password length (excluding null terminator) */
#define PASSWORD_MAX_LEN  31

/**
 * @brief Initialize password storage
 *
 * Loads password from EEPROM. If EEPROM is not available or
 * not initialized, uses default password.
 *
 * @return 0 on success, negative errno on error
 */
int password_storage_init(void);

/**
 * @brief Get current password
 *
 * @return Pointer to current password string
 */
const char *password_storage_get(void);

/**
 * @brief Save new password to EEPROM
 *
 * @param new_password New password to save (max 31 chars)
 * @return 0 on success, negative errno on error
 */
int password_storage_save(const char *new_password);

/**
 * @brief Reset password to default
 *
 * Clears EEPROM and resets to default password.
 *
 * @return 0 on success, negative errno on error
 */
int password_storage_reset(void);

/**
 * @brief Check if EEPROM storage is available
 *
 * @return true if EEPROM is working, false otherwise
 */
bool password_storage_is_available(void);

/**
 * @brief Set master password used flag
 *
 * Called when master password is used for login.
 * Sets audit flag in EEPROM.
 */
void password_storage_set_master_used(void);

/**
 * @brief Check if master password was ever used
 *
 * @return true if master was used, false otherwise
 */
bool password_storage_was_master_used(void);

/**
 * @brief Get failed login attempt count
 *
 * Returns the count of failed login attempts stored in EEPROM.
 * This persists across reboots to prevent lockout bypass.
 *
 * @return Number of failed attempts (0-255)
 */
uint8_t password_storage_get_failed_attempts(void);

/**
 * @brief Increment failed login attempt count
 *
 * Increments the failed attempt counter and saves to EEPROM.
 * Call this on each failed login attempt.
 */
void password_storage_inc_failed_attempts(void);

/**
 * @brief Clear failed login attempt count
 *
 * Resets the failed attempt counter to zero (after successful login).
 */
void password_storage_clear_failed_attempts(void);

/**
 * @brief Check if initial password was changed
 *
 * Returns true if the user has changed the default password.
 * This is used to enforce password change on first boot.
 *
 * @return true if password was changed from default, false otherwise
 */
bool password_storage_is_password_changed(void);

#endif /* PASSWORD_STORAGE_H */
