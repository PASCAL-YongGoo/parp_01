/**
 * @file usb_hid.c
 * @brief USB HID Keyboard Implementation for EPC Output
 *
 * Thread Safety:
 * - usb_hid_send_epc() is protected by mutex (safe for concurrent calls)
 * - typing_speed_cpm uses atomic access
 * - hid_stats uses atomic counters for lock-free statistics
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
 * Constants
 * ======================================================================== */

/** Number of HID events per character (key press + key release) */
#define HID_EVENTS_PER_CHAR     2

/** Milliseconds per minute */
#define MS_PER_MINUTE           60000

/** Factor to convert CPM to delay: 60000ms / 2 events = 30000 */
#define CPM_TO_DELAY_FACTOR     (MS_PER_MINUTE / HID_EVENTS_PER_CHAR)

/** Maximum retries for a single HID report submission */
#define HID_SUBMIT_MAX_RETRIES  5

/** Delay between retries in milliseconds */
#define HID_SUBMIT_RETRY_DELAY_MS  10

/* ========================================================================
 * HID Device State
 * ======================================================================== */

static const struct device *hid_dev;
static bool hid_ready = false;

/* HID output mute state (default: true = muted for development) */
static bool hid_muted = true;

/* Atomic typing speed variable */
static atomic_t typing_speed_cpm = ATOMIC_INIT(HID_TYPING_SPEED_DEFAULT);

/* Mutex for HID send protection */
static K_MUTEX_DEFINE(hid_send_lock);

/* Static buffer for HID reports (DMA-aligned) */
UDC_STATIC_BUF_DEFINE(hid_report, HID_KBD_REPORT_SIZE);

/* ========================================================================
 * Send Statistics (atomic, lock-free)
 * ======================================================================== */

static struct {
	atomic_t chars_attempted;  /* Total characters attempted */
	atomic_t chars_sent;       /* Successfully sent characters */
	atomic_t chars_dropped;    /* Characters dropped after all retries exhausted */
	atomic_t retries;          /* Total retry attempts across all sends */
	atomic_t epc_sent;         /* Complete EPCs sent successfully */
	atomic_t epc_partial;      /* EPCs with partial character drops */
	atomic_t submit_errors;    /* Total hid_device_submit_report failures */
} hid_stats;

/* ========================================================================
 * Internal Helpers
 * ======================================================================== */

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

/**
 * @brief Submit a HID report with retry logic
 *
 * Retries up to HID_SUBMIT_MAX_RETRIES times on transient failures
 * (-ENOMEM, -EBUSY). Non-retryable errors (-EACCES) return immediately.
 * Tracks retry count in statistics.
 *
 * @param report Report buffer to submit
 * @param size Report size in bytes
 * @return 0 on success, negative error code if all retries exhausted
 */
static int hid_submit_with_retry(const uint8_t *report, uint16_t size)
{
	int ret;

	for (int attempt = 0; attempt <= HID_SUBMIT_MAX_RETRIES; attempt++) {
		if (attempt > 0) {
			atomic_inc(&hid_stats.retries);
			k_msleep(HID_SUBMIT_RETRY_DELAY_MS);
			LOG_DBG("HID submit retry %d/%d", attempt,
				HID_SUBMIT_MAX_RETRIES);
		}

		ret = hid_device_submit_report(hid_dev, size, report);
		if (ret == 0) {
			return 0;
		}

		atomic_inc(&hid_stats.submit_errors);

		/* Non-retryable errors: exit immediately */
		if (ret == -EACCES) {
			LOG_ERR("HID class disabled (USB disconnected?)");
			return ret;
		}
	}

	LOG_WRN("HID submit failed after %d retries: %d",
		HID_SUBMIT_MAX_RETRIES, ret);
	return ret;
}

/* ========================================================================
 * ASCII to HID Keycode Conversion
 * ======================================================================== */

/**
 * @brief Convert ASCII character to HID keyboard keycode and modifier
 *
 * Supports:
 * - Digits: 0-9
 * - Letters: A-F (for hex, always sent as uppercase with Shift modifier)
 * - Space character
 *
 * @param c ASCII character (will be converted to uppercase internally)
 * @param modifier Output: HID modifier byte (0x02 = Left Shift for uppercase)
 * @return HID keycode, or 0 if invalid character
 */
