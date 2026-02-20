/**
 * @file uart_router.c
 * @brief UART Router Implementation
 *
 * Thread Safety:
 * - uart_router_process() must be called from single thread only
 * - Shell commands are thread-safe (protected by mode_lock)
 * - Ring buffer reset operations are protected by disabling interrupts
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "uart_router.h"
#include "usb_hid.h"
#include "beep_control.h"
#include "rgb_led.h"
#include "switch_control.h"
#include "e310_settings.h"
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <zephyr/sys/atomic.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Phase 4.4: Unified logging level */
LOG_MODULE_REGISTER(uart_router, LOG_LEVEL_INF);

/* Global router pointer for shell commands */
static uart_router_t *g_router_instance;

/** Frame assembler timeout (ms) - reset if no byte for this duration */
#define FRAME_ASSEMBLER_TIMEOUT_MS  100

/* Re-entrance guard for uart_router_process() */
static atomic_t process_lock = ATOMIC_INIT(0);

/* ISR overrun flag — set in ISR, handled in thread context */
static atomic_t uart4_rx_overrun = ATOMIC_INIT(0);

/* Forward declarations */
static void frame_assembler_reset(frame_assembler_t *fa);

/* ========================================================================
 * Phase 1.1: Safe Ring Buffer Reset Helper
 * ======================================================================== */

/**
 * @brief Safely reset UART4 RX buffer and frame assembler with ISR protection
 */
static void safe_uart4_rx_reset(uart_router_t *router)
{
	uart_irq_rx_disable(router->uart4);
	ring_buf_reset(&router->uart4_rx_ring);
	frame_assembler_reset(&router->e310_frame);
	uart_irq_rx_enable(router->uart4);
}

/* ========================================================================
 * EPC Filter Functions (Duplicate Detection)
 * ======================================================================== */

/**
 * @brief Initialize EPC filter
 */
static void epc_filter_init(epc_filter_t *filter, uint32_t debounce_sec)
{
	memset(filter, 0, sizeof(epc_filter_t));
	filter->debounce_ms = debounce_sec * 1000;
}

/**
 * @brief Clear EPC filter cache
 */
static void epc_filter_clear(epc_filter_t *filter)
{
	filter->count = 0;
	filter->next_idx = 0;
	memset(filter->entries, 0, sizeof(filter->entries));
}

/**
 * @brief Set EPC filter debounce time
 */
static void epc_filter_set_debounce(epc_filter_t *filter, uint32_t debounce_sec)
{
	filter->debounce_ms = debounce_sec * 1000;
	LOG_INF("EPC debounce set to %u seconds", debounce_sec);
}

static void epc_filter_print_summary(epc_filter_t *filter)
{
	if (filter->count == 0) {
		return;
	}

	printk("--- %u tag(s) ---\n", filter->count);

	for (uint8_t i = 0; i < filter->count; i++) {
		epc_cache_entry_t *e = &filter->entries[i];
		if (e->epc_len == 0) {
			continue;
		}
		char line[128];
		int pos = snprintf(line, sizeof(line), "#%u ", i + 1);
		for (uint8_t j = 0; j < e->epc_len && pos < (int)sizeof(line) - 4; j++) {
			pos += snprintf(&line[pos], sizeof(line) - pos, "%02X", e->epc[j]);
			if (j % 2 == 1 && j < e->epc_len - 1) {
				line[pos++] = ' ';
			}
		}
		snprintf(&line[pos], sizeof(line) - pos,
			 "  RSSI:%u/%u  x%u\n", e->rssi_max, e->rssi_min,
			 e->read_count);
		printk("%s", line);
	}
}

static bool epc_filter_check(epc_filter_t *filter, const uint8_t *epc,
                             uint8_t epc_len, uint8_t rssi)
{
	int64_t now = k_uptime_get();

	for (uint8_t i = 0; i < filter->count; i++) {
		epc_cache_entry_t *entry = &filter->entries[i];

		if (entry->epc_len == epc_len &&
		    memcmp(entry->epc, epc, epc_len) == 0) {
			entry->read_count++;
			entry->last_seen = now;
			if (rssi > entry->rssi_max) {
				entry->rssi_max = rssi;
			}
			if (rssi < entry->rssi_min) {
				entry->rssi_min = rssi;
			}

			if ((now - entry->last_sent) < filter->debounce_ms) {
				return false;
			}
			entry->last_sent = now;
			return true;
		}
	}

	epc_cache_entry_t *entry = &filter->entries[filter->next_idx];
	memcpy(entry->epc, epc, epc_len);
	entry->epc_len = epc_len;
	entry->last_sent = now;
	entry->last_seen = now;
	entry->rssi_max = rssi;
	entry->rssi_min = rssi;
	entry->read_count = 1;

	filter->next_idx = (filter->next_idx + 1) % EPC_CACHE_SIZE;
	if (filter->count < EPC_CACHE_SIZE) {
		filter->count++;
	}

	return true;
}

/* ========================================================================
 * Frame Assembler Functions
 * ======================================================================== */

/**
 * @brief Reset frame assembler state
 */
static void frame_assembler_reset(frame_assembler_t *fa)
{
	fa->received = 0;
	fa->expected = 0;
	fa->state = FRAME_STATE_WAIT_LEN;
	fa->last_byte_time = 0;
}

/**
 * @brief Feed bytes into frame assembler
 *
 * @param fa Frame assembler context
 * @param data Input bytes
 * @param len Number of bytes
 * @return Number of bytes consumed, 0 if more data needed
 */
static size_t frame_assembler_feed(frame_assembler_t *fa,
                                    const uint8_t *data, size_t len)
{
	if (len == 0) {
		return 0;
	}

	int64_t now = k_uptime_get();

	/* Phase 4.3: Check timeout in ALL states (including partial reception) */
	if (fa->last_byte_time > 0 &&
	    (now - fa->last_byte_time) > FRAME_ASSEMBLER_TIMEOUT_MS) {
		LOG_DBG("Frame assembler timeout (state=%d, received=%zu)",
		        fa->state, fa->received);
		frame_assembler_reset(fa);
	}

	size_t consumed = 0;

	while (consumed < len && fa->state != FRAME_STATE_COMPLETE) {
		uint8_t byte = data[consumed];

		switch (fa->state) {
		case FRAME_STATE_WAIT_LEN:
			/* First byte is length - Len field includes Adr+Cmd+Data+CRC
			 * Total frame size = Len + 1 (Len byte itself not counted in Len) */
			fa->buffer[0] = byte;
			fa->received = 1;
			fa->expected = byte + 1; /* Total = Len value + Len byte */
			fa->last_byte_time = now;

			if (fa->expected < E310_MIN_RESPONSE_SIZE ||
			    fa->expected > E310_MAX_FRAME_SIZE) {
				LOG_WRN("Invalid frame length: %u", byte);
				frame_assembler_reset(fa);
			} else {
				fa->state = FRAME_STATE_RECEIVING;
			}
			break;

		case FRAME_STATE_RECEIVING:
			if (fa->received < E310_MAX_FRAME_SIZE) {
				fa->buffer[fa->received++] = byte;
				fa->last_byte_time = now;

				if (fa->received >= fa->expected) {
					fa->state = FRAME_STATE_COMPLETE;
				}
			} else {
				LOG_ERR("Frame buffer overflow");
				frame_assembler_reset(fa);
			}
			break;

		case FRAME_STATE_COMPLETE:
			/* Frame already complete, don't consume more */
			return consumed;
		}
		consumed++;
	}

	return consumed;
}

/**
 * @brief Check if frame is complete
 */
static bool frame_assembler_is_complete(frame_assembler_t *fa)
{
	return fa->state == FRAME_STATE_COMPLETE;
}

