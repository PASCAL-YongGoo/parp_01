/**
 * @file shell_login.c
 * @brief Shell Login Implementation
 *
 * Provides login/logout commands with:
 * - Password authentication (EEPROM-backed)
 * - Master password for recovery
 * - Failed attempt lockout
 * - Auto-logout on inactivity
 * - Password change capability
 * - Password reset (master session only)
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "shell_login.h"
#include "password_storage.h"
#include <zephyr/kernel.h>
#include <zephyr/shell/shell.h>
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(shell_login, LOG_LEVEL_INF);

/**
 * @brief Constant-time string comparison (timing attack resistant)
 *
 * Compares two strings in constant time to prevent timing attacks.
 * The comparison time depends only on the length of the input string,
 * not on when/where the strings differ.
 *
 * @param input User-provided input string
 * @param secret Secret string to compare against
 * @return true if strings match, false otherwise
 */
static bool secure_compare(const char *input, const char *secret)
{
	size_t input_len = strlen(input);
	size_t secret_len = strlen(secret);

	/* Use volatile to prevent compiler optimization */
	volatile uint8_t result = (input_len != secret_len) ? 1 : 0;

	/* Compare all characters of the shorter string */
	size_t cmp_len = (input_len < secret_len) ? input_len : secret_len;
	for (size_t i = 0; i < cmp_len; i++) {
		result |= ((uint8_t)input[i] ^ (uint8_t)secret[i]);
	}

	return (result == 0);
}

/**
 * @brief Verify master password (de-obfuscate and compare)
 *
 * De-obfuscates the stored master password and performs constant-time
 * comparison with user input.
 *
 * @param input User-provided password
 * @return true if master password matches, false otherwise
 */
static bool verify_master_password(const char *input)
{
	static const uint8_t obfuscated[] = SHELL_LOGIN_MASTER_OBFUSCATED;
	char master[sizeof(obfuscated)];

	/* De-obfuscate master password */
	for (size_t i = 0; i < sizeof(obfuscated) - 1; i++) {
		master[i] = (char)(obfuscated[i] ^ SHELL_LOGIN_MASTER_XOR_KEY);
	}
	master[sizeof(obfuscated) - 1] = '\0';

	/* Constant-time comparison */
	return secure_compare(input, master);
}

/**
 * @brief Get de-obfuscated default password
 *
 * Returns the default password after de-obfuscating from stored bytes.
 * Uses one-time initialization pattern for efficiency.
 *
 * @return Pointer to null-terminated default password string
 */
const char *shell_login_get_default_password(void)
{
	static char default_pw[16];
	static bool initialized = false;

	if (!initialized) {
		static const uint8_t obfuscated[] = SHELL_LOGIN_DEFAULT_OBFUSCATED;
		for (size_t i = 0; i < sizeof(obfuscated) - 1; i++) {
			default_pw[i] = (char)(obfuscated[i] ^ SHELL_LOGIN_DEFAULT_XOR_KEY);
		}
		default_pw[sizeof(obfuscated) - 1] = '\0';
		initialized = true;
	}
	return default_pw;
}

/**
 * @brief Validate password complexity
 *
 * Requirements:
 * - Length: 4-31 characters
 * - At least one letter (a-z, A-Z)
 * - At least one digit (0-9)
 *
 * @param password Password string to validate
 * @param len Length of password
 * @return true if valid, false otherwise
 */
static bool validate_password_complexity(const char *password, size_t len)
{
	bool has_letter = false;
	bool has_digit = false;

	if (len < 4 || len > PASSWORD_MAX_LEN) {
		return false;
	}

	for (size_t i = 0; i < len; i++) {
		char c = password[i];
		if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
			has_letter = true;
		} else if (c >= '0' && c <= '9') {
			has_digit = true;
		}
	}

	return has_letter && has_digit;
}

/* Login state */
static bool authenticated;
static bool is_master_session;

/* Security state */
static uint8_t failed_attempts;
static int64_t lockout_until;
static int64_t last_activity;

/* Shell backend pointer for logout operations */
static const struct shell *login_shell;

/**
 * @brief Perform logout operations
 */
