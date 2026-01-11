/**
 * @file usb_hid.c
 * @brief USB HID Keyboard Implementation for EPC Output
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "usb_hid.h"
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/usb/udc_buf.h>
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_hid.h>
#include <zephyr/logging/log.h>
#include <ctype.h>
#include <string.h>

LOG_MODULE_REGISTER(usb_hid, LOG_LEVEL_INF);

/* ========================================================================
 * HID Report Descriptor
 * ======================================================================== */

/* Standard HID Keyboard Report Descriptor */
static const uint8_t hid_kbd_report_desc[] = HID_KEYBOARD_REPORT_DESC();

/* Standard keyboard report size */
#define HID_KBD_REPORT_SIZE 8

/* ========================================================================
 * HID Device State
 * ======================================================================== */

static const struct device *hid_dev;
static bool hid_ready = false;
UDC_STATIC_BUF_DEFINE(hid_report, HID_KBD_REPORT_SIZE);

/* ========================================================================
 * ASCII to HID Keycode Conversion
 * ======================================================================== */

/**
 * @brief Convert ASCII character to HID keyboard keycode
 *
 * Supports:
 * - Digits: 0-9
 * - Uppercase letters: A-F (for hex)
 * - Space character
 *
 * @param c ASCII character (will be converted to uppercase)
 * @return HID keycode, or 0 if invalid character
 */
static uint8_t ascii_to_hid_keycode(char c)
{
	/* Convert to uppercase */
	c = toupper((unsigned char)c);

	/* Digits 1-9 */
	if (c >= '1' && c <= '9') {
		return 0x1E + (c - '1');  /* 0x1E = '1', 0x26 = '9' */
	}
	/* Digit 0 */
	else if (c == '0') {
		return 0x27;  /* 0x27 = '0' */
	}
	/* Letters A-F (hex) */
	else if (c >= 'A' && c <= 'F') {
		return 0x04 + (c - 'A');  /* 0x04 = 'A', 0x09 = 'F' */
	}
	/* Space */
	else if (c == ' ') {
		return 0x2C;  /* 0x2C = Space */
	}

	return 0;  /* Invalid character */
}

/* ========================================================================
 * HID Callbacks
 * ======================================================================== */

/**
 * @brief HID interface ready callback
 */
static void hid_iface_ready(const struct device *dev, const bool ready)
{
	LOG_INF("HID interface %s", ready ? "ready" : "not ready");
	hid_ready = ready;
}

/**
 * @brief HID Get Report callback
 */
static int hid_get_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, uint8_t *const buf)
{
	LOG_DBG("Get Report: Type %u, ID %u", type, id);
	return 0;
}

/**
 * @brief HID Set Report callback (for LED status, etc.)
 */
static int hid_set_report(const struct device *dev,
			  const uint8_t type, const uint8_t id,
			  const uint16_t len, const uint8_t *const buf)
{
	if (type != HID_REPORT_TYPE_OUTPUT) {
		LOG_WRN("Unsupported report type: %u", type);
		return -ENOTSUP;
	}

	LOG_DBG("Set Report: Type %u, ID %u, Len %u", type, id, len);

	/* Handle keyboard LED status (Num Lock, Caps Lock, etc.) */
	if (len > 0) {
		LOG_DBG("Keyboard LEDs: 0x%02X", buf[0]);
	}

	return 0;
}

/**
 * @brief HID Set Protocol callback
 */
static void hid_set_protocol(const struct device *dev, const uint8_t proto)
{
	LOG_DBG("Set Protocol: %u", proto);
}

/**
 * @brief HID Set Idle callback
 */
static void hid_set_idle(const struct device *dev, const uint8_t id,
			 const uint32_t duration)
{
	LOG_DBG("Set Idle: ID %u, Duration %u", id, duration);
}

/* HID Operations structure */
static const struct hid_device_ops hid_ops = {
	.iface_ready = hid_iface_ready,
	.get_report = hid_get_report,
	.set_report = hid_set_report,
	.set_protocol = hid_set_protocol,
	.set_idle = hid_set_idle,
};

/* ========================================================================
 * Public API Implementation
 * ======================================================================== */

int usb_hid_init(void)
{
	/* Get HID device */
	hid_dev = DEVICE_DT_GET_ONE(zephyr_hid_device);
	if (!hid_dev) {
		LOG_ERR("HID device not found");
		return -ENODEV;
	}

	if (!device_is_ready(hid_dev)) {
		LOG_ERR("HID device not ready");
		return -ENODEV;
	}

	/* Register HID report descriptor and operations */
	int ret = hid_device_register(hid_dev, hid_kbd_report_desc,
				      sizeof(hid_kbd_report_desc), &hid_ops);
	if (ret != 0) {
		LOG_ERR("Failed to register HID device: %d", ret);
		return ret;
	}

	if (IS_ENABLED(CONFIG_USBD_HID_SET_POLLING_PERIOD)) {
		ret = hid_device_set_in_polling(hid_dev, 1000);
		if (ret != 0 && ret != -ENOTSUP) {
			LOG_WRN("Failed to set IN polling period: %d", ret);
		}

		ret = hid_device_set_out_polling(hid_dev, 1000);
		if (ret != 0 && ret != -ENOTSUP) {
			LOG_WRN("Failed to set OUT polling period: %d", ret);
		}
	}

	LOG_INF("USB HID Keyboard initialized");
	return 0;
}

int usb_hid_send_epc(const uint8_t *epc, size_t len)
{
	if (!hid_dev) {
		LOG_ERR("HID device not initialized");
		return -ENODEV;
	}

	if (!hid_ready) {
		LOG_WRN("HID interface not ready");
		return -EAGAIN;
	}

	if (!epc || len == 0) {
		return -EINVAL;
	}

	/* Send each character as uppercase HID keycode */
	for (size_t i = 0; i < len; i++) {
		char c = (char)epc[i];

		/* Convert to uppercase */
		c = toupper((unsigned char)c);

		/* Get HID keycode */
		uint8_t keycode = ascii_to_hid_keycode(c);
		if (keycode == 0) {
			LOG_DBG("Skipping invalid character: 0x%02X", c);
			continue;  /* Skip invalid characters */
		}

		/* Key Press: report[0]=modifiers, report[2]=keycode */
		memset(hid_report, 0, HID_KBD_REPORT_SIZE);
		hid_report[2] = keycode;
		int ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
						   hid_report);
		if (ret < 0) {
			LOG_ERR("Failed to send key press: %d", ret);
			return ret;
		}
		k_msleep(20);  /* Delay between key press and release */

		/* Key Release: all zeros */
		memset(hid_report, 0, HID_KBD_REPORT_SIZE);
		ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
					       hid_report);
		if (ret < 0) {
			LOG_ERR("Failed to send key release: %d", ret);
			return ret;
		}
		k_msleep(20);  /* Delay between characters */
	}

	/* Send Enter key (0x28) for tag separation */
	memset(hid_report, 0, HID_KBD_REPORT_SIZE);
	hid_report[2] = 0x28;
	int ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
					   hid_report);
	if (ret < 0) {
		LOG_ERR("Failed to send Enter key: %d", ret);
		return ret;
	}
	k_msleep(20);

	/* Release Enter key */
	memset(hid_report, 0, HID_KBD_REPORT_SIZE);
	ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
				       hid_report);
	if (ret < 0) {
		LOG_ERR("Failed to release Enter key: %d", ret);
		return ret;
	}

	LOG_INF("EPC sent via HID: %.*s", (int)len, epc);
	return 0;
}

bool usb_hid_is_ready(void)
{
	return hid_ready;
}
