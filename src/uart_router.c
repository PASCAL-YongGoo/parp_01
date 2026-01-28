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
#include <zephyr/logging/log.h>
#include <zephyr/shell/shell.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>

/* Phase 4.4: Unified logging level */
LOG_MODULE_REGISTER(uart_router, LOG_LEVEL_INF);

/* Global router pointer for shell commands */
static uart_router_t *g_router_instance;

/** Frame assembler timeout (ms) - reset if no byte for this duration */
#define FRAME_ASSEMBLER_TIMEOUT_MS  100

/* Forward declarations */
static void frame_assembler_reset(frame_assembler_t *fa);

/* ========================================================================
 * Phase 1.1: Safe Ring Buffer Reset Helper
 * ======================================================================== */

/**
 * @brief Safely reset all ring buffers with ISR protection
 *
 * Disables UART interrupts before resetting to prevent race conditions
 * between ISR and main loop.
 */
static void safe_ring_buf_reset_all(uart_router_t *router)
{
	/* Disable all UART interrupts */
	uart_irq_rx_disable(router->uart1);
	uart_irq_tx_disable(router->uart1);
	uart_irq_rx_disable(router->uart4);
	uart_irq_tx_disable(router->uart4);

	/* Reset all buffers */
	ring_buf_reset(&router->uart1_rx_ring);
	ring_buf_reset(&router->uart1_tx_ring);
	ring_buf_reset(&router->uart4_rx_ring);
	ring_buf_reset(&router->uart4_tx_ring);

	/* Re-enable RX interrupts */
	uart_irq_rx_enable(router->uart1);
	uart_irq_rx_enable(router->uart4);
	/* TX is enabled only when there's data to send */
}

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
			/* First byte is length */
			fa->buffer[0] = byte;
			fa->received = 1;
			fa->expected = byte + E310_CRC16_LENGTH; /* len + CRC */
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
 * @brief UART1 (PC/Console) interrupt callback
 */
static void uart1_callback(const struct device *dev, void *user_data)
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
			/* Put data into ring buffer */
			int put = ring_buf_put(&router->uart1_rx_ring, buf, len);
			router->stats.uart1_rx_bytes += put;

			if (put < len) {
				router->stats.rx_overruns++;
				LOG_WRN("UART1 RX overrun: lost %d bytes", len - put);
			}
		}
	}

	/* Phase 2.1: Handle TX with claim/finish pattern */
	if (uart_irq_tx_ready(dev)) {
		uint8_t *data;
		uint32_t len = ring_buf_get_claim(&router->uart1_tx_ring, &data, 64);

		if (len > 0) {
			int sent = uart_fifo_fill(dev, data, len);
			ring_buf_get_finish(&router->uart1_tx_ring, sent);
			router->stats.uart1_tx_bytes += sent;
		} else {
			/* No more data to send, disable TX interrupt */
			uart_irq_tx_disable(dev);
		}
	}
}

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
			/* Put data into ring buffer */
			int put = ring_buf_put(&router->uart4_rx_ring, buf, len);
			router->stats.uart4_rx_bytes += put;

			/* Phase 2.2: Reset frame assembler on overrun */
			if (put < len) {
				router->stats.rx_overruns++;
				LOG_WRN("UART4 RX overrun: lost %d bytes, resetting assembler",
				        len - put);
				frame_assembler_reset(&router->e310_frame);
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

	/* Get CDC ACM device (USB virtual serial) */
	router->uart1 = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));
	if (!device_is_ready(router->uart1)) {
		LOG_ERR("CDC ACM UART device not ready");
		return -ENODEV;
	}

	router->uart4 = DEVICE_DT_GET(DT_NODELABEL(uart4));
	if (!device_is_ready(router->uart4)) {
		LOG_ERR("UART4 device not ready");
		return -ENODEV;
	}

	/* Initialize ring buffers (RX and TX) */
	ring_buf_init(&router->uart1_rx_ring, sizeof(router->uart1_rx_buf),
	              router->uart1_rx_buf);
	ring_buf_init(&router->uart1_tx_ring, sizeof(router->uart1_tx_buf),
	              router->uart1_tx_buf);
	ring_buf_init(&router->uart4_rx_ring, sizeof(router->uart4_rx_buf),
	              router->uart4_rx_buf);
	ring_buf_init(&router->uart4_tx_ring, sizeof(router->uart4_tx_buf),
	              router->uart4_tx_buf);

	/* Initialize E310 protocol context */
	e310_init(&router->e310_ctx, E310_ADDR_DEFAULT);

	/* Initialize frame assembler */
	frame_assembler_reset(&router->e310_frame);

	/* Set initial mode */
	router->mode = ROUTER_MODE_IDLE;
	router->inventory_active = false;
	router->uart1_ready = true;
	router->uart4_ready = true;
	router->host_connected = false;

	/* Store global reference for shell commands */
	g_router_instance = router;

	LOG_INF("UART Router initialized");
	LOG_INF("  CDC ACM: %s", router->uart1->name);
	LOG_INF("  UART4: %s", router->uart4->name);

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

	/* Configure UART interrupts */
	uart_irq_callback_user_data_set(router->uart1, uart1_callback, router);
	uart_irq_callback_user_data_set(router->uart4, uart4_callback, router);

	/* Enable RX interrupts */
	uart_irq_rx_enable(router->uart1);
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

	/* Disable interrupts */
	uart_irq_rx_disable(router->uart1);
	uart_irq_tx_disable(router->uart1);
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

		/* Phase 1.1: Safely reset all ring buffers */
		safe_ring_buf_reset_all(router);

		/* Reset frame assembler on mode change */
		frame_assembler_reset(&router->e310_frame);
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

