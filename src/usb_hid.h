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

/**
 * @brief Initialize USB HID Keyboard
 *
 * Initializes the USB HID keyboard device and prepares it for sending
 * EPC data as keyboard input.
 *
 * @return 0 on success, negative error code on failure
 */
int usb_hid_init(void);

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

#endif /* USB_HID_H */
