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
#define UART_ROUTER_BUF_SIZE        4096

/** Maximum processing time per iteration (ms) */
#define UART_ROUTER_PROCESS_TIMEOUT 10

/** Maximum number of cached EPCs for duplicate filtering */
#define EPC_CACHE_SIZE              32

/** Default EPC debounce time in seconds */
#define EPC_DEBOUNCE_DEFAULT_SEC    3

/* ========================================================================
 * EPC Filter (for duplicate detection)
 * ======================================================================== */

/**
 * @brief EPC cache entry for duplicate detection
 */
typedef struct {
	uint8_t epc[E310_MAX_EPC_LENGTH]; /**< EPC data */
	uint8_t epc_len;                   /**< EPC length */
	int64_t timestamp;                 /**< Last sent timestamp (ms) */
} epc_cache_entry_t;

/**
 * @brief EPC filter context for duplicate detection
 */
typedef struct {
	epc_cache_entry_t entries[EPC_CACHE_SIZE]; /**< Cache entries */
	uint8_t count;                              /**< Current cached count */
	uint8_t next_idx;                           /**< Next insertion index (circular) */
	uint32_t debounce_ms;                       /**< Debounce time in ms */
} epc_filter_t;

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
	const struct device *uart4;  /**< UART4 device (E310 module) */

	/* Operating mode */
	router_mode_t mode;          /**< Current operating mode */
	struct k_mutex mode_lock;    /**< Mode change protection */

	/* Ring buffers for UART4 data */
	struct ring_buf uart4_rx_ring;  /**< UART4 RX ring buffer */
	struct ring_buf uart4_tx_ring;  /**< UART4 TX ring buffer */
	uint8_t uart4_rx_buf[UART_ROUTER_BUF_SIZE];  /**< UART4 RX buffer storage */
	uint8_t uart4_tx_buf[UART_ROUTER_BUF_SIZE];  /**< UART4 TX buffer storage */

	/* E310 protocol context (for inventory mode) */
	e310_context_t e310_ctx;     /**< E310 protocol handler */
	frame_assembler_t e310_frame; /**< E310 frame assembler */

	/* EPC duplicate filter */
	epc_filter_t epc_filter;     /**< EPC duplicate detection */

	/* Statistics */
	uart_router_stats_t stats;   /**< Router statistics */

	/* State flags */
	bool running;                /**< Router is running */
	bool uart4_ready;            /**< UART4 device is ready */
	bool inventory_active;       /**< E310 inventory is running */
	bool e310_connected;         /**< E310 connection sequence completed */

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

/* ========================================================================
 * E310 RFID Control API
 * ======================================================================== */

/**
 * @brief Connect to E310 (initialization sequence)
 *
 * Sends the connection/initialization sequence to E310:
 * 1. Get Reader Info (broadcast 0xFF)
 * 2. Get Reader Info (address 0x00)
 * 3. Stop Inventory
 * 4. Set Work Mode (0x00)
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_connect_e310(uart_router_t *router);

/**
 * @brief Start E310 Tag Inventory
 *
 * Sends Tag Inventory command to E310 and sets mode to INVENTORY.
 *
 * @param router Pointer to router context
 * @return 0 on success, negative errno on error
 */
int uart_router_start_inventory(uart_router_t *router);

/**
 * @brief Stop E310 Tag Inventory
 *
 * Sends Stop Inventory command to E310 and sets mode to IDLE.
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