/**
 * @brief Process bypass mode (transparent UART1 <-> UART4 bridging)
 */
static void process_bypass_mode(uart_router_t *router)
{
	uint8_t buf[128];
	int len;

	/* Forward UART1 RX -> UART4 TX */
	len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));
	if (len > 0) {
		int put = ring_buf_put(&router->uart4_tx_ring, buf, len);
		if (put > 0) {
			uart_irq_tx_enable(router->uart4);
		}
		if (put < len) {
			router->stats.tx_errors++;
		}
	}

	/* Forward UART4 RX -> UART1 TX (only if host connected) */
	len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));
	if (len > 0 && router->host_connected) {
		int put = ring_buf_put(&router->uart1_tx_ring, buf, len);
		if (put > 0) {
			uart_irq_tx_enable(router->uart1);
		}
		if (put < len) {
			router->stats.tx_errors++;
		}
	}
}

/**
 * @brief Process a complete E310 frame
 */
static void process_e310_frame(uart_router_t *router,
                                const uint8_t *frame, size_t len)
{
	/* Parse E310 protocol frame */
	e310_response_header_t header;
	int ret = e310_parse_response_header(frame, len, &header);

	if (ret != E310_OK) {
		LOG_DBG("Frame parse error: %d", ret);
		router->stats.parse_errors++;
		return;
	}

	/* Echo raw data to CDC ACM for debugging (only if host connected) */
	if (router->host_connected) {
		int put = ring_buf_put(&router->uart1_tx_ring, frame, len);
		if (put > 0) {
			uart_irq_tx_enable(router->uart1);
		}
	}

	if (header.recmd == E310_RECMD_AUTO_UPLOAD) {
		/* Parse auto-upload tag (Fast Inventory mode) */
		e310_tag_data_t tag;
		ret = e310_parse_auto_upload_tag(&frame[4], len - 6, &tag);

		if (ret >= 0) {
			/* Phase 3.4: Check format result */
			char epc_str[128];
			int fmt_ret = e310_format_epc_string(tag.epc, tag.epc_len,
			                                     epc_str, sizeof(epc_str));
			if (fmt_ret < 0) {
				LOG_ERR("Failed to format EPC: %d", fmt_ret);
				router->stats.parse_errors++;
				return;
			}

			/* Send to USB HID Keyboard */
			ret = usb_hid_send_epc((uint8_t *)epc_str, fmt_ret);
			if (ret == 0) {
				router->stats.epc_sent++;
				LOG_INF("EPC: %s (RSSI: %u)", epc_str, tag.rssi);
			} else {
				LOG_ERR("Failed to send EPC via HID: %d", ret);
				router->stats.tx_errors++;
			}

			router->stats.frames_parsed++;
		} else {
			LOG_WRN("Failed to parse auto-upload tag: %d", ret);
			router->stats.parse_errors++;
		}
	} else {
		/* Other response types */
		LOG_DBG("E310 response: reCmd=0x%02X, status=0x%02X",
		        header.recmd, header.status);
		router->stats.frames_parsed++;
	}
}