/**
 * @brief Get complete frame data
 */
static const uint8_t *frame_assembler_get_frame(frame_assembler_t *fa,
                                                 size_t *len)
{
	if (fa->state == FRAME_STATE_COMPLETE) {
		*len = fa->received;
		return fa->buffer;
	}
	*len = 0;
	return NULL;
}

/* ========================================================================
 * UART Interrupt Callbacks
 * ======================================================================== */

/**
 * @brief UART4 (E310 module) interrupt callback
 */
static void uart4_callback(const struct device *dev, void *user_data)
{
	uart_router_t *router = (uart_router_t *)user_data;

	if (!uart_irq_update(dev)) {
		return;
	}

	/* Handle RX */
	if (uart_irq_rx_ready(dev)) {
		uint8_t buf[64];
		int len = uart_fifo_read(dev, buf, sizeof(buf));

		if (len > 0) {
			int put = ring_buf_put(&router->uart4_rx_ring, buf, len);
			router->stats.uart4_rx_bytes += put;

			if (put < len) {
				router->stats.rx_overruns++;
				atomic_set(&uart4_rx_overrun, 1);
			}
		}
	}

	/* Phase 2.1: Handle TX with claim/finish pattern */
	if (uart_irq_tx_ready(dev)) {
		uint8_t *data;
		uint32_t len = ring_buf_get_claim(&router->uart4_tx_ring, &data, 64);

		if (len > 0) {
			int sent = uart_fifo_fill(dev, data, len);
			ring_buf_get_finish(&router->uart4_tx_ring, sent);
			router->stats.uart4_tx_bytes += sent;
		} else {
			/* No more data to send, disable TX interrupt */
			uart_irq_tx_disable(dev);
		}
	}
}

/* ========================================================================
 * Initialization
 * ======================================================================== */

int uart_router_init(uart_router_t *router)
{
	/* Clear context */
	memset(router, 0, sizeof(uart_router_t));

	/* Initialize mutex */
	k_mutex_init(&router->mode_lock);

	/* Get UART4 device (E310 RFID module) */
	router->uart4 = DEVICE_DT_GET(DT_NODELABEL(uart4));
	if (!device_is_ready(router->uart4)) {
		LOG_ERR("UART4 device not ready");
		return -ENODEV;
	}

	/* Initialize ring buffers for UART4 */
	ring_buf_init(&router->uart4_rx_ring, sizeof(router->uart4_rx_buf),
	              router->uart4_rx_buf);
	ring_buf_init(&router->uart4_tx_ring, sizeof(router->uart4_tx_buf),
	              router->uart4_tx_buf);

	/* Initialize E310 protocol context */
	e310_init(&router->e310_ctx, E310_ADDR_DEFAULT);

	/* Initialize frame assembler */
	frame_assembler_reset(&router->e310_frame);

	/* Initialize EPC filter with default debounce time */
	epc_filter_init(&router->epc_filter, EPC_DEBOUNCE_DEFAULT_SEC);

	router->mode = ROUTER_MODE_IDLE;
	router->inventory_active = false;
	router->uart4_ready = true;
	router->inventory_interval_ms = INVENTORY_INTERVAL_DEFAULT_MS;

	g_router_instance = router;

	LOG_INF("UART Router initialized (IDLE mode)");
	LOG_INF("  UART4 (E310): %s (PD0-RX, PD1-TX)", router->uart4->name);

	return 0;
}

/* ========================================================================
 * Start/Stop
 * ======================================================================== */

int uart_router_start(uart_router_t *router)
{
	if (router->running) {
		return -EALREADY;
	}

	/* Configure UART4 interrupt */
	uart_irq_callback_user_data_set(router->uart4, uart4_callback, router);
	uart_irq_rx_enable(router->uart4);

	router->running = true;

	LOG_INF("UART Router started");
	return 0;
}

int uart_router_stop(uart_router_t *router)
{
	if (!router->running) {
		return 0;
	}

	uart_irq_rx_disable(router->uart4);
	uart_irq_tx_disable(router->uart4);

	router->running = false;

	LOG_INF("UART Router stopped");
	return 0;
}

/* ========================================================================
 * Mode Control
 * ======================================================================== */

int uart_router_set_mode(uart_router_t *router, router_mode_t mode)
{
	k_mutex_lock(&router->mode_lock, K_FOREVER);

	router_mode_t old_mode = router->mode;
	router->mode = mode;

	k_mutex_unlock(&router->mode_lock);

	if (old_mode != mode) {
		LOG_INF("Mode changed: %s -> %s",
		        uart_router_get_mode_name(old_mode),
		        uart_router_get_mode_name(mode));

		safe_uart4_rx_reset(router);
	}

	return 0;
}

router_mode_t uart_router_get_mode(uart_router_t *router)
{
	k_mutex_lock(&router->mode_lock, K_FOREVER);
	router_mode_t mode = router->mode;
	k_mutex_unlock(&router->mode_lock);
	return mode;
}

/* ========================================================================
 * Data Processing
 * ======================================================================== */

/* Forward declaration */
bool e310_is_debug_mode(void);

/**
 * @brief Process a complete E310 frame
 */
