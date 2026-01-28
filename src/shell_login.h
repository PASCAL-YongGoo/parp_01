/**
 * @file shell_login.h
 * @brief Shell Login Security
 *
 * Provides password authentication for shell access.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef SHELL_LOGIN_H
#define SHELL_LOGIN_H

#include <stdbool.h>

/**
 * Default password obfuscation (XOR with key to prevent binary string search)
 *
 * This is NOT cryptographic security - it only prevents casual discovery
 * via `strings` command on the binary.
 *
 * NOTE: Default password is documented separately in secure storage.
 * DO NOT add the plain text password in source code.
 */
#define SHELL_LOGIN_DEFAULT_XOR_KEY      0x5A
#define SHELL_LOGIN_DEFAULT_OBFUSCATED { \
	0x2a, 0x3b, 0x28, 0x2a, 0x68, 0x6a, 0x68, 0x6c, 0x00 \
}

/* For backward compatibility - use shell_login_get_default_password() */
#define SHELL_LOGIN_DEFAULT_PASSWORD  shell_login_get_default_password()

/**
 * Master password obfuscation (XOR with key to prevent binary string search)
 *
 * This is NOT cryptographic security - it only prevents casual discovery
 * via `strings` command on the binary.
 *
 * NOTE: Master password is documented separately in secure storage.
 * DO NOT add the plain text password in source code.
 */
#define SHELL_LOGIN_MASTER_XOR_KEY    0x5A
#define SHELL_LOGIN_MASTER_OBFUSCATED { \
	0x2a, 0x3b, 0x29, 0x39, 0x3b, 0x36, 0x6b, 0x7b, 0x00 \
}

/** Maximum login attempts before lockout */
#define SHELL_LOGIN_MAX_ATTEMPTS      3

/** Lockout duration in seconds after max attempts */
#define SHELL_LOGIN_LOCKOUT_SEC       30

/** Auto-logout timeout in seconds (0 = disabled) */
#define SHELL_LOGIN_TIMEOUT_SEC       300

/**
 * @brief Get de-obfuscated default password
 *
 * Returns the default password after de-obfuscating from stored bytes.
 * Thread-safe, returns pointer to static buffer.
 *
 * @return Pointer to null-terminated default password string
 */
const char *shell_login_get_default_password(void);

/**
 * @brief Initialize shell login system
 *
 * @return 0 on success, negative errno on error
 */
int shell_login_init(void);

/**
 * @brief Check if user is authenticated
 *
 * @return true if logged in, false otherwise
 */
bool shell_login_is_authenticated(void);

/**
 * @brief Check if current session is a master session
 *
 * @return true if logged in with master password, false otherwise
 */
bool shell_login_is_master_session(void);

/**
 * @brief Update last activity time
 *
 * Call this when user interacts with shell to reset timeout.
 */
void shell_login_activity(void);

/**
 * @brief Check for auto-logout timeout
 *
 * Call this periodically from main loop.
 */
void shell_login_check_timeout(void);

/**
 * @brief Force logout
 *
 * Used when inventory is started via switch.
 */
void shell_login_force_logout(void);

#endif /* SHELL_LOGIN_H */
