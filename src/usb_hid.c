/**
 * @file usb_hid.c
 * @brief USB HID Keyboard Implementation for EPC Output
 *
 * Thread Safety:
 * - usb_hid_send_epc() is protected by mutex (safe for concurrent calls)
 * - typing_speed_cpm uses atomic access
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
#include <zephyr/sys/atomic.h>
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
 * CPM to Delay Conversion Constants (Phase 4.1)
 * ======================================================================== */

/** Number of HID events per character (key press + key release) */
#define HID_EVENTS_PER_CHAR     2

/** Milliseconds per minute */
#define MS_PER_MINUTE           60000

/** Factor to convert CPM to delay: 60000ms / 2 events = 30000 */
#define CPM_TO_DELAY_FACTOR     (MS_PER_MINUTE / HID_EVENTS_PER_CHAR)

/* ========================================================================
 * HID Device State
 * ======================================================================== */

static const struct device *hid_dev;
static bool hid_ready = false;

/* Phase 1.3: Atomic typing speed variable */
static atomic_t typing_speed_cpm = ATOMIC_INIT(HID_TYPING_SPEED_DEFAULT);

/* Phase 2.3: Mutex for HID send protection */
static K_MUTEX_DEFINE(hid_send_lock);

/* Static buffer for HID reports (DMA-aligned) */
UDC_STATIC_BUF_DEFINE(hid_report, HID_KBD_REPORT_SIZE);

/**
 * @brief Calculate key delay in ms from typing speed (CPM)
 *
 * CPM = Characters Per Minute
 * Each character = 2 HID events (press + release)
 * Delay per event = 60000ms / CPM / 2 = 30000 / CPM
 */
static inline uint32_t cpm_to_delay_ms(uint16_t cpm)
{
	if (cpm == 0) {
		return 50; /* Safe default: ~600 CPM */
	}
	return CPM_TO_DELAY_FACTOR / cpm;
}

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
 * @param c ASCII character (will be converted to uppercase internally)
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

int parp_usb_hid_init(void)
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
	/* Phase 3.3: Input parameter validation first */
	if (!epc || len == 0) {
		return -EINVAL;
	}

	/* Device state validation */
	if (!hid_dev) {
		LOG_ERR("HID device not initialized");
		return -ENODEV;
	}

	if (!hid_ready) {
		LOG_WRN("HID interface not ready");
		return -EAGAIN;
	}

	/* Phase 2.3: Lock mutex for thread safety */
	k_mutex_lock(&hid_send_lock, K_FOREVER);

	/* Phase 1.3: Get typing speed atomically */
	uint16_t current_speed = (uint16_t)atomic_get(&typing_speed_cpm);
	uint32_t key_delay = cpm_to_delay_ms(current_speed);

	int result = 0;

	/* Phase 4.2: Send each character (toupper is done in ascii_to_hid_keycode) */
	for (size_t i = 0; i < len; i++) {
		char c = (char)epc[i];

		/* Get HID keycode (includes uppercase conversion) */
		uint8_t keycode = ascii_to_hid_keycode(c);
		if (keycode == 0) {
			LOG_DBG("Skipping invalid character: 0x%02X", (uint8_t)c);
			continue;  /* Skip invalid characters */
		}

		/* Key Press: report[0]=modifiers, report[2]=keycode */
		memset(hid_report, 0, HID_KBD_REPORT_SIZE);
		hid_report[2] = keycode;
		int ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
						   hid_report);
		if (ret < 0) {
			LOG_ERR("Failed to send key press: %d", ret);
			result = ret;
			goto unlock;
		}
		k_msleep(key_delay);  /* Delay between key press and release */

		/* Key Release: all zeros */
		memset(hid_report, 0, HID_KBD_REPORT_SIZE);
		ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
					       hid_report);
		if (ret < 0) {
			LOG_ERR("Failed to send key release: %d", ret);
			result = ret;
			goto unlock;
		}
		k_msleep(key_delay);  /* Delay between characters */
	}

	/* Send Enter key (0x28) for tag separation */
	memset(hid_report, 0, HID_KBD_REPORT_SIZE);
	hid_report[2] = 0x28;
	int ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
					   hid_report);
	if (ret < 0) {
		LOG_ERR("Failed to send Enter key: %d", ret);
		result = ret;
		goto unlock;
	}
	k_msleep(key_delay);

	/* Release Enter key */
	memset(hid_report, 0, HID_KBD_REPORT_SIZE);
	ret = hid_device_submit_report(hid_dev, HID_KBD_REPORT_SIZE,
				       hid_report);
	if (ret < 0) {
		LOG_ERR("Failed to release Enter key: %d", ret);
		result = ret;
		goto unlock;
	}

	LOG_INF("EPC sent via HID: %.*s (speed: %u CPM)", (int)len, epc, current_speed);

unlock:
	k_mutex_unlock(&hid_send_lock);
	return result;
}

bool usb_hid_is_ready(void)
{
	return hid_ready;
}

int usb_hid_set_typing_speed(uint16_t cpm)
{
	/* Round to nearest step */
	cpm = ((cpm + HID_TYPING_SPEED_STEP / 2) / HID_TYPING_SPEED_STEP)
	       * HID_TYPING_SPEED_STEP;

	/* Clamp to valid range */
	if (cpm < HID_TYPING_SPEED_MIN) {
		cpm = HID_TYPING_SPEED_MIN;
	} else if (cpm > HID_TYPING_SPEED_MAX) {
		cpm = HID_TYPING_SPEED_MAX;
	}

	/* Phase 1.3: Set atomically */
	atomic_set(&typing_speed_cpm, cpm);
	LOG_INF("Typing speed set to %u CPM (delay: %u ms)",
	        cpm, cpm_to_delay_ms(cpm));
	return 0;
}

uint16_t usb_hid_get_typing_speed(void)
{
	/* Phase 1.3: Get atomically */
	return (uint16_t)atomic_get(&typing_speed_cpm);
}