static void process_e310_frame(uart_router_t *router,
                                const uint8_t *frame, size_t len)
{
	if (!router->inventory_active || e310_is_debug_mode()) {
		printk("RX[%zu]: ", len);
		for (size_t i = 0; i < len; i++) {
			printk("%02X ", frame[i]);
		}
		printk("\n");
	}

	/* Parse E310 protocol frame */
	e310_response_header_t header;
	int ret = e310_parse_response_header(frame, len, &header);

	if (ret != E310_OK) {
		LOG_DBG("Frame parse error: %d", ret);
		router->stats.parse_errors++;
		return;
	}

	if (e310_is_debug_mode() && !router->inventory_active) {
		printk("  -> Len=%u Addr=0x%02X Cmd=0x%02X Status=0x%02X (%s)\n",
		       header.len, header.addr, header.recmd, header.status,
		       e310_get_status_desc(header.status));
	}

	if (header.recmd == E310_RECMD_AUTO_UPLOAD) {
		/* Parse auto-upload tag (Tag Inventory mode) */
		e310_tag_data_t tag;
		ret = e310_parse_auto_upload_tag(&frame[4], len - 6, &tag);

		if (ret >= 0) {
			/* Format EPC as hex string without spaces for HID */
			char epc_str[64];
			int pos = 0;
			for (uint8_t b = 0; b < tag.epc_len && pos + 2 < (int)sizeof(epc_str); b++) {
				pos += snprintf(&epc_str[pos], sizeof(epc_str) - pos,
				                "%02X", tag.epc[b]);
			}
			epc_str[pos] = '\0';

			if (epc_filter_check(&router->epc_filter, tag.epc,
				                     tag.epc_len, tag.rssi)) {
				int hid_ret = usb_hid_send_epc((const uint8_t *)epc_str,
				                               strlen(epc_str));
				if (hid_ret >= 0) {
					router->stats.epc_sent++;
					beep_control_trigger();
					rgb_led_notify_tag_read();
				} else if (hid_ret != 0) {
					LOG_WRN("HID send failed: %d", hid_ret);
				}
			}

			router->stats.frames_parsed++;
		} else {
			LOG_WRN("Failed to parse auto-upload tag: %d", ret);
			router->stats.parse_errors++;
		}
	} else if (header.recmd == E310_CMD_TAG_INVENTORY) {
		router->stats.frames_parsed++;
		bool inventory_round_done = false;

		if (header.status == E310_STATUS_SUCCESS ||
		    header.status == E310_STATUS_MORE_DATA) {
			size_t data_len = len - 6;
			if (data_len >= 2) {
				uint8_t antenna = frame[4];
				uint8_t tag_count = frame[5];
				LOG_DBG("Tag Inventory: ant=%u, %u tag(s), %zu data bytes",
				        antenna, tag_count, data_len);

				const uint8_t *block_ptr = &frame[6];
				size_t remaining = data_len - 2;

			for (uint8_t i = 0; i < tag_count && remaining > 0; i++) {
				if (e310_is_debug_mode()) {
					printk("  RAW[%u/%u] %zu bytes:", i + 1,
					       tag_count, remaining);
					for (size_t d = 0;
					     d < remaining && d < 64; d++) {
						printk(" %02X", block_ptr[d]);
					}
					printk("\n");
				}

				e310_tag_data_t tag;
				int consumed = e310_parse_tag_data(
					block_ptr, remaining, &tag);
				if (consumed <= 0) {
					LOG_WRN("Tag Inventory: parse error %d at tag %u",
					        consumed, i);
					break;
				}

				tag.antenna = antenna;

				uint8_t *epc_data = tag.epc;
				uint8_t epc_len = tag.epc_len;

				if (e310_is_debug_mode()) {
					printk("  PARSED[%u]: epc_len=%u"
					       " has_tid=%d rssi=%u\n",
					       i + 1, epc_len,
					       tag.has_tid, tag.rssi);
					printk("    EPC:");
					for (uint8_t b = 0; b < epc_len; b++) {
						printk(" %02X", epc_data[b]);
					}
					printk("\n");
				}

				char epc_str[64];
				int epc_pos = 0;
				for (uint8_t b = 0; b < epc_len &&
				     epc_pos + 2 < (int)sizeof(epc_str); b++) {
					epc_pos += snprintf(&epc_str[epc_pos],
					                    sizeof(epc_str) - epc_pos,
					                    "%02X", epc_data[b]);
				}
				epc_str[epc_pos] = '\0';

				if (epc_filter_check(&router->epc_filter,
					             epc_data, epc_len, tag.rssi)) {
					int hid_ret = usb_hid_send_epc(
						(const uint8_t *)epc_str,
						strlen(epc_str));
					if (hid_ret >= 0) {
						router->stats.epc_sent++;
						beep_control_trigger();
						rgb_led_notify_tag_read();
					} else if (hid_ret != 0) {
						LOG_WRN("HID send failed: %d",
						        hid_ret);
					}
				}

					block_ptr += consumed;
					remaining -= consumed;
				}
			}
			if (header.status == E310_STATUS_SUCCESS) {
				inventory_round_done = true;
			}
		} else if (header.status == E310_STATUS_OPERATION_COMPLETE) {
			inventory_round_done = true;
		} else if (header.status == E310_STATUS_INVENTORY_TIMEOUT) {
			inventory_round_done = true;
		} else {
			LOG_WRN("Tag Inventory: 0x%02X (%s)",
			        header.status, e310_get_status_desc(header.status));
			inventory_round_done = true;
		}

		if (inventory_round_done && router->inventory_active) {
			router->inventory_active = false;

			if (router->inventory_interval_ms > 0) {
				router->next_inventory_time =
					k_uptime_get() +
					router->inventory_interval_ms;
			} else {
				epc_filter_print_summary(&router->epc_filter);
				switch_control_set_inventory_state(false);
				LOG_INF("Inventory round complete (single-shot)");
			}
		}
	} else if (header.recmd == E310_CMD_OBTAIN_READER_INFO) {
		router->stats.frames_parsed++;
		if (header.status == E310_STATUS_SUCCESS && len > 6) {
			e310_reader_info_t info;
			ret = e310_parse_reader_info(&frame[4], len - 6, &info);
			if (ret == E310_OK) {
				printk("=== Reader Info ===\n");
				printk("  FW Version: %u.%u\n",
				       info.firmware_version >> 8,
				       info.firmware_version & 0xFF);
				printk("  Model: 0x%02X\n", info.model_type);
				printk("  Protocol: 0x%02X\n", info.protocol_type);
				{
					/* Decode frequency band from bit7-6 of MaxFre/MinFre */
					uint8_t band_hi = (info.max_freq >> 6) & 0x03;
					uint8_t band_lo = (info.min_freq >> 6) & 0x03;
					uint8_t max_point = info.max_freq & 0x3F;
					uint8_t min_point = info.min_freq & 0x3F;
					const char *band_name = "Unknown";
					if (band_hi == 0 && band_lo == 1) band_name = "China2";
					else if (band_hi == 0 && band_lo == 2) band_name = "US";
					else if (band_hi == 0 && band_lo == 3) band_name = "Korea";
					else if (band_hi == 1 && band_lo == 0) band_name = "EU";
					else if (band_hi == 2 && band_lo == 0) band_name = "China1";
					printk("  Freq: %s, channels %u-%u (%u ch)\n",
					       band_name, min_point, max_point,
					       max_point - min_point + 1);
				}
				printk("  RF Power: %u dBm\n", info.power);
				printk("  Scan Time: %u\n", info.scan_time);
				printk("  Antenna: 0x%02X\n", info.antenna);
			} else {
				printk("Reader Info: parse error %d\n", ret);
			}
		} else {
			printk("Reader Info: Status 0x%02X (%s)\n",
			       header.status, e310_get_status_desc(header.status));
		}
	} else if (header.recmd == E310_CMD_OBTAIN_READER_SN) {
		router->stats.frames_parsed++;
		if (header.status == E310_STATUS_SUCCESS && len > 6) {
			size_t sn_len = len - 6;
			printk("Reader SN: ");
			for (size_t i = 0; i < sn_len; i++) {
				printk("%02X", frame[4 + i]);
			}
			printk("\n");
		} else {
			printk("Reader SN: Status 0x%02X (%s)\n",
			       header.status, e310_get_status_desc(header.status));
		}
	} else if (header.recmd == 0x92) {
		router->stats.frames_parsed++;
		if (header.status == E310_STATUS_SUCCESS && len > 6) {
			int8_t temp;
			ret = e310_parse_temperature(&frame[4], len - 6, &temp);
			if (ret == E310_OK) {
				printk("Reader Temperature: %d C\n", temp);
			} else {
				printk("Temperature: parse error %d\n", ret);
			}
		} else {
			printk("Temperature: Status 0x%02X (%s)\n",
			       header.status, e310_get_status_desc(header.status));
		}
	} else if (header.recmd == E310_CMD_GET_TAG_COUNT_FROM_BUFFER) {
		router->stats.frames_parsed++;
		if (header.status == E310_STATUS_SUCCESS && len > 6) {
			uint32_t count;
			ret = e310_parse_tag_count(&frame[4], len - 6, &count);
			if (ret == E310_OK) {
				printk("Tag Count: %u\n", count);
			} else {
				printk("Tag Count: parse error %d\n", ret);
			}
		} else {
			printk("Tag Count: Status 0x%02X (%s)\n",
			       header.status, e310_get_status_desc(header.status));
		}
	} else {
		router->stats.frames_parsed++;
		printk("E310 [0x%02X] Status=0x%02X (%s)",
		       header.recmd, header.status,
		       e310_get_status_desc(header.status));
		if (header.status == E310_STATUS_SUCCESS && len > 6) {
			printk(" Data:");
			for (size_t i = 4; i < len - 2; i++) {
				printk(" %02X", frame[i]);
			}
		}
		printk("\n");
	}
}

/**
 * @brief Process inventory mode (UART4 → Parser → USB HID)
 */