/**
 * @brief Process inventory mode (UART4 → Parser → USB HID)
 */
static void process_inventory_mode(uart_router_t *router)
{
	uint8_t buf[128];
	int len;

	/* CDC input is now handled by Shell backend directly */
	/* No longer discarding - allows shell commands during inventory */

	/* Get UART4 data (E310 responses) */
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
				/* Verify CRC before processing */
				int ret = e310_verify_crc(frame, frame_len);
				if (ret == E310_OK) {
					process_e310_frame(router, frame, frame_len);
				} else {
					/* Phase 3.1: Dump bad frame for debugging */
					LOG_WRN("Frame CRC error (len=%zu)", frame_len);
					LOG_HEXDUMP_DBG(frame, frame_len, "Bad frame");
					router->stats.parse_errors++;
				}
			}

			/* Reset for next frame */
			frame_assembler_reset(&router->e310_frame);
		}
	}
}

void uart_router_process(uart_router_t *router)
{
	if (!router->running) {
		return;
	}

	/* Periodically check host connection status */
	uart_router_check_host_connection(router);

	router_mode_t mode = uart_router_get_mode(router);

	switch (mode) {
	case ROUTER_MODE_BYPASS:
		process_bypass_mode(router);
		break;

	case ROUTER_MODE_INVENTORY:
		process_inventory_mode(router);
		break;

	case ROUTER_MODE_IDLE:
	default:
		/* Phase 1.2: Discard data without ring_buf_reset */
		{
			uint8_t discard[64];
			ring_buf_get(&router->uart1_rx_ring, discard, sizeof(discard));
			ring_buf_get(&router->uart4_rx_ring, discard, sizeof(discard));
		}
		break;
	}
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
	case ROUTER_MODE_BYPASS:     return "BYPASS";
	case ROUTER_MODE_INVENTORY:  return "INVENTORY";
	default:                     return "UNKNOWN";
	}
}

int uart_router_send_uart1(uart_router_t *router, const uint8_t *data, size_t len)
{
	if (!router->uart1_ready) {
		return -ENODEV;
	}

	/* Check if host is connected before sending */
	if (!router->host_connected) {
		/* Silently discard data when host is not connected */
		return 0;
	}

	/* Queue data in TX ring buffer */
	int put = ring_buf_put(&router->uart1_tx_ring, data, len);
	if (put > 0) {
		/* Enable TX interrupt to start transmission */
		uart_irq_tx_enable(router->uart1);
	}

	if (put < (int)len) {
		router->stats.tx_errors++;
		LOG_WRN("UART1 TX buffer full: lost %zu bytes", len - put);
	}

	return put;
}

