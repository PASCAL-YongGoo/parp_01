/**
 * @file uart_router.c
 * @brief UART Router Implementation
 *
 * @copyright Copyright (c) 2026 PARP
 */

#include "uart_router.h"
#include "usb_hid.h"
#include <zephyr/logging/log.h>
#include <string.h>

LOG_MODULE_REGISTER(uart_router, LOG_LEVEL_DBG);

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
				LOG_WRN("UART1 RX buffer overrun: lost %d bytes", len - put);
			}
		}
	}

	/* Handle TX */
	if (uart_irq_tx_ready(dev)) {
		uint8_t buf[64];
		int len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));

		if (len > 0) {
			int sent = uart_fifo_fill(dev, buf, len);
			router->stats.uart1_tx_bytes += sent;

			if (sent < len) {
				/* Put back unsent data */
				ring_buf_put(&router->uart1_rx_ring, &buf[sent], len - sent);
			}
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

			if (put < len) {
				router->stats.rx_overruns++;
				LOG_WRN("UART4 RX buffer overrun: lost %d bytes", len - put);
			}
		}
	}

	/* Handle TX */
	if (uart_irq_tx_ready(dev)) {
		uint8_t buf[64];
		int len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));

		if (len > 0) {
			int sent = uart_fifo_fill(dev, buf, len);
			router->stats.uart4_tx_bytes += sent;

			if (sent < len) {
				/* Put back unsent data */
				ring_buf_put(&router->uart4_rx_ring, &buf[sent], len - sent);
			}
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

	/* Initialize ring buffers */
	ring_buf_init(&router->uart1_rx_ring, sizeof(router->uart1_rx_buf),
	              router->uart1_rx_buf);
	ring_buf_init(&router->uart4_rx_ring, sizeof(router->uart4_rx_buf),
	              router->uart4_rx_buf);

	/* Initialize E310 protocol context */
	e310_init(&router->e310_ctx, E310_ADDR_DEFAULT);

	/* Set initial mode */
	router->mode = ROUTER_MODE_IDLE;
	router->uart1_ready = true;
	router->uart4_ready = true;

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
		LOG_INF("Mode changed: %s → %s",
		        uart_router_get_mode_name(old_mode),
		        uart_router_get_mode_name(mode));

		/* Clear ring buffers on mode change */
		ring_buf_reset(&router->uart1_rx_ring);
		ring_buf_reset(&router->uart4_rx_ring);
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
 * @brief Process bypass mode (transparent UART1 ↔ UART4 bridging)
 */
static void process_bypass_mode(uart_router_t *router)
{
	uint8_t buf[128];
	int len;

	/* Forward UART1 → UART4 */
	len = ring_buf_get(&router->uart1_rx_ring, buf, sizeof(buf));
	if (len > 0) {
		/* Send to UART4 */
		for (int i = 0; i < len; i++) {
			uart_poll_out(router->uart4, buf[i]);
		}
		router->stats.uart4_tx_bytes += len;
	}

	/* Forward UART4 → UART1 */
	len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));
	if (len > 0) {
		/* Send to UART1 */
		for (int i = 0; i < len; i++) {
			uart_poll_out(router->uart1, buf[i]);
		}
		router->stats.uart1_tx_bytes += len;
	}
}

/**
 * @brief Process inventory mode (UART4 → Parser → USB HID)
 */
static void process_inventory_mode(uart_router_t *router)
{
	uint8_t buf[256];
	int len;

	/* CDC ACM input is BLOCKED in inventory mode */
	ring_buf_reset(&router->uart1_rx_ring);

	/* Process UART4 data (E310 responses) */
	len = ring_buf_get(&router->uart4_rx_ring, buf, sizeof(buf));
	if (len > 0) {
		/* Parse E310 protocol frames */
		e310_response_header_t header;
		int ret = e310_parse_response_header(buf, len, &header);

		if (ret == E310_OK && header.recmd == E310_RECMD_AUTO_UPLOAD) {
			/* Parse auto-upload tag (Fast Inventory mode) */
			e310_tag_data_t tag;
			ret = e310_parse_auto_upload_tag(&buf[4], len - 6, &tag);

			if (ret == E310_OK) {
				/* Format EPC as hex string */
				char epc_str[128];
				e310_format_epc_string(tag.epc, tag.epc_len,
				                       epc_str, sizeof(epc_str));

				/* Send to USB HID Keyboard (uppercase automatically) */
				ret = usb_hid_send_epc((uint8_t *)epc_str, strlen(epc_str));
				if (ret == 0) {
					router->stats.epc_sent++;
					LOG_INF("EPC sent via HID: %s (RSSI: %u)",
					        epc_str, tag.rssi);
				} else {
					LOG_ERR("Failed to send EPC via HID: %d", ret);
					router->stats.tx_errors++;
				}

				router->stats.frames_parsed++;
			} else {
				LOG_WRN("Failed to parse auto-upload tag: %d", ret);
				router->stats.parse_errors++;
			}
		} else if (ret != E310_OK) {
			LOG_DBG("Frame parse error: %d", ret);
			router->stats.parse_errors++;
		}

		/* Echo raw data to CDC ACM for debugging */
		for (int i = 0; i < len; i++) {
			uart_poll_out(router->uart1, buf[i]);
		}
	}
}

void uart_router_process(uart_router_t *router)
{
	if (!router->running) {
		return;
	}

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
		/* Clear buffers in idle mode */
		ring_buf_reset(&router->uart1_rx_ring);
		ring_buf_reset(&router->uart4_rx_ring);
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

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(router->uart1, data[i]);
	}

	router->stats.uart1_tx_bytes += len;
	return len;
}

int uart_router_send_uart4(uart_router_t *router, const uint8_t *data, size_t len)
{
	if (!router->uart4_ready) {
		return -ENODEV;
	}

	for (size_t i = 0; i < len; i++) {
		uart_poll_out(router->uart4, data[i]);
	}

	router->stats.uart4_tx_bytes += len;
	return len;
}