static void process_inventory_mode(uart_router_t *router)
{
	uint8_t buf[128];
	int len;

	if (atomic_cas(&uart4_rx_overrun, 1, 0)) {
		LOG_WRN("UART4 RX overrun (lost bytes), resetting assembler");
		frame_assembler_reset(&router->e310_frame);
	}

	len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));
	if (len <= 0) {
		return;
	}

	/* Feed data into frame assembler */
	size_t offset = 0;
	while (offset < (size_t)len) {
		size_t consumed = frame_assembler_feed(&router->e310_frame,
		                                       &buf[offset],
		                                       len - offset);
		if (consumed == 0) {
			break;
		}
		offset += consumed;

		/* Check if we have a complete frame */
		if (frame_assembler_is_complete(&router->e310_frame)) {
			size_t frame_len;
			const uint8_t *frame = frame_assembler_get_frame(
			                         &router->e310_frame, &frame_len);

			if (frame && frame_len > 0) {
				int ret = e310_verify_crc(frame, frame_len);
				if (ret == E310_OK) {
					process_e310_frame(router, frame, frame_len);
					frame_assembler_reset(&router->e310_frame);
			} else {
				LOG_WRN("Frame CRC error (len=%zu)", frame_len);
				printk("RX[%zu]: ", frame_len);
				for (size_t i = 0; i < frame_len; i++) {
					printk("%02X ", frame[i]);
				}
				printk("\n");
				if (frame_len >= 3) {
					uint16_t calc_crc = e310_crc16(frame, frame_len - 2);
					uint16_t frame_crc = frame[frame_len - 2] | (frame[frame_len - 1] << 8);
					printk("CRC calc=%04X frame=%04X\n", calc_crc, frame_crc);
				}
				router->stats.parse_errors++;
				safe_uart4_rx_reset(router);
				return;
			}
			} else {
				frame_assembler_reset(&router->e310_frame);
			}
		}
	}
}

static int send_inventory_command(uart_router_t *router)
{
	safe_uart4_rx_reset(router);

	/* 연속 모드: ScanTime=10 (1초), 단발: ScanTime=50 (5초) */
	uint8_t scan_time = (router->inventory_interval_ms > 0) ? 10 : 50;
	int len = e310_build_tag_inventory_scan_time(&router->e310_ctx,
						     scan_time);
	if (len < 0) {
		return len;
	}
	return uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
}

void uart_router_process(uart_router_t *router)
{
	if (!router->running) {
		return;
	}

	/*
	 * Re-entrance guard: prevent concurrent calls from main loop
	 * and shell thread (via wait_for_e310_response).
	 * atomic_cas returns true if we successfully set 0->1.
	 */
	if (!atomic_cas(&process_lock, 0, 1)) {
		return; /* Another thread is already processing */
	}

	process_inventory_mode(router);

	if (router->inventory_interval_ms > 0 &&
	    router->next_inventory_time > 0 &&
	    router->mode == ROUTER_MODE_INVENTORY &&
	    k_uptime_get() >= router->next_inventory_time) {
		router->inventory_active = true;
		if (send_inventory_command(router) < 0) {
			router->inventory_active = false;
		}
		router->next_inventory_time =
			k_uptime_get() +
			router->inventory_interval_ms;
	}

	atomic_set(&process_lock, 0);
}

/* ========================================================================
 * Statistics
 * ======================================================================== */

void uart_router_get_stats(uart_router_t *router, uart_router_stats_t *stats)
{
	memcpy(stats, &router->stats, sizeof(uart_router_stats_t));
}

void uart_router_reset_stats(uart_router_t *router)
{
	memset(&router->stats, 0, sizeof(uart_router_stats_t));
}

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

const char *uart_router_get_mode_name(router_mode_t mode)
{
	switch (mode) {
	case ROUTER_MODE_IDLE:       return "IDLE";
	case ROUTER_MODE_INVENTORY:  return "INVENTORY";
	default:                     return "UNKNOWN";
	}
}

int uart_router_send_uart4(uart_router_t *router, const uint8_t *data, size_t len)
{
	if (!router->uart4_ready) {
		return -ENODEV;
	}

	if (!router->inventory_active) {
		printk("TX[%zu]: ", len);
		for (size_t i = 0; i < len; i++) {
			printk("%02X ", data[i]);
		}
		printk("\n");
	}

	/* Queue data in TX ring buffer */
	int put = ring_buf_put(&router->uart4_tx_ring, data, len);
	if (put > 0) {
		/* Enable TX interrupt to start transmission */
		uart_irq_tx_enable(router->uart4);
	}

	if (put < (int)len) {
		router->stats.tx_errors++;
		LOG_WRN("UART4 TX buffer full: lost %zu bytes", len - put);
	}

	return put;
}

/* ========================================================================
 * E310 RFID Control API
 * ======================================================================== */

/**
 * @brief Wait for E310 response with timeout (poll-only, no processing)
 *
 * Sleeps and polls frame counters. Actual UART processing is done by
 * main loop's uart_router_process() — this function MUST NOT call it
 * to avoid driving the parser from the shell thread context.
 */
static int wait_for_e310_response(uart_router_t *router, int timeout_ms)
{
	int64_t start = k_uptime_get();
	uint32_t initial_frames = router->stats.frames_parsed;
	uint32_t initial_errors = router->stats.parse_errors;

	while ((k_uptime_get() - start) < timeout_ms) {
		if (atomic_cas(&process_lock, 0, 1)) {
			process_inventory_mode(router);
			atomic_set(&process_lock, 0);
		}

		if (router->stats.frames_parsed > initial_frames ||
		    router->stats.parse_errors > initial_errors) {
			return 0;
		}

		k_msleep(10);
	}

	return -ETIMEDOUT;
}

int uart_router_connect_e310(uart_router_t *router)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	int len, ret;
	int responses_ok = 0;
	uint8_t saved_addr = router->e310_ctx.reader_addr;

	/* Set mode to IDLE for command/response processing */
	router_mode_t saved_mode = router->mode;
	uart_router_set_mode(router, ROUTER_MODE_IDLE);

	/* Reset frame assembler */
	frame_assembler_reset(&router->e310_frame);

	LOG_INF("Connecting to E310...");

	/* Step 1: Get Reader Info with broadcast address (0xFF) */
	router->e310_ctx.reader_addr = E310_ADDR_BROADCAST;
	len = e310_build_obtain_reader_info(&router->e310_ctx);
	if (len > 0) {
		ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
		if (ret > 0 && wait_for_e310_response(router, 200) == 0) {
			responses_ok++;
		} else {
			LOG_WRN("E310 broadcast query: no response");
		}
	}

	/* Step 2: Get Reader Info with specific address (0x00) */
	router->e310_ctx.reader_addr = E310_ADDR_DEFAULT;
	len = e310_build_obtain_reader_info(&router->e310_ctx);
	if (len > 0) {
		ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
		if (ret > 0 && wait_for_e310_response(router, 200) == 0) {
			responses_ok++;
		} else {
			LOG_WRN("E310 address query: no response");
		}
	}

	/* Step 3: Stop any running inventory */
	len = e310_build_stop_immediately(&router->e310_ctx);
	if (len > 0) {
		ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
		if (ret > 0) {
			wait_for_e310_response(router, 200);
			/* Stop response is optional - E310 may not be running */
		}
	}

	/* Step 4: Set work mode */
	len = e310_build_set_work_mode(&router->e310_ctx, 0x00);
	if (len > 0) {
		ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
		if (ret > 0 && wait_for_e310_response(router, 200) == 0) {
			responses_ok++;
		} else {
			LOG_WRN("E310 set work mode: no response");
		}
	}

	/* Restore original address and mode */
	router->e310_ctx.reader_addr = saved_addr;
	uart_router_set_mode(router, saved_mode);

	/* Require at least one successful response to consider connected */
	if (responses_ok > 0) {
		router->e310_connected = true;
		LOG_INF("E310 connected (%d/%d responses OK)", responses_ok, 3);

		len = e310_build_enable_antenna_check(&router->e310_ctx, false);
		if (len > 0) {
			ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
			if (ret > 0 && wait_for_e310_response(router, 200) == 0) {
				LOG_INF("Antenna check disabled");
			} else {
				LOG_WRN("Failed to disable antenna check");
			}
		}

		uint8_t saved_power = e310_settings_get_rf_power();
		len = e310_build_modify_rf_power(&router->e310_ctx, saved_power);
		if (len > 0) {
			ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
			if (ret > 0 && wait_for_e310_response(router, 200) == 0) {
				LOG_INF("RF power applied: %u dBm", saved_power);
			} else {
				LOG_WRN("Failed to apply RF power");
			}
		}

		return 0;
	}

	LOG_ERR("E310 connection failed: no responses received");
	router->e310_connected = false;
	return -ETIMEDOUT;
}