int uart_router_send_uart4(uart_router_t *router, const uint8_t *data, size_t len)
{
	if (!router->uart4_ready) {
		return -ENODEV;
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
 * USB Host Connection Detection (DTR)
 * ======================================================================== */

bool uart_router_check_host_connection(uart_router_t *router)
{
	if (!router->uart1_ready) {
		return false;
	}

	uint32_t dtr = 0;
	int ret = uart_line_ctrl_get(router->uart1, UART_LINE_CTRL_DTR, &dtr);

	bool was_connected = router->host_connected;
	router->host_connected = (ret == 0 && dtr != 0);

	/* Log state changes */
	if (router->host_connected && !was_connected) {
		LOG_INF("USB host connected (DTR high)");
	} else if (!router->host_connected && was_connected) {
		LOG_INF("USB host disconnected (DTR low)");
		/* Safely clear TX buffer when host disconnects */
		uart_irq_tx_disable(router->uart1);
		ring_buf_reset(&router->uart1_tx_ring);
	}

	return router->host_connected;
}

bool uart_router_is_host_connected(uart_router_t *router)
{
	return router->host_connected;
}

int uart_router_wait_host_connection(uart_router_t *router, int32_t timeout_ms)
{
	if (timeout_ms == 0) {
		/* No wait, just check current state */
		uart_router_check_host_connection(router);
		return router->host_connected ? 0 : -ETIMEDOUT;
	}

	int64_t start = k_uptime_get();
	int64_t deadline = (timeout_ms < 0) ? INT64_MAX : start + timeout_ms;

	while (k_uptime_get() < deadline) {
		if (uart_router_check_host_connection(router)) {
			return 0;
		}
		k_msleep(100);
	}

	return -ETIMEDOUT;
}

/* ========================================================================
 * E310 RFID Control API
 * ======================================================================== */

int uart_router_start_inventory(uart_router_t *router)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	/* Phase 3.2: Clear UART4 RX buffer before starting */
	safe_uart4_rx_reset(router);

	/* Build Start Fast Inventory command */
	int len = e310_build_start_fast_inventory(&router->e310_ctx, E310_TARGET_A);
	if (len < 0) {
		LOG_ERR("Failed to build start inventory command: %d", len);
		return len;
	}

	/* Send command to E310 via UART4 */
	int ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		LOG_ERR("Failed to send start inventory command: %d", ret);
		return ret;
	}

	/* Set mode to INVENTORY */
	uart_router_set_mode(router, ROUTER_MODE_INVENTORY);
	router->inventory_active = true;

	LOG_INF("E310 Fast Inventory started");
	return 0;
}

int uart_router_stop_inventory(uart_router_t *router)
{
	if (!router->uart4_ready) {
		LOG_ERR("UART4 not ready");
		return -ENODEV;
	}

	/* Build Stop Fast Inventory command */
	int len = e310_build_stop_fast_inventory(&router->e310_ctx);
	if (len < 0) {
		LOG_ERR("Failed to build stop inventory command: %d", len);
		return len;
	}

	/* Send command to E310 via UART4 */
	int ret = uart_router_send_uart4(router, router->e310_ctx.tx_buffer, len);
	if (ret < 0) {
		LOG_ERR("Failed to send stop inventory command: %d", ret);
		return ret;
	}

	router->inventory_active = false;

	/* Set mode to IDLE */
	uart_router_set_mode(router, ROUTER_MODE_IDLE);

	LOG_INF("E310 Fast Inventory stopped");
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

	/* Update host connection status */
	uart_router_check_host_connection(router);

	shell_print(sh, "=== UART Router Status ===");
	shell_print(sh, "Running: %s", router->running ? "yes" : "no");
	shell_print(sh, "Mode: %s", uart_router_get_mode_name(router->mode));
	shell_print(sh, "");
	shell_print(sh, "USB CDC ACM:");
	shell_print(sh, "  Device: %s", router->uart1->name);
	shell_print(sh, "  Host connected: %s", router->host_connected ? "YES" : "no");
	shell_print(sh, "  TX buffer: %u/%u bytes",
		    ring_buf_size_get(&router->uart1_tx_ring),
		    UART_ROUTER_BUF_SIZE);
	shell_print(sh, "  RX buffer: %u/%u bytes",
		    ring_buf_size_get(&router->uart1_rx_ring),
		    UART_ROUTER_BUF_SIZE);
	shell_print(sh, "");
	shell_print(sh, "UART4 (E310):");
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
	shell_print(sh, "CDC ACM (UART1):");
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
		shell_print(sh, "Usage: router mode <idle|bypass|inventory>");
		return 0;
	}

	router_mode_t new_mode;
	if (strcmp(argv[1], "idle") == 0) {
		new_mode = ROUTER_MODE_IDLE;
	} else if (strcmp(argv[1], "bypass") == 0) {
		new_mode = ROUTER_MODE_BYPASS;
	} else if (strcmp(argv[1], "inventory") == 0) {
		new_mode = ROUTER_MODE_INVENTORY;
	} else {
		shell_error(sh, "Unknown mode: %s", argv[1]);
		shell_print(sh, "Valid modes: idle, bypass, inventory");
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

	shell_print(sh, "E310 Fast Inventory started");
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

	shell_print(sh, "E310 Fast Inventory stopped");
	return 0;
}

