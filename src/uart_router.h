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
 * Frame Assembler (for E310 protocol frames)
 * ======================================================================== */

/** Frame assembler state */
typedef enum {
	FRAME_STATE_WAIT_LEN,    /**< Waiting for first byte (length) */
	FRAME_STATE_RECEIVING,   /**< Receiving frame data */
	FRAME_STATE_COMPLETE,    /**< Frame complete */
} frame_state_t;

/**
 * @brief Frame assembler for E310 protocol
 *
 * Assembles bytes from UART into complete E310 frames.
 */
typedef struct {
	uint8_t buffer[E310_MAX_FRAME_SIZE]; /**< Frame buffer */
	size_t  received;                     /**< Bytes received so far */
	size_t  expected;                     /**< Expected frame length */
	frame_state_t state;                  /**< Current state */
	int64_t last_byte_time;              /**< Timestamp of last byte (for timeout) */
} frame_assembler_t;

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

	/* Ring buffers for data routing (RX and TX) */
	struct ring_buf uart1_rx_ring;  /**< UART1 RX ring buffer */
	struct ring_buf uart1_tx_ring;  /**< UART1 TX ring buffer */
	struct ring_buf uart4_rx_ring;  /**< UART4 RX ring buffer */
	struct ring_buf uart4_tx_ring;  /**< UART4 TX ring buffer */
	uint8_t uart1_rx_buf[UART_ROUTER_BUF_SIZE];  /**< UART1 RX buffer storage */
	uint8_t uart1_tx_buf[UART_ROUTER_BUF_SIZE];  /**< UART1 TX buffer storage */
	uint8_t uart4_rx_buf[UART_ROUTER_BUF_SIZE];  /**< UART4 RX buffer storage */
	uint8_t uart4_tx_buf[UART_ROUTER_BUF_SIZE];  /**< UART4 TX buffer storage */

	/* E310 protocol context (for inventory mode) */
	e310_context_t e310_ctx;     /**< E310 protocol handler */
	frame_assembler_t e310_frame; /**< E310 frame assembler */

	/* Statistics */
	uart_router_stats_t stats;   /**< Router statistics */

	/* State flags */
	bool running;                /**< Router is running */
	bool uart1_ready;            /**< UART1 device is ready */
	bool uart4_ready;            /**< UART4 device is ready */
	bool host_connected;         /**< USB host has opened port (DTR high) */
	bool inventory_active;       /**< E310 inventory is running */

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

/**
 * @brief Check and update USB host connection status
 *
 * Checks the DTR (Data Terminal Ready) line control signal to determine
 * if the USB host has opened the CDC ACM port.
 *
 * @param router Pointer to router context
 * @return true if host is connected (DTR high), false otherwise
 */
bool uart_router_check_host_connection(uart_router_t *router);

/**
 * @brief Check if USB host is connected
 *
 * @param router Pointer to router context
 * @return true if host is connected, false otherwise
 */
bool uart_router_is_host_connected(uart_router_t *router);

/**
 * @brief Wait for USB host connection with timeout
 *
 * Blocks until the USB host connects or timeout expires.
 *
 * @param router Pointer to router context
 * @param timeout_ms Timeout in milliseconds (0 = no wait, -1 = forever)
 * @return 0 if connected, -ETIMEDOUT if timeout
 */
int uart_router_wait_host_connection(uart_router_t *router, int32_t timeout_ms);

/* ========================================================================
 * E310 RFID Control API
 * ======================================================================== */

/**
 * @brief Start E310 Fast Inventory
 *
 * Sends Start Fast Inventory command to E310 and sets mode to INVENTORY.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_start_inventory(uart_router_t *router);

/**
 * @brief Stop E310 Fast Inventory
 *
 * Sends Stop Fast Inventory command to E310 and sets mode to IDLE.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_stop_inventory(uart_router_t *router);

/**
 * @brief Set E310 RF Power
 *
 * @param router Pointer to router context
 * @param power RF power level (0-30 dBm)
 * @return 0 on success, negative errno on error
 */
int uart_router_set_rf_power(uart_router_t *router, uint8_t power);

/**
 * @brief Request E310 Reader Information
 *
 * Sends Obtain Reader Info command to E310.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_get_reader_info(uart_router_t *router);

/**
 * @brief Check if inventory is active
 *
 * @param router Pointer to router context
 * @return true if inventory is running, false otherwise
 */
bool uart_router_is_inventory_active(uart_router_t *router);

/** @} */ /* End of uart_router group */

#ifdef __cplusplus
}
#endif

#endif /* UART_ROUTER_H_ */