int uart_router_start_inventory(uart_router_t *router)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	if (!router->e310_connected) {
		LOG_INF("E310 not connected, running init sequence...");
		int ret = uart_router_connect_e310(router);
		if (ret < 0) {
			LOG_ERR("E310 connection failed: %d", ret);
			return ret;
		}
		router->e310_connected = true;
	}

	/* Set mode to INVENTORY first — uart_router_set_mode() resets ring buffers
	 * on mode change, so we must do this BEFORE queuing the TX command.
	 * Otherwise the TX ring buffer gets wiped and E310 never receives the command. */
	uart_router_set_mode(router, ROUTER_MODE_INVENTORY);

	/* Clear EPC cache for new inventory session */
	epc_filter_clear(&router->epc_filter);

	int len = e310_build_tag_inventory_default(&router->e310_ctx);
	if (len < 0) {
		LOG_ERR("Failed to build start inventory command: %d", len);
		uart_router_set_mode(router, ROUTER_MODE_IDLE);
		return len;
	}

	/* Send command to E310 via UART4 */
	int ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		LOG_ERR("Failed to send start inventory command: %d", ret);
		uart_router_set_mode(router, ROUTER_MODE_IDLE);
		return ret;
	}

	router->inventory_active = true;
	switch_control_set_inventory_state(true);
	usb_hid_set_enabled(true);
	rgb_led_set_inventory_status(true);

	LOG_INF("E310 Tag Inventory started (interval %u ms, HID=ON)",
	        router->inventory_interval_ms);
	return 0;
}

int uart_router_stop_inventory(uart_router_t *router)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	router->inventory_active = false;
	router->next_inventory_time = 0;
	switch_control_set_inventory_state(false);
	usb_hid_set_enabled(false);
	rgb_led_set_inventory_status(false);

	epc_filter_print_summary(&router->epc_filter);

	int len = e310_build_stop_immediately(&router->e310_ctx);
	if (len < 0) {
		LOG_ERR("Failed to build stop command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		LOG_ERR("Failed to send stop command: %d", ret);
		return ret;
	}

	LOG_INF("E310 Tag Inventory stopped (HID=OFF)");
	return 0;
}

int uart_router_set_rf_power(uart_router_t *router, uint8_t power)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	/* Clamp power to valid range */
	if (power > 30) {
		power = 30;
	}

	/* Build Modify RF Power command */
	int len = e310_build_modify_rf_power(&router->e310_ctx, power);
	if (len < 0) {
		LOG_ERR("Failed to build RF power command: %d", len);
		return len;
	}

	/* Send command to E310 via UART4 */
	int ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		LOG_ERR("Failed to send RF power command: %d", ret);
		return ret;
	}

	LOG_INF("E310 RF power set to %u dBm", power);
	return 0;
}

int uart_router_get_reader_info(uart_router_t *router)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	/* Build Obtain Reader Info command */
	int len = e310_build_obtain_reader_info(&router->e310_ctx);
	if (len < 0) {
		LOG_ERR("Failed to build reader info command: %d", len);
		return len;
	}

	/* Send command to E310 via UART4 */
	int ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		LOG_ERR("Failed to send reader info command: %d", ret);
		return ret;
	}

	LOG_INF("E310 Reader info requested");
	return 0;
}

bool uart_router_is_inventory_active(uart_router_t *router)
{
	return router->inventory_active;
}

/* ========================================================================
 * Shell Commands
 * ======================================================================== */

static int cmd_router_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "UART Router not initialized");
		return -ENODEV;
	}

	uart_router_t *router = g_router_instance;

	shell_print(sh, "=== UART Router Status ===");
	shell_print(sh, "Running: %s", router->running ? "yes" : "no");
	shell_print(sh, "Mode: %s", uart_router_get_mode_name(router->mode));
	shell_print(sh, "");
	shell_print(sh, "UART4 (E310 - PD1-TX/PD0-RX):");
	shell_print(sh, "  Device: %s", router->uart4->name);
	shell_print(sh, "  TX buffer: %u/%u bytes",
		    ring_buf_size_get(&router->uart4_tx_ring),
		    UART_ROUTER_BUF_SIZE);
	shell_print(sh, "  RX buffer: %u/%u bytes",
		    ring_buf_size_get(&router->uart4_rx_ring),
		    UART_ROUTER_BUF_SIZE);

	return 0;
}

static int cmd_router_stats(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "UART Router not initialized");
		return -ENODEV;
	}

	uart_router_stats_t stats;
	uart_router_get_stats(g_router_instance, &stats);

	shell_print(sh, "=== UART Router Statistics ===");
	shell_print(sh, "USART1 (Console):");
	shell_print(sh, "  RX: %u bytes", stats.uart1_rx_bytes);
	shell_print(sh, "  TX: %u bytes", stats.uart1_tx_bytes);
	shell_print(sh, "UART4 (E310):");
	shell_print(sh, "  RX: %u bytes", stats.uart4_rx_bytes);
	shell_print(sh, "  TX: %u bytes", stats.uart4_tx_bytes);
	shell_print(sh, "Errors:");
	shell_print(sh, "  RX overruns: %u", stats.rx_overruns);
	shell_print(sh, "  TX errors: %u", stats.tx_errors);
	shell_print(sh, "E310 Protocol:");
	shell_print(sh, "  Frames parsed: %u", stats.frames_parsed);
	shell_print(sh, "  Parse errors: %u", stats.parse_errors);
	shell_print(sh, "  EPC sent (HID): %u", stats.epc_sent);

	return 0;
}

static int cmd_router_mode(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "UART Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Current mode: %s",
			    uart_router_get_mode_name(g_router_instance->mode));
		shell_print(sh, "Usage: router mode <idle|inventory>");
		return 0;
	}

	router_mode_t new_mode;
	if (strcmp(argv[1], "idle") == 0) {
		new_mode = ROUTER_MODE_IDLE;
	} else if (strcmp(argv[1], "inventory") == 0) {
		new_mode = ROUTER_MODE_INVENTORY;
	} else {
		shell_error(sh, "Unknown mode: %s", argv[1]);
		shell_print(sh, "Valid modes: idle, inventory");
		return -EINVAL;
	}

	uart_router_set_mode(g_router_instance, new_mode);
	shell_print(sh, "Mode set to: %s", uart_router_get_mode_name(new_mode));

	return 0;
}

/* Shell command registration */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_router,
	SHELL_CMD(status, NULL, "Show router status", cmd_router_status),
	SHELL_CMD(stats, NULL, "Show router statistics", cmd_router_stats),
	SHELL_CMD(mode, NULL, "Get/set router mode", cmd_router_mode),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(router, &sub_router, "UART Router commands", NULL);

/* ========================================================================
 * E310 Shell Commands
 * ======================================================================== */

