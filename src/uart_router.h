/**
 * @file uart_router.h
 * @brief UART Router for PARP-01 RFID System
 *
 * Provides transparent data routing between USB CDC ACM (PC) and UART4 (E310 RFID module)
 * with support for multiple operating modes.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef UART_ROUTER_H_
#define UART_ROUTER_H_

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/ring_buffer.h>
#include "e310_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup uart_router UART Router
 * @{
 */

/* ========================================================================
 * Constants
 * ======================================================================== */

/** UART ring buffer size (per UART, per direction) */
#define UART_ROUTER_BUF_SIZE        1024

/** Maximum processing time per iteration (ms) */
#define UART_ROUTER_PROCESS_TIMEOUT 10

/* ========================================================================
 * Operating Modes
 * ======================================================================== */

/**
 * @brief UART Router operating modes
 */
typedef enum {
	/** Idle - no routing active */
	ROUTER_MODE_IDLE = 0,

	/** Bypass mode - transparent CDC ACM ↔ UART4 bridging */
	ROUTER_MODE_BYPASS,

	/** Inventory mode - UART4 → E310 Parser → USB HID Keyboard */
	ROUTER_MODE_INVENTORY,
} router_mode_t;

/* ========================================================================
 * Data Structures
 * ======================================================================== */

/**
 * @brief UART Router statistics
 */
typedef struct {
	uint32_t uart1_rx_bytes;    /**< Bytes received from CDC ACM/UART1 */
	uint32_t uart1_tx_bytes;    /**< Bytes sent to CDC ACM/UART1 */
	uint32_t uart4_rx_bytes;    /**< Bytes received from UART4 */
	uint32_t uart4_tx_bytes;    /**< Bytes sent to UART4 */
	uint32_t rx_overruns;       /**< RX buffer overrun count */
	uint32_t tx_errors;         /**< TX error count */
	uint32_t frames_parsed;     /**< E310 frames successfully parsed */
	uint32_t parse_errors;      /**< E310 parse error count */
	uint32_t epc_sent;          /**< EPC tags sent via HID */
} uart_router_stats_t;

/**
 * @brief UART Router context
 */
typedef struct {
	/* UART devices */
	const struct device *uart1;  /**< CDC ACM or USART1 device (PC) */
	const struct device *uart4;  /**< UART4 device (E310 module) */

	/* Operating mode */
	router_mode_t mode;          /**< Current operating mode */
	struct k_mutex mode_lock;    /**< Mode change protection */

	/* Ring buffers for data routing */
	struct ring_buf uart1_rx_ring;  /**< UART1 RX ring buffer */
	struct ring_buf uart4_rx_ring;  /**< UART4 RX ring buffer */
	uint8_t uart1_rx_buf[UART_ROUTER_BUF_SIZE];  /**< UART1 RX buffer storage */
	uint8_t uart4_rx_buf[UART_ROUTER_BUF_SIZE];  /**< UART4 RX buffer storage */

	/* E310 protocol context (for inventory mode) */
	e310_context_t e310_ctx;     /**< E310 protocol handler */

	/* Statistics */
	uart_router_stats_t stats;   /**< Router statistics */

	/* State flags */
	bool running;                /**< Router is running */
	bool uart1_ready;            /**< UART1 is ready */
	bool uart4_ready;            /**< UART4 is ready */

} uart_router_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize UART router
 *
 * Sets up UART devices, ring buffers, and internal state.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_init(uart_router_t *router);

/**
 * @brief Start UART router operation
 *
 * Enables UART interrupts and begins routing.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_start(uart_router_t *router);

/**
 * @brief Stop UART router operation
 *
 * Disables UART interrupts and stops routing.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_stop(uart_router_t *router);

/**
 * @brief Set router operating mode
 *
 * Changes the router operating mode. Thread-safe.
 *
 * @param router Pointer to router context
 * @param mode New operating mode
 * @return 0 on success, negative errno on error
 */
int uart_router_set_mode(uart_router_t *router, router_mode_t mode);

/**
 * @brief Get current operating mode
 *
 * @param router Pointer to router context
 * @return Current operating mode
 */
router_mode_t uart_router_get_mode(uart_router_t *router);

/**
 * @brief Process pending UART data
 *
 * Should be called periodically from main loop or dedicated thread.
 * Handles data forwarding based on current mode.
 *
 * @param router Pointer to router context
 */
void uart_router_process(uart_router_t *router);

/**
 * @brief Get router statistics
 *
 * @param router Pointer to router context
 * @param stats Output: statistics structure
 */
void uart_router_get_stats(uart_router_t *router, uart_router_stats_t *stats);

/**
 * @brief Reset router statistics
 *
 * @param router Pointer to router context
 */
void uart_router_reset_stats(uart_router_t *router);

/**
 * @brief Send data to UART1 (PC)
 *
 * Queues data for transmission to UART1.
 *
 * @param router Pointer to router context
 * @param data Data buffer
 * @param len Data length
 * @return Number of bytes queued, or negative errno on error
 */
int uart_router_send_uart1(uart_router_t *router, const uint8_t *data, size_t len);

/**
 * @brief Send data to UART4 (E310)
 *
 * Queues data for transmission to UART4.
 *
 * @param router Pointer to router context
 * @param data Data buffer
 * @param len Data length
 * @return Number of bytes queued, or negative errno on error
 */
int uart_router_send_uart4(uart_router_t *router, const uint8_t *data, size_t len);

/**
 * @brief Get mode name string
 *
 * @param mode Operating mode
 * @return Human-readable mode name
 */
const char *uart_router_get_mode_name(router_mode_t mode);

/** @} */ /* End of uart_router group */

#ifdef __cplusplus
}
#endif

#endif /* UART_ROUTER_H_ */