static void do_logout(const struct shell *sh)
{
	authenticated = false;
	is_master_session = false;
	/* Note: Don't clear failed_attempts on logout - only on successful login */

	if (sh) {
		shell_obscure_set(sh, true);
		shell_set_root_cmd("login");
	}
}

/**
 * @brief Login command handler
 */
static int cmd_login(const struct shell *sh, size_t argc, char **argv)
{
	int64_t now = k_uptime_get();
	const char *current_password;

	/* Check if locked out */
	if (lockout_until > now) {
		int remaining = (int)((lockout_until - now) / 1000);
		shell_error(sh, "Locked. Try again in %d seconds.", remaining);
		return -EACCES;
	}

	/* Check arguments */
	if (argc != 2) {
		shell_print(sh, "Usage: login <password>");
		return -EINVAL;
	}

	/* Get current password from storage */
	current_password = password_storage_get();

	/* Check master password first (obfuscated + constant-time) */
	if (verify_master_password(argv[1])) {
		authenticated = true;
		is_master_session = true;
		last_activity = now;
		login_shell = sh;

		/* Clear failed attempts on successful login */
		password_storage_clear_failed_attempts();
		failed_attempts = 0;

		/* Record master usage in EEPROM */
		password_storage_set_master_used();

		shell_obscure_set(sh, false);
		shell_set_root_cmd(NULL);

		shell_warn(sh, "*** MASTER PASSWORD LOGIN ***");
		shell_print(sh, "Logged in with master password.");
		shell_print(sh, "Use 'resetpasswd' to reset user password.");
		LOG_WRN("Shell login with MASTER password");
		return 0;
	}

	/* Verify user password (constant-time comparison) */
	if (secure_compare(argv[1], current_password)) {
		authenticated = true;
		is_master_session = false;
		last_activity = now;
		login_shell = sh;

		/* Clear failed attempts on successful login */
		password_storage_clear_failed_attempts();
		failed_attempts = 0;

		shell_obscure_set(sh, false);
		shell_set_root_cmd(NULL);

		shell_print(sh, "Login successful. Type 'help' for commands.");
		LOG_INF("Shell login successful");

		/* Warn if default password is still in use */
		if (!password_storage_is_password_changed()) {
			shell_warn(sh, "");
			shell_warn(sh, "*** SECURITY WARNING ***");
			shell_warn(sh, "Default password in use. Change immediately!");
			shell_print(sh, "Use: passwd <current> <new>");
			shell_warn(sh, "");
		}
	} else {
		/* Increment and persist failed attempts */
		password_storage_inc_failed_attempts();
		failed_attempts = password_storage_get_failed_attempts();

		LOG_WRN("Shell login failed (attempt %d/%d)",
			failed_attempts, SHELL_LOGIN_MAX_ATTEMPTS);

		if (failed_attempts >= SHELL_LOGIN_MAX_ATTEMPTS) {
			lockout_until = now + (SHELL_LOGIN_LOCKOUT_SEC * 1000);
			shell_error(sh, "Too many failed attempts. "
				    "Locked for %d seconds.",
				    SHELL_LOGIN_LOCKOUT_SEC);
			/* Don't clear on lockout - keep in EEPROM */
		} else {
			shell_error(sh, "Invalid password (%d/%d)",
				    failed_attempts, SHELL_LOGIN_MAX_ATTEMPTS);
		}
	}

	return 0;
}

/**
 * @brief Logout command handler
 */
static int cmd_logout(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!authenticated) {
		shell_print(sh, "Not logged in.");
		return 0;
	}

	do_logout(sh);
	shell_print(sh, "Logged out.");
	LOG_INF("Shell logout");

	return 0;
}

/**
 * @brief Password change command handler
 */