static int cmd_e310_connect(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	shell_print(sh, "Connecting to E310...");
	int ret = uart_router_connect_e310(g_router_instance);
	if (ret < 0) {
		shell_error(sh, "Connection failed: %d", ret);
		return ret;
	}

	shell_print(sh, "E310 connected successfully");
	return 0;
}

static int cmd_e310_start(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int ret = uart_router_start_inventory(g_router_instance);
	if (ret < 0) {
		shell_error(sh, "Failed to start inventory: %d", ret);
		return ret;
	}

	/* Return immediately — main loop handles all E310 response processing.
	 * Previously called wait_for_e310_response(3000) here, which blocked
	 * the shell thread and called uart_router_process() from shell context,
	 * causing UART1 freeze due to shell thread starvation. */
	shell_print(sh, "E310 Tag Inventory started");
	return 0;
}

static int cmd_e310_stop(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int ret = uart_router_stop_inventory(g_router_instance);
	if (ret < 0) {
		shell_error(sh, "Failed to stop inventory: %d", ret);
		return ret;
	}

	uart_router_set_mode(g_router_instance, ROUTER_MODE_IDLE);
	shell_print(sh, "E310 Tag Inventory stopped");
	return 0;
}

static int cmd_e310_power(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Current RF power: %d dBm (saved)", e310_settings_get_rf_power());
		shell_print(sh, "Usage: e310 power <0-30>");
		return 0;
	}

	int power = atoi(argv[1]);
	if (power < 0 || power > 30) {
		shell_error(sh, "Invalid power: %d (must be 0-30)", power);
		return -EINVAL;
	}

	int ret = uart_router_set_rf_power(g_router_instance, (uint8_t)power);
	if (ret < 0) {
		shell_error(sh, "Failed to set RF power: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);
	ret = e310_settings_set_rf_power((uint8_t)power);
	if (ret < 0) {
		shell_warn(sh, "RF power set but failed to save: %d", ret);
	}

	shell_print(sh, "RF power set to %d dBm (saved)", power);
	return 0;
}

static int cmd_e310_info(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int ret = uart_router_get_reader_info(g_router_instance);
	if (ret < 0) {
		shell_error(sh, "Failed to request reader info: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 500) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	shell_print(sh, "=== E310 Status ===");
	shell_print(sh, "Inventory active: %s",
	            g_router_instance->inventory_active ? "YES" : "no");
	shell_print(sh, "Router mode: %s",
	            uart_router_get_mode_name(g_router_instance->mode));
	shell_print(sh, "Reader address: 0x%02X",
	            g_router_instance->e310_ctx.reader_addr);
	shell_print(sh, "Frames parsed: %u", g_router_instance->stats.frames_parsed);
	shell_print(sh, "Parse errors: %u", g_router_instance->stats.parse_errors);
	shell_print(sh, "EPC sent (HID): %u", g_router_instance->stats.epc_sent);
	shell_print(sh, "UART4 RX bytes: %u", g_router_instance->stats.uart4_rx_bytes);
	shell_print(sh, "UART4 TX bytes: %u", g_router_instance->stats.uart4_tx_bytes);

	return 0;
}

/** Debug mode flag for showing raw E310 frames */
static bool e310_debug_mode = false;

/**
 * @brief Parse hex string to bytes
 * @return Number of bytes parsed, or negative on error
 */
static int parse_hex_string(const char *hex_str, uint8_t *out, size_t max_len)
{
	size_t len = strlen(hex_str);
	size_t out_idx = 0;

	for (size_t i = 0; i < len && out_idx < max_len; i += 2) {
		/* Skip spaces */
		while (hex_str[i] == ' ' && i < len) {
			i++;
		}
		if (i >= len) break;

		/* Parse two hex chars */
		char byte_str[3] = {0};
		byte_str[0] = hex_str[i];
		if (i + 1 < len && hex_str[i + 1] != ' ') {
			byte_str[1] = hex_str[i + 1];
		} else {
			/* Single char - treat as 0X */
			byte_str[1] = byte_str[0];
			byte_str[0] = '0';
			i--; /* Adjust index */
		}

		char *endptr;
		unsigned long val = strtoul(byte_str, &endptr, 16);
		if (*endptr != '\0') {
			return -EINVAL;
		}
		out[out_idx++] = (uint8_t)val;
	}

	return (int)out_idx;
}

static int cmd_e310_send(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Usage: e310 send <hex bytes>");
		shell_print(sh, "Example: e310 send 04 FF 21 (Get Reader Info)");
		shell_print(sh, "         e310 send 09 00 01 04 FE 00 80 32 (Tag Inventory)");
		shell_print(sh, "Note: CRC is automatically appended");
		return 0;
	}

	/* Concatenate all arguments into one hex string */
	char hex_buf[256] = {0};
	size_t pos = 0;
	for (int i = 1; i < argc && pos < sizeof(hex_buf) - 1; i++) {
		size_t arg_len = strlen(argv[i]);
		if (pos + arg_len + 1 >= sizeof(hex_buf)) break;
		memcpy(&hex_buf[pos], argv[i], arg_len);
		pos += arg_len;
		hex_buf[pos++] = ' ';
	}

	/* Parse hex string */
	uint8_t cmd_buf[64];
	int cmd_len = parse_hex_string(hex_buf, cmd_buf, sizeof(cmd_buf) - 2);
	if (cmd_len <= 0) {
		shell_error(sh, "Invalid hex string");
		return -EINVAL;
	}

	/* Calculate and append CRC */
	uint16_t crc = e310_crc16(cmd_buf, cmd_len);
	cmd_buf[cmd_len++] = (uint8_t)(crc & 0xFF);
	cmd_buf[cmd_len++] = (uint8_t)((crc >> 8) & 0xFF);

	/* Show what we're sending */
	shell_fprintf(sh, SHELL_NORMAL, "TX[%d]: ", cmd_len);
	for (int i = 0; i < cmd_len; i++) {
		shell_fprintf(sh, SHELL_NORMAL, "%02X ", cmd_buf[i]);
	}
	shell_fprintf(sh, SHELL_NORMAL, "\n");

	/* Send to UART4 */
	int ret = uart_router_send_uart4(g_router_instance, cmd_buf, cmd_len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 1000) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_debug(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Debug mode: %s", e310_debug_mode ? "ON" : "OFF");
		shell_print(sh, "Usage: e310 debug <on|off>");
		return 0;
	}

	if (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0) {
		e310_debug_mode = true;
		shell_print(sh, "Debug mode enabled - raw frames will be shown");
	} else if (strcmp(argv[1], "off") == 0 || strcmp(argv[1], "0") == 0) {
		e310_debug_mode = false;
		shell_print(sh, "Debug mode disabled");
	} else {
		shell_error(sh, "Invalid argument: %s (use on/off)", argv[1]);
		return -EINVAL;
	}

	return 0;
}

static int cmd_e310_addr(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Current address: 0x%02X (saved: 0x%02X)",
		            g_router_instance->e310_ctx.reader_addr,
		            e310_settings_get_reader_addr());
		shell_print(sh, "Usage: e310 addr <0x00-0xFF>");
		shell_print(sh, "  0xFF = broadcast (works with most modules)");
		return 0;
	}

	unsigned long addr = strtoul(argv[1], NULL, 0);
	if (addr > 0xFF) {
		shell_error(sh, "Invalid address: %s (must be 0x00-0xFF)", argv[1]);
		return -EINVAL;
	}

	g_router_instance->e310_ctx.reader_addr = (uint8_t)addr;

	int ret = e310_settings_set_reader_addr((uint8_t)addr);
	if (ret < 0) {
		shell_warn(sh, "Address set but failed to save: %d", ret);
	}

	shell_print(sh, "Reader address set to 0x%02X (saved)", (uint8_t)addr);
	return 0;
}