static uint8_t ascii_to_hid_keycode(char c, uint8_t *modifier)
{
	*modifier = 0;

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
	/* Letters A-F (hex) — send with Left Shift for uppercase */
	else if (c >= 'A' && c <= 'F') {
		*modifier = 0x02;  /* Left Shift */
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
	/* Input parameter validation */
	if (!epc || len == 0) {
		return -EINVAL;
	}

	/* Mute check - silently discard if muted */
	if (hid_muted) {
		LOG_DBG("HID muted, EPC not sent");
		return 0;
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

	/* Lock mutex for thread safety */
	k_mutex_lock(&hid_send_lock, K_FOREVER);

	/* Get typing speed atomically */
	uint16_t current_speed = (uint16_t)atomic_get(&typing_speed_cpm);
	uint32_t key_delay = cpm_to_delay_ms(current_speed);

	int result = 0;
	int chars_dropped = 0;

	for (size_t i = 0; i < len; i++) {
		char c = (char)epc[i];

		uint8_t modifier = 0;
		uint8_t keycode = ascii_to_hid_keycode(c, &modifier);
		if (keycode == 0) {
			LOG_DBG("Skipping invalid character: 0x%02X", (uint8_t)c);
			continue;
		}

		atomic_inc(&hid_stats.chars_attempted);

		/* Key Press */
		memset(hid_report, 0, HID_KBD_REPORT_SIZE);
		hid_report[0] = modifier;
		hid_report[2] = keycode;
		int ret = hid_submit_with_retry(hid_report, HID_KBD_REPORT_SIZE);
		if (ret < 0) {
			if (ret == -EACCES) {
				/* USB disconnected: abort entire send */
				LOG_ERR("USB disconnected during EPC send at char %zu", i);
				result = ret;
				goto unlock;
			}
			/* Transient failure: skip this character, continue with rest */
			LOG_WRN("Dropped char '%c' at pos %zu (press failed: %d)",
				c, i, ret);
			chars_dropped++;
			atomic_inc(&hid_stats.chars_dropped);
			k_msleep(key_delay);
			continue;
		}
		k_msleep(key_delay);

		/* Key Release: all zeros */
		memset(hid_report, 0, HID_KBD_REPORT_SIZE);
		ret = hid_submit_with_retry(hid_report, HID_KBD_REPORT_SIZE);
		if (ret < 0) {
			if (ret == -EACCES) {
				LOG_ERR("USB disconnected during key release at char %zu", i);
				result = ret;
				goto unlock;
			}
			/* Release failure: key may appear "stuck" briefly */
			LOG_WRN("Key release failed for '%c' at pos %zu: %d",
				c, i, ret);
			chars_dropped++;
			atomic_inc(&hid_stats.chars_dropped);
			k_msleep(key_delay);
			continue;
		}
		k_msleep(key_delay);

		atomic_inc(&hid_stats.chars_sent);
	}

	/* Send Enter key (0x28) for tag separation */
	memset(hid_report, 0, HID_KBD_REPORT_SIZE);
	hid_report[2] = 0x28;
	int ret = hid_submit_with_retry(hid_report, HID_KBD_REPORT_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to send Enter key: %d", ret);
		result = ret;
		goto unlock;
	}
	k_msleep(key_delay);

	/* Release Enter key */
	memset(hid_report, 0, HID_KBD_REPORT_SIZE);
	ret = hid_submit_with_retry(hid_report, HID_KBD_REPORT_SIZE);
	if (ret < 0) {
		LOG_ERR("Failed to release Enter key: %d", ret);
		result = ret;
		goto unlock;
	}

	/* Track EPC-level statistics */
	if (chars_dropped > 0) {
		atomic_inc(&hid_stats.epc_partial);
		LOG_WRN("EPC sent with %d dropped chars: %.*s (speed: %u CPM)",
			chars_dropped, (int)len, epc, current_speed);
	} else {
		atomic_inc(&hid_stats.epc_sent);
		LOG_INF("EPC sent via HID: %.*s (speed: %u CPM)",
			(int)len, epc, current_speed);
	}

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

	atomic_set(&typing_speed_cpm, cpm);
	LOG_INF("Typing speed set to %u CPM (delay: %u ms)",
	        cpm, cpm_to_delay_ms(cpm));
	return 0;
}

uint16_t usb_hid_get_typing_speed(void)
{
	return (uint16_t)atomic_get(&typing_speed_cpm);
}

void usb_hid_set_enabled(bool enable)
{
	hid_muted = !enable;
	LOG_INF("HID output %s", enable ? "enabled" : "disabled (muted)");
}

bool usb_hid_is_enabled(void)
{
	return !hid_muted;
}

void usb_hid_get_stats(struct usb_hid_stats *stats)
{
	if (!stats) {
		return;
	}
	stats->chars_attempted = (uint32_t)atomic_get(&hid_stats.chars_attempted);
	stats->chars_sent = (uint32_t)atomic_get(&hid_stats.chars_sent);
	stats->chars_dropped = (uint32_t)atomic_get(&hid_stats.chars_dropped);
	stats->retries = (uint32_t)atomic_get(&hid_stats.retries);
	stats->epc_sent = (uint32_t)atomic_get(&hid_stats.epc_sent);
	stats->epc_partial = (uint32_t)atomic_get(&hid_stats.epc_partial);
	stats->submit_errors = (uint32_t)atomic_get(&hid_stats.submit_errors);
}

void usb_hid_reset_stats(void)
{
	atomic_set(&hid_stats.chars_attempted, 0);
	atomic_set(&hid_stats.chars_sent, 0);
	atomic_set(&hid_stats.chars_dropped, 0);
	atomic_set(&hid_stats.retries, 0);
	atomic_set(&hid_stats.epc_sent, 0);
	atomic_set(&hid_stats.epc_partial, 0);
	atomic_set(&hid_stats.submit_errors, 0);
	LOG_INF("HID stats reset");
}