static int cmd_passwd(const struct shell *sh, size_t argc, char **argv)
{
	const char *current_password;
	int ret;

	/* Must be authenticated */
	if (!authenticated) {
		shell_error(sh, "Login required");
		return -EACCES;
	}

	/* Check arguments */
	if (argc != 3) {
		shell_print(sh, "Usage: passwd <old_password> <new_password>");
		return -EINVAL;
	}

	/* Get current password */
	current_password = password_storage_get();

	/* Verify current password (constant-time comparison) */
	if (!secure_compare(argv[1], current_password)) {
		shell_error(sh, "Current password incorrect");
		return -EACCES;
	}

	/* Validate new password complexity */
	size_t new_len = strlen(argv[2]);
	if (!validate_password_complexity(argv[2], new_len)) {
		shell_error(sh, "Password must be 4-31 characters");
		shell_error(sh, "with at least one letter and one digit");
		return -EINVAL;
	}

	/* Save new password to EEPROM */
	ret = password_storage_save(argv[2]);
	if (ret < 0) {
		shell_error(sh, "Failed to save password: %d", ret);
		return ret;
	}

	last_activity = k_uptime_get();

	if (password_storage_is_available()) {
		shell_print(sh, "Password changed and saved to EEPROM");
	} else {
		shell_warn(sh, "Password changed (RAM only - EEPROM unavailable)");
	}
	LOG_INF("Password changed");

	return 0;
}

/**
 * @brief Reset password command handler (master session only)
 */
static int cmd_resetpasswd(const struct shell *sh, size_t argc, char **argv)
{
	int ret;

	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	/* Must be authenticated */
	if (!authenticated) {
		shell_error(sh, "Login required");
		return -EACCES;
	}

	/* Must be master session */
	if (!is_master_session) {
		shell_error(sh, "Master password login required");
		shell_print(sh, "Logout and login with master password first.");
		return -EPERM;
	}

	/* Reset password to default */
	ret = password_storage_reset();
	if (ret < 0) {
		shell_error(sh, "Failed to reset password: %d", ret);
		return ret;
	}

	shell_print(sh, "Password reset to default.");
	shell_print(sh, "Refer to device documentation for credentials.");
	LOG_WRN("Password reset to default by master session");

	return 0;
}

/* Register shell commands */
SHELL_CMD_REGISTER(login, NULL, "Login with password", cmd_login);
SHELL_CMD_REGISTER(logout, NULL, "Logout from shell", cmd_logout);
SHELL_CMD_REGISTER(passwd, NULL, "Change password: passwd <old> <new>",
		   cmd_passwd);
SHELL_CMD_REGISTER(resetpasswd, NULL,
		   "Reset password to default (master session only)",
		   cmd_resetpasswd);

/* Public API */

int shell_login_init(void)
{
	authenticated = false;
	is_master_session = false;
	lockout_until = 0;
	last_activity = 0;
	login_shell = NULL;

	/* Load failed attempts from EEPROM (persists across reboots) */
	failed_attempts = password_storage_get_failed_attempts();
	if (failed_attempts > 0) {
		LOG_WRN("Restored %d failed login attempts from EEPROM",
			failed_attempts);
		if (failed_attempts >= SHELL_LOGIN_MAX_ATTEMPTS) {
			/* Re-apply lockout if max attempts reached before reboot */
			lockout_until = k_uptime_get() + (SHELL_LOGIN_LOCKOUT_SEC * 1000);
			LOG_WRN("Lockout re-applied for %d seconds",
				SHELL_LOGIN_LOCKOUT_SEC);
		}
	}

	LOG_INF("Shell login initialized");

	if (password_storage_is_available()) {
		LOG_INF("Password storage: EEPROM");
	} else {
		LOG_WRN("Password storage: RAM only (EEPROM unavailable)");
	}

	if (password_storage_was_master_used()) {
		LOG_WRN("Note: Master password was previously used");
	}

	return 0;
}

bool shell_login_is_authenticated(void)
{
	return authenticated;
}

bool shell_login_is_master_session(void)
{
	return is_master_session;
}

void shell_login_activity(void)
{
	if (authenticated) {
		last_activity = k_uptime_get();
	}
}

void shell_login_check_timeout(void)
{
#if SHELL_LOGIN_TIMEOUT_SEC > 0
	if (!authenticated) {
		return;
	}

	int64_t now = k_uptime_get();
	int64_t idle_ms = now - last_activity;

	if (idle_ms > (SHELL_LOGIN_TIMEOUT_SEC * 1000)) {
		LOG_INF("Auto logout due to inactivity (%d seconds)",
			SHELL_LOGIN_TIMEOUT_SEC);
		do_logout(login_shell);
	}
#endif
}

void shell_login_force_logout(void)
{
	if (authenticated) {
		LOG_INF("Force logout (inventory started)");
		do_logout(login_shell);
	}
}