static int cmd_e310_single(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	/* Build and send single tag inventory command */
	int len = e310_build_single_tag_inventory(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 1000) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	/* Send stop immediately command */
	int len = e310_build_stop_immediately(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);

	g_router_instance->inventory_active = false;
	g_router_instance->next_inventory_time = 0;
	switch_control_set_inventory_state(false);
	usb_hid_set_enabled(false);
	rgb_led_set_inventory_status(false);
	uart_router_set_mode(g_router_instance, ROUTER_MODE_IDLE);

	shell_print(sh, "E310 stop command sent, router reset to IDLE");
	return 0;
}

static int cmd_e310_sn(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int len = e310_build_obtain_reader_sn(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 500) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_temp(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int len = e310_build_measure_temperature(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 1000) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_buffer_count(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int len = e310_build_get_tag_count(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 500) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_buffer_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int len = e310_build_clear_memory_buffer(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 500) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_buffer_get(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int len = e310_build_get_data_from_buffer(&g_router_instance->e310_ctx);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 500) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

/**
 * @brief Check if debug mode is enabled (for use by frame processor)
 */
bool e310_is_debug_mode(void)
{
	return e310_debug_mode;
}

static int cmd_e310_buzzer(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Usage: e310 buzzer <on|off|beep [duration]>");
		shell_print(sh, "  on     - Enable buzzer");
		shell_print(sh, "  off    - Disable buzzer");
		shell_print(sh, "  beep N - Beep for N*100ms (1-255)");
		return 0;
	}

	int len;
	if (strcmp(argv[1], "on") == 0) {
		len = e310_build_enable_buzzer(&g_router_instance->e310_ctx, true);
	} else if (strcmp(argv[1], "off") == 0) {
		len = e310_build_enable_buzzer(&g_router_instance->e310_ctx, false);
	} else if (strcmp(argv[1], "beep") == 0) {
		uint8_t duration_100ms = (argc >= 3) ? atoi(argv[2]) : 1;
		uint8_t active_time = (duration_100ms > 127) ? 255 : (duration_100ms * 2);
		len = e310_build_led_buzzer_control(&g_router_instance->e310_ctx,
		                                     active_time, 0, 1);
	} else {
		shell_error(sh, "Unknown option: %s", argv[1]);
		return -EINVAL;
	}

	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);
	shell_print(sh, "Buzzer command sent");
	return 0;
}

static int cmd_e310_led(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Usage: e310 led <on|off>");
		return 0;
	}

	uint8_t led_state = (strcmp(argv[1], "on") == 0) ? 1 : 0;
	int len = e310_build_led_buzzer_control(&g_router_instance->e310_ctx,
	                                         led_state,
	                                         0,
	                                         led_state);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);
	shell_print(sh, "LED %s", led_state ? "ON" : "OFF");
	return 0;
}

static int cmd_e310_antenna(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Current antenna: 0x%02x (saved)", e310_settings_get_antenna());
		shell_print(sh, "Usage: e310 antenna <config>");
		return 0;
	}

	unsigned long config = strtoul(argv[1], NULL, 0);
	int len = e310_build_setup_antenna_mux(&g_router_instance->e310_ctx,
	                                        (uint8_t)config);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);
	ret = e310_settings_set_antenna((uint8_t)config);
	if (ret < 0) {
		shell_warn(sh, "Antenna set but failed to save: %d", ret);
	}

	shell_print(sh, "Antenna config set to 0x%02X (saved)", (uint8_t)config);
	return 0;
}

static int cmd_e310_freq(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 4) {
		uint8_t region, start, end;
		e310_settings_get_frequency(&region, &start, &end);
		shell_print(sh, "Current: region=%d, range=%d-%d (saved)", region, start, end);
		shell_print(sh, "Usage: e310 freq <region> <start> <end>");
		shell_print(sh, "  region: 1=China, 2=US, 3=Europe, 4=Korea");
		shell_print(sh, "  start/end: Frequency index (0-62)");
		return 0;
	}

	uint8_t region = atoi(argv[1]);
	uint8_t start = atoi(argv[2]);
	uint8_t end = atoi(argv[3]);

	/* Encode region + frequency points into MaxFre/MinFre bytes per protocol 0x22 */
	uint8_t band_hi = 0;
	uint8_t band_lo = 0;
	switch (region) {
	case 1:
		band_hi = 0x00;
		band_lo = 0x01;
		break; /* China */
	case 2:
		band_hi = 0x00;
		band_lo = 0x02;
		break; /* US */
	case 3:
		band_hi = 0x01;
		band_lo = 0x00;
		break; /* Europe */
	case 4:
		band_hi = 0x00;
		band_lo = 0x03;
		break; /* Korea */
	default:
		shell_error(sh, "Invalid region %d (1=China, 2=US, 3=Europe, 4=Korea)", region);
		return -EINVAL;
	}

	uint8_t max_fre = (band_hi << 6) | (end & 0x3F);
	uint8_t min_fre = (band_lo << 6) | (start & 0x3F);

	int len = e310_build_modify_frequency(&g_router_instance->e310_ctx,
	                                       max_fre, min_fre);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);
	ret = e310_settings_set_frequency(region, start, end);
	if (ret < 0) {
		shell_warn(sh, "Frequency set but failed to save: %d", ret);
	}

	shell_print(sh, "Frequency set: region=%u, range=%u-%u (saved)", region, start, end);
	return 0;
}

static int cmd_e310_invtime(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		uint8_t inv_time = e310_settings_get_inventory_time();
		shell_print(sh, "Current inventory time: %d (%.1f sec, saved)",
			    inv_time, (double)(inv_time * 0.1f));
		shell_print(sh, "Usage: e310 invtime <time>");
		shell_print(sh, "  time: Inventory time in 100ms units (1-255)");
		return 0;
	}

	uint8_t time = atoi(argv[1]);
	int len = e310_build_modify_inventory_time(&g_router_instance->e310_ctx, time);
	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	wait_for_e310_response(g_router_instance, 500);
	ret = e310_settings_set_inventory_time(time);
	if (ret < 0) {
		shell_warn(sh, "Inventory time set but failed to save: %d", ret);
	}

	shell_print(sh, "Inventory time set to %ums (saved)", time * 100);
	return 0;
}

static int cmd_e310_interval(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Current interval: %u ms",
			    g_router_instance->inventory_interval_ms);
		shell_print(sh, "Usage: e310 interval <ms>");
		return 0;
	}

	uint32_t ms = atoi(argv[1]);
	g_router_instance->inventory_interval_ms = ms;
	shell_print(sh, "Inventory interval set to %u ms", ms);
	return 0;
}

