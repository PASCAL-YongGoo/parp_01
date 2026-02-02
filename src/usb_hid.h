/**
 * @file usb_hid.h
 * @brief USB HID Keyboard Interface for EPC Output
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef USB_HID_H
#define USB_HID_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/* ========================================================================
 * Typing Speed Configuration
 * ======================================================================== */

/** Minimum typing speed in CPM (Characters Per Minute) */
#define HID_TYPING_SPEED_MIN     100

/** Maximum typing speed in CPM */
#define HID_TYPING_SPEED_MAX     1500

/** Default typing speed in CPM (~600 CPM = 10 chars/sec) */
#define HID_TYPING_SPEED_DEFAULT 600

/** Typing speed step size */
#define HID_TYPING_SPEED_STEP    100

/**
 * @brief Initialize USB HID Keyboard
 *
 * Initializes the USB HID keyboard device and prepares it for sending
 * EPC data as keyboard input.
 *
 * @return 0 on success, negative error code on failure
 */
int parp_usb_hid_init(void);

/**
 * @brief Send EPC string as keyboard input (uppercase only)
 *
 * Converts the EPC string to uppercase and sends it as HID keyboard
 * input followed by an Enter key. All characters are converted to
 * uppercase automatically to ensure consistency.
 *
 * @param epc Pointer to EPC string (hex digits, spaces allowed)
 * @param len Length of EPC string
 * @return 0 on success, negative error code on failure
 *
 * @note Only hex digits (0-9, A-F) and spaces are supported
 * @note All lowercase letters are automatically converted to uppercase
 * @note An Enter key is appended at the end for tag separation
 */
int usb_hid_send_epc(const uint8_t *epc, size_t len);

/**
 * @brief Check if HID device is ready to send data
 *
 * @return true if HID device is ready, false otherwise
 */
bool usb_hid_is_ready(void);

/**
 * @brief Set HID keyboard typing speed
 *
 * Sets the typing speed for EPC output. Speed is specified in CPM
 * (Characters Per Minute) and will be rounded to nearest 100.
 *
 * @param cpm Typing speed in CPM (100-1500, step 100)
 * @return 0 on success, -EINVAL if out of range
 */
int usb_hid_set_typing_speed(uint16_t cpm);

/**
 * @brief Get current HID keyboard typing speed
 *
 * @return Current typing speed in CPM
 */
uint16_t usb_hid_get_typing_speed(void);

/**
 * @brief Enable or disable HID output (software mute)
 *
 * When disabled, usb_hid_send_epc() will silently discard data
 * without sending to USB. USB connection remains active.
 *
 * @param enable true to enable output, false to mute
 */
void usb_hid_set_enabled(bool enable);

/**
 * @brief Check if HID output is enabled
 *
 * @return true if HID output is enabled, false if muted
 */
bool usb_hid_is_enabled(void);

#endif /* USB_HID_H */
