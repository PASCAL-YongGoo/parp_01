/**
 * @file usb_device.h
 * @brief USB Device Stack (NEXT) initialization helpers
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef USB_DEVICE_H
#define USB_DEVICE_H

#include <zephyr/usb/usbd.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize USB device support and register classes
 *
 * This sets up descriptors, configurations, and registers all enabled
 * class instances. The HID device must be registered before this is called.
 *
 * @param msg_cb Optional USBD message callback (may be NULL)
 * @return Pointer to initialized USB device context, or NULL on error
 */
struct usbd_context *usb_device_init(usbd_msg_cb_t msg_cb);

#ifdef __cplusplus
}
#endif

#endif /* USB_DEVICE_H */