static int cmd_e310_gpio(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	int len;
	if (argc < 2) {
		len = e310_build_obtain_gpio_state(&g_router_instance->e310_ctx);
		shell_print(sh, "Usage: e310 gpio [state]");
		shell_print(sh, "  (no arg) - Get current GPIO state");
		shell_print(sh, "  state    - Set GPIO state (0x00-0xFF)");
		shell_print(sh, "Requesting current GPIO state...");
	} else {
		unsigned long state = strtoul(argv[1], NULL, 0);
		len = e310_build_gpio_control(&g_router_instance->e310_ctx, (uint8_t)state);
		shell_print(sh, "GPIO state set to 0x%02X", (uint8_t)state);
	}

	if (len < 0) {
		shell_error(sh, "Failed to build command: %d", len);
		return len;
	}

	int ret = uart_router_send_uart4(g_router_instance,
	                                  g_router_instance->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	if (wait_for_e310_response(g_router_instance, 500) < 0) {
		shell_warn(sh, "No response from E310");
	}
	return 0;
}

static int cmd_e310_settings_show(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	e310_settings_print(sh);
	return 0;
}

static int cmd_e310_settings_reset(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	int ret = e310_settings_reset();
	if (ret < 0) {
		shell_error(sh, "Failed to reset settings: %d", ret);
		return ret;
	}

	shell_print(sh, "Settings reset to defaults");
	e310_settings_print(sh);
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_e310_settings,
	SHELL_CMD(show, NULL, "Show current settings", cmd_e310_settings_show),
	SHELL_CMD(reset, NULL, "Reset to factory defaults", cmd_e310_settings_reset),
	SHELL_SUBCMD_SET_END
);

/* E310 buffer sub-commands */
SHELL_STATIC_SUBCMD_SET_CREATE(sub_e310_buffer,
	SHELL_CMD(count, NULL, "Get tag count in buffer", cmd_e310_buffer_count),
	SHELL_CMD(get, NULL, "Get data from buffer", cmd_e310_buffer_get),
	SHELL_CMD(clear, NULL, "Clear memory buffer", cmd_e310_buffer_clear),
	SHELL_SUBCMD_SET_END
);

SHELL_STATIC_SUBCMD_SET_CREATE(sub_e310,
	SHELL_CMD(connect, NULL, "Connect to E310 (init sequence)", cmd_e310_connect),
	SHELL_CMD(start, NULL, "Start Tag Inventory", cmd_e310_start),
	SHELL_CMD(stop, NULL, "Stop Tag Inventory", cmd_e310_stop),
	SHELL_CMD(single, NULL, "Single tag inventory", cmd_e310_single),
	SHELL_CMD(power, NULL, "Set RF power (0-30 dBm)", cmd_e310_power),
	SHELL_CMD(freq, NULL, "Set frequency region/range", cmd_e310_freq),
	SHELL_CMD(invtime, NULL, "Set inventory time", cmd_e310_invtime),
	SHELL_CMD(interval, NULL, "Set inventory interval (ms)", cmd_e310_interval),
	SHELL_CMD(antenna, NULL, "Set antenna configuration", cmd_e310_antenna),
	SHELL_CMD(buzzer, NULL, "Buzzer control", cmd_e310_buzzer),
	SHELL_CMD(led, NULL, "LED control", cmd_e310_led),
	SHELL_CMD(gpio, NULL, "GPIO control/status", cmd_e310_gpio),
	SHELL_CMD(info, NULL, "Request reader information", cmd_e310_info),
	SHELL_CMD(sn, NULL, "Get reader serial number", cmd_e310_sn),
	SHELL_CMD(temp, NULL, "Measure reader temperature", cmd_e310_temp),
	SHELL_CMD(status, NULL, "Show E310 status", cmd_e310_status),
	SHELL_CMD(addr, NULL, "Get/set reader address", cmd_e310_addr),
	SHELL_CMD(buffer, &sub_e310_buffer, "Buffer operations", NULL),
	SHELL_CMD(settings, &sub_e310_settings, "Persistent settings", NULL),
	SHELL_CMD(send, NULL, "Send raw hex command", cmd_e310_send),
	SHELL_CMD(debug, NULL, "Enable/disable debug mode", cmd_e310_debug),
	SHELL_CMD(reset, NULL, "Stop E310 and reset router", cmd_e310_reset),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(e310, &sub_e310, "E310 RFID module commands", NULL);

/* ========================================================================
 * USB Control Shell Commands
 * ======================================================================== */

static int cmd_usb_hid(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "HID output: %s", usb_hid_is_enabled() ? "ON" : "OFF (muted)");
		shell_print(sh, "Usage: usb hid <on|off>");
		return 0;
	}

	if (strcmp(argv[1], "on") == 0) {
		usb_hid_set_enabled(true);
		shell_print(sh, "HID output enabled");
	} else if (strcmp(argv[1], "off") == 0) {
		usb_hid_set_enabled(false);
		shell_print(sh, "HID output disabled (muted)");
	} else {
		shell_error(sh, "Invalid argument: %s (use on/off)", argv[1]);
		return -EINVAL;
	}
	return 0;
}

static int cmd_usb_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	shell_print(sh, "=== USB Status ===");
	shell_print(sh, "HID output: %s", usb_hid_is_enabled() ? "ON" : "OFF (muted)");
	shell_print(sh, "HID ready: %s", usb_hid_is_ready() ? "yes" : "no");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_usb,
	SHELL_CMD(hid, NULL, "HID output on/off", cmd_usb_hid),
	SHELL_CMD(status, NULL, "Show USB status", cmd_usb_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(usb, &sub_usb, "USB control commands", NULL);

/* ========================================================================
 * HID Shell Commands
 * ======================================================================== */

static int cmd_hid_speed(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Current typing speed: %u CPM", usb_hid_get_typing_speed());
		shell_print(sh, "Usage: hid speed <100-1500>");
		return 0;
	}

	int speed = atoi(argv[1]);
	int ret = usb_hid_set_typing_speed((uint16_t)speed);
	if (ret < 0) {
		shell_error(sh, "Failed to set speed: %d", ret);
		return ret;
	}

	shell_print(sh, "Typing speed set to %u CPM", usb_hid_get_typing_speed());
	return 0;
}

static int cmd_hid_debounce(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Current debounce: %u seconds",
		            g_router_instance->epc_filter.debounce_ms / 1000);
		shell_print(sh, "Usage: hid debounce <seconds>");
		return 0;
	}

	uint32_t sec = atoi(argv[1]);
	epc_filter_set_debounce(&g_router_instance->epc_filter, sec);
	shell_print(sh, "Debounce set to %u seconds", sec);
	return 0;
}

static int cmd_hid_clear(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	epc_filter_clear(&g_router_instance->epc_filter);
	shell_print(sh, "EPC cache cleared");
	return 0;
}

static int cmd_hid_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	shell_print(sh, "=== HID Status ===");
	shell_print(sh, "Output: %s", usb_hid_is_enabled() ? "ON" : "OFF (muted)");
	shell_print(sh, "Ready: %s", usb_hid_is_ready() ? "yes" : "no");
	shell_print(sh, "Typing speed: %u CPM", usb_hid_get_typing_speed());
	shell_print(sh, "EPC Filter:");
	shell_print(sh, "  Debounce: %u sec", g_router_instance->epc_filter.debounce_ms / 1000);
	shell_print(sh, "  Cached EPCs: %u/%u", g_router_instance->epc_filter.count, EPC_CACHE_SIZE);
	shell_print(sh, "  EPCs sent: %u", g_router_instance->stats.epc_sent);
	return 0;
}

static int cmd_hid_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	const char *test_epc = "E200 1234 5678 9ABC DEF0";

	shell_print(sh, "Sending test EPC: %s", test_epc);
	int ret = usb_hid_send_epc((const uint8_t *)test_epc, strlen(test_epc));
	if (ret < 0) {
		shell_error(sh, "Failed to send: %d", ret);
		return ret;
	}

	shell_print(sh, "Test EPC sent (check keyboard output)");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_hid,
	SHELL_CMD(speed, NULL, "Get/set typing speed (CPM)", cmd_hid_speed),
	SHELL_CMD(debounce, NULL, "Get/set EPC debounce time (seconds)", cmd_hid_debounce),
	SHELL_CMD(clear, NULL, "Clear EPC cache", cmd_hid_clear),
	SHELL_CMD(status, NULL, "Show HID status", cmd_hid_status),
	SHELL_CMD(test, NULL, "Send test EPC", cmd_hid_test),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(hid, &sub_hid, "HID keyboard commands", NULL);