static int cmd_e310_power(const struct shell *sh, size_t argc, char **argv)
{
	if (!g_router_instance) {
		shell_error(sh, "Router not initialized");
		return -ENODEV;
	}

	if (argc < 2) {
		shell_print(sh, "Usage: e310 power <0-30>");
		shell_print(sh, "  Sets RF power in dBm");
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

	shell_print(sh, "RF power set to %d dBm", power);
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

	shell_print(sh, "Reader info requested (check log for response)");
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
	shell_print(sh, "Frames parsed: %u", g_router_instance->stats.frames_parsed);
	shell_print(sh, "Parse errors: %u", g_router_instance->stats.parse_errors);
	shell_print(sh, "EPC sent (HID): %u", g_router_instance->stats.epc_sent);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_e310,
	SHELL_CMD(start, NULL, "Start Fast Inventory", cmd_e310_start),
	SHELL_CMD(stop, NULL, "Stop Fast Inventory", cmd_e310_stop),
	SHELL_CMD(power, NULL, "Set RF power (0-30 dBm)", cmd_e310_power),
	SHELL_CMD(info, NULL, "Request reader information", cmd_e310_info),
	SHELL_CMD(status, NULL, "Show E310 status", cmd_e310_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(e310, &sub_e310, "E310 RFID module commands", NULL);

/* ========================================================================
 * HID Shell Commands
 * ======================================================================== */

static int cmd_hid_speed(const struct shell *sh, size_t argc, char **argv)
{
	if (argc < 2) {
		shell_print(sh, "Current typing speed: %u CPM",
		            usb_hid_get_typing_speed());
		shell_print(sh, "Usage: hid speed <100-1500>");
		shell_print(sh, "  Sets typing speed in CPM (step: 100)");
		return 0;
	}

	int speed = atoi(argv[1]);
	if (speed < HID_TYPING_SPEED_MIN || speed > HID_TYPING_SPEED_MAX) {
		shell_error(sh, "Invalid speed: %d (must be %d-%d)",
		            speed, HID_TYPING_SPEED_MIN, HID_TYPING_SPEED_MAX);
		return -EINVAL;
	}

	int ret = usb_hid_set_typing_speed((uint16_t)speed);
	if (ret < 0) {
		shell_error(sh, "Failed to set typing speed: %d", ret);
		return ret;
	}

	shell_print(sh, "Typing speed set to %u CPM", usb_hid_get_typing_speed());
	return 0;
}

static int cmd_hid_test(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	if (!usb_hid_is_ready()) {
		shell_error(sh, "HID device not ready");
		return -EAGAIN;
	}

	/* Test typing with sample EPC */
	const char *test_epc = "E2000019500B028217901823";
	shell_print(sh, "Sending test EPC: %s", test_epc);
	shell_print(sh, "Typing speed: %u CPM", usb_hid_get_typing_speed());

	int ret = usb_hid_send_epc((const uint8_t *)test_epc, strlen(test_epc));
	if (ret < 0) {
		shell_error(sh, "Failed to send test EPC: %d", ret);
		return ret;
	}

	shell_print(sh, "Test EPC sent successfully");
	return 0;
}

static int cmd_hid_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);

	uint16_t speed = usb_hid_get_typing_speed();
	shell_print(sh, "=== HID Keyboard Status ===");
	shell_print(sh, "Ready: %s", usb_hid_is_ready() ? "YES" : "no");
	shell_print(sh, "Typing speed: %u CPM", speed);
	shell_print(sh, "  (Delay per key: ~%u ms)", 30000 / speed);

	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(sub_hid,
	SHELL_CMD(speed, NULL, "Get/set typing speed (CPM)", cmd_hid_speed),
	SHELL_CMD(test, NULL, "Send test EPC via HID", cmd_hid_test),
	SHELL_CMD(status, NULL, "Show HID status", cmd_hid_status),
	SHELL_SUBCMD_SET_END
);

SHELL_CMD_REGISTER(hid, &sub_hid, "USB HID Keyboard commands", NULL);
