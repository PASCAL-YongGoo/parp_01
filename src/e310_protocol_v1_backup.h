/**
 * @file e310_protocol.h
 * @brief Impinj E310 RFID Reader Protocol Library
 *
 * This library implements the protocol for communicating with the Impinj E310
 * RFID reader module. Based on UHFEx10 User Manual V2.20.
 *
 * @copyright Copyright (c) 2026 PARP
 */

#ifndef E310_PROTOCOL_H_
#define E310_PROTOCOL_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @defgroup e310_protocol E310 RFID Protocol
 * @{
 */

/* ========================================================================
 * Protocol Constants
 * ======================================================================== */

/** Maximum frame length (including overhead) */
#define E310_MAX_FRAME_SIZE         256

/** Maximum EPC data length */
#define E310_MAX_EPC_LENGTH         62

/** Maximum TID data length */
#define E310_MAX_TID_LENGTH         32

/** Maximum mask data length */
#define E310_MAX_MASK_LENGTH        64

/** CRC-16 length */
#define E310_CRC16_LENGTH           2

/** Broadcast address */
#define E310_ADDR_BROADCAST         0xFF

/** Default reader address */
#define E310_ADDR_DEFAULT           0x00

/* ========================================================================
 * Command Codes
 * ======================================================================== */

/** @defgroup e310_commands Command Codes
 * @{
 */

/** Tag Inventory - Single inventory operation */
#define E310_CMD_TAG_INVENTORY              0x01

/** Read Tag Data */
#define E310_CMD_READ_DATA                  0x02

/** Write Tag Data */
#define E310_CMD_WRITE_DATA                 0x03

/** Write EPC */
#define E310_CMD_WRITE_EPC                  0x04

/** Kill Tag */
#define E310_CMD_KILL_TAG                   0x05

/** Set Protection */
#define E310_CMD_SET_PROTECTION             0x06

/** Block Erase */
#define E310_CMD_BLOCK_ERASE                0x07

/** Single Tag Inventory */
#define E310_CMD_SINGLE_TAG_INVENTORY       0x0F

/** Inventory with Memory Buffer */
#define E310_CMD_INVENTORY_MEM_BUFFER       0x18

/** Mix Inventory */
#define E310_CMD_MIX_INVENTORY              0x19

/** Obtain Reader Information */
#define E310_CMD_OBTAIN_READER_INFO         0x21

/** Modify Frequency */
#define E310_CMD_MODIFY_FREQUENCY           0x22

/** Modify Reader Address */
#define E310_CMD_MODIFY_READER_ADDR         0x24

/** Modify Inventory Time */
#define E310_CMD_MODIFY_INVENTORY_TIME      0x25

/** Modify Baud Rate */
#define E310_CMD_MODIFY_BAUD_RATE           0x28

/** Modify RF Power */
#define E310_CMD_MODIFY_RF_POWER            0x2F

/** LED/Buzzer Control */
#define E310_CMD_LED_BUZZER_CONTROL         0x33

/** Setup Antenna Mux */
#define E310_CMD_SETUP_ANTENNA_MUX          0x3F

/** Enable/Disable Buzzer */
#define E310_CMD_ENABLE_DISABLE_BUZZER      0x40

/** GPIO Control */
#define E310_CMD_GPIO_CONTROL               0x46

/** Obtain GPIO State */
#define E310_CMD_OBTAIN_GPIO_STATE          0x47

/** Obtain Reader SN */
#define E310_CMD_OBTAIN_READER_SN           0x4C

/** Start Fast Inventory - Continuous inventory mode */
#define E310_CMD_START_FAST_INVENTORY       0x50

/** Stop Fast Inventory */
#define E310_CMD_STOP_FAST_INVENTORY        0x51

/** Get Data from Buffer */
#define E310_CMD_GET_DATA_FROM_BUFFER       0x72

/** Clear Memory Buffer */
#define E310_CMD_CLEAR_MEMORY_BUFFER        0x73

/** Get Tag Count from Buffer */
#define E310_CMD_GET_TAG_COUNT_FROM_BUFFER  0x74

/** Stop Immediately */
#define E310_CMD_STOP_IMMEDIATELY           0x93

/** @} */

/* ========================================================================
 * Response Codes
 * ======================================================================== */

/** @defgroup e310_status Status Codes
 * @{
 */

/** Success / Command completed */
#define E310_STATUS_SUCCESS                 0x00

/** Operation completed successfully */
#define E310_STATUS_OPERATION_COMPLETE      0x01

/** Inventory timeout - no tags found within scan time */
#define E310_STATUS_INVENTORY_TIMEOUT       0x02

/** More data to transmit - additional frames will follow */
#define E310_STATUS_MORE_DATA               0x03

/** Reader memory buffer full */
#define E310_STATUS_MEMORY_FULL             0x04

/** Statistics data packet (used in inventory) */
#define E310_STATUS_STATISTICS_DATA         0x26

/** Antenna connection error */
#define E310_STATUS_ANTENNA_ERROR           0xF8

/** Invalid frame length */
#define E310_STATUS_INVALID_LENGTH          0xFD

/** Invalid command or CRC error */
#define E310_STATUS_INVALID_COMMAND_CRC     0xFE

/** Unrecognized parameter */
#define E310_STATUS_UNKNOWN_PARAMETER       0xFF

/** @} */

/* ========================================================================
 * Auto-upload Response Code (Fast Inventory)
 * ======================================================================== */

/** Auto-uploaded tag data (used in fast inventory mode) */
#define E310_RECMD_AUTO_UPLOAD              0xEE

/* ========================================================================
 * Session Values
 * ======================================================================== */

/** @defgroup e310_session EPC Session Values
 * @{
 */

#define E310_SESSION_S0                     0x00
#define E310_SESSION_S1                     0x01
#define E310_SESSION_S2                     0x02
#define E310_SESSION_S3                     0x03
#define E310_SESSION_SMART                  0xFF

/** @} */

/* ========================================================================
 * Memory Bank Selection
 * ======================================================================== */

/** @defgroup e310_membank Memory Banks
 * @{
 */

#define E310_MEMBANK_RESERVED               0x00
#define E310_MEMBANK_EPC                    0x01
#define E310_MEMBANK_TID                    0x02
#define E310_MEMBANK_USER                   0x03

/** @} */

/* ========================================================================
 * Target Values
 * ======================================================================== */

/** @defgroup e310_target Inventory Target
 * @{
 */

#define E310_TARGET_A                       0x00
#define E310_TARGET_B                       0x01

/** @} */

/* ========================================================================
 * Antenna Selection
 * ======================================================================== */

/** @defgroup e310_antenna Antenna Selection
 * @{
 */

#define E310_ANT_1                          0x80
#define E310_ANT_2                          0x81
#define E310_ANT_3                          0x82
#define E310_ANT_4                          0x83
#define E310_ANT_5                          0x84
#define E310_ANT_6                          0x85
#define E310_ANT_7                          0x86
#define E310_ANT_8                          0x87
#define E310_ANT_9                          0x88
#define E310_ANT_10                         0x89
#define E310_ANT_11                         0x8A
#define E310_ANT_12                         0x8B
#define E310_ANT_13                         0x8C
#define E310_ANT_14                         0x8D
#define E310_ANT_15                         0x8E
#define E310_ANT_16                         0x8F

/** @} */

/* ========================================================================
 * QValue Flags (bit field in QValue parameter)
 * ======================================================================== */

/** @defgroup e310_qvalue_flags QValue Flags
 * @{
 */

/** Statistics data packet flag (bit 7) */
#define E310_QVALUE_FLAG_STATISTICS         (1 << 7)

/** Strategy indicator (bit 6): 0=normal, 1=special */
#define E310_QVALUE_FLAG_STRATEGY           (1 << 6)

/** FastID inventory indicator (bit 5) */
#define E310_QVALUE_FLAG_FASTID             (1 << 5)

/** Phase information (bit 4) */
#define E310_QVALUE_FLAG_PHASE              (1 << 4)

/** Q-value mask (bits 3:0) - range 0-15 */
#define E310_QVALUE_MASK                    0x0F

/** @} */

/* ========================================================================
 * Data Structures
 * ======================================================================== */

/**
 * @brief E310 frame header structure
 */
typedef struct {
	uint8_t len;        /**< Frame length (excluding CRC) */
	uint8_t addr;       /**< Reader address */
	uint8_t cmd;        /**< Command code */
} __attribute__((packed)) e310_frame_header_t;

/**
 * @brief E310 response header structure
 */
typedef struct {
	uint8_t len;        /**< Frame length */
	uint8_t addr;       /**< Reader address */
	uint8_t recmd;      /**< Response command code */
	uint8_t status;     /**< Status code */
} __attribute__((packed)) e310_response_header_t;

/**
 * @brief EPC tag data structure
 */
typedef struct {
	uint8_t epc[E310_MAX_EPC_LENGTH];   /**< EPC data */
	uint8_t epc_len;                     /**< EPC length in bytes */
	uint8_t tid[E310_MAX_TID_LENGTH];   /**< TID data (optional) */
	uint8_t tid_len;                     /**< TID length in bytes */
	uint8_t rssi;                        /**< Signal strength */
	uint8_t antenna;                     /**< Antenna number */
	uint32_t phase;                      /**< Phase information (if enabled) */
	uint32_t frequency_khz;              /**< Frequency in kHz (if enabled) */
	bool has_tid;                        /**< TID data present */
	bool has_phase;                      /**< Phase data present */
	bool has_frequency;                  /**< Frequency data present */
} e310_tag_data_t;

/**
 * @brief Inventory statistics structure
 */
typedef struct {
	uint8_t antenna;        /**< Antenna that read the tags */
	uint16_t read_rate;     /**< Tags per second */
	uint32_t total_count;   /**< Total tags detected */
} e310_inventory_stats_t;

/**
 * @brief Reader information structure
 */
typedef struct {
	uint16_t firmware_version;  /**< Firmware version (major.minor) */
	uint8_t model_type;         /**< Model code */
	uint8_t protocol_type;      /**< Supported protocols (bitmask) */
	uint8_t max_freq;           /**< Maximum frequency point */
	uint8_t min_freq;           /**< Minimum frequency point */
	uint8_t power;              /**< RF power (0-30) */
	uint8_t scan_time;          /**< Inventory time */
	uint8_t antenna;            /**< Antenna setting */
	uint8_t check_antenna;      /**< Antenna check setting */
} e310_reader_info_t;

/**
 * @brief Inventory parameters for 0x01 command
 */
typedef struct {
	uint8_t q_value;            /**< Q-value and flags (combined) */
	uint8_t session;            /**< Session (S0-S3 or Smart) */
	uint8_t mask_mem;           /**< Mask memory bank */
	uint16_t mask_addr;         /**< Mask start bit address */
	uint8_t mask_len;           /**< Mask bit length */
	uint8_t mask_data[E310_MAX_MASK_LENGTH]; /**< Mask data */
	uint8_t tid_addr;           /**< TID start address */
	uint8_t tid_len;            /**< TID length (0-15) */
	uint8_t target;             /**< Target (A or B) */
	uint8_t antenna;            /**< Antenna selection */
	uint8_t scan_time;          /**< Scan time (* 100ms) */
} e310_inventory_params_t;

/**
 * @brief E310 protocol context
 */
typedef struct {
	uint8_t reader_addr;                    /**< Current reader address */
	uint8_t tx_buffer[E310_MAX_FRAME_SIZE]; /**< Transmit buffer */
	uint8_t rx_buffer[E310_MAX_FRAME_SIZE]; /**< Receive buffer */
	size_t tx_len;                          /**< TX buffer length */
	size_t rx_len;                          /**< RX buffer length */
} e310_context_t;

/* ========================================================================
 * API Functions
 * ======================================================================== */

/**
 * @brief Initialize E310 protocol context
 *
 * @param ctx Pointer to context structure
 * @param reader_addr Reader address (0x00-0xFE, or 0xFF for broadcast)
 */
void e310_init(e310_context_t *ctx, uint8_t reader_addr);

/**
 * @brief Calculate CRC-16 for E310 protocol
 *
 * Uses CRC-16-CCITT-FALSE algorithm with polynomial 0x8408 and preset 0xFFFF.
 *
 * @param data Pointer to data buffer
 * @param length Length of data
 * @return CRC-16 value
 */
uint16_t e310_crc16(const uint8_t *data, size_t length);

/**
 * @brief Verify CRC-16 of received frame
 *
 * @param frame Pointer to complete frame (including CRC at end)
 * @param length Total frame length
 * @return true if CRC is valid, false otherwise
 */
bool e310_verify_crc(const uint8_t *frame, size_t length);

/**
 * @brief Build command frame header
 *
 * @param ctx Context
 * @param cmd Command code
 * @param data_len Length of data payload
 * @return Total frame length (header + data + CRC)
 */
size_t e310_build_frame_header(e310_context_t *ctx, uint8_t cmd, size_t data_len);

/**
 * @brief Finalize frame by adding CRC
 *
 * @param ctx Context
 * @param frame_len Current frame length (without CRC)
 * @return Total length including CRC
 */
size_t e310_finalize_frame(e310_context_t *ctx, size_t frame_len);

/**
 * @brief Parse response frame header
 *
 * @param frame Received frame data
 * @param length Frame length
 * @param header Output: parsed header
 * @return 0 on success, negative error code otherwise
 */
int e310_parse_response_header(const uint8_t *frame, size_t length,
                                 e310_response_header_t *header);

/* ========================================================================
 * Command Builders - High Priority Commands
 * ======================================================================== */

/**
 * @brief Build "Start Fast Inventory" command (0x50)
 *
 * This starts continuous inventory mode where the reader automatically
 * sends tag data as it's read.
 *
 * @param ctx Context
 * @param target Target (A or B)
 * @return Frame length ready to transmit
 */
size_t e310_build_start_fast_inventory(e310_context_t *ctx, uint8_t target);

/**
 * @brief Build "Stop Fast Inventory" command (0x51)
 *
 * @param ctx Context
 * @return Frame length ready to transmit
 */
size_t e310_build_stop_fast_inventory(e310_context_t *ctx);

/**
 * @brief Build "Tag Inventory" command (0x01)
 *
 * Single inventory operation with detailed parameters.
 *
 * @param ctx Context
 * @param params Inventory parameters
 * @return Frame length ready to transmit
 */
size_t e310_build_tag_inventory(e310_context_t *ctx,
                                 const e310_inventory_params_t *params);

/**
 * @brief Build "Obtain Reader Information" command (0x21)
 *
 * @param ctx Context
 * @return Frame length ready to transmit
 */
size_t e310_build_obtain_reader_info(e310_context_t *ctx);

/**
 * @brief Build "Stop Immediately" command (0x93)
 *
 * @param ctx Context
 * @return Frame length ready to transmit
 */
size_t e310_build_stop_immediately(e310_context_t *ctx);

/* ========================================================================
 * Response Parsers
 * ======================================================================== */

/**
 * @brief Parse EPC tag data from inventory response
 *
 * @param data Pointer to EPC ID block in response
 * @param length Length of EPC ID block
 * @param tag Output: parsed tag data
 * @return Number of bytes consumed, or negative error code
 */
int e310_parse_tag_data(const uint8_t *data, size_t length, e310_tag_data_t *tag);

/**
 * @brief Parse inventory statistics response
 *
 * @param data Response data field
 * @param length Data length
 * @param stats Output: statistics data
 * @return 0 on success, negative error code otherwise
 */
int e310_parse_inventory_stats(const uint8_t *data, size_t length,
                                 e310_inventory_stats_t *stats);

/**
 * @brief Parse reader information response
 *
 * @param data Response data field
 * @param length Data length
 * @param info Output: reader information
 * @return 0 on success, negative error code otherwise
 */
int e310_parse_reader_info(const uint8_t *data, size_t length,
                             e310_reader_info_t *info);

/**
 * @brief Parse auto-upload tag data (fast inventory mode, reCmd=0xEE)
 *
 * @param data Response data field
 * @param length Data length
 * @param tag Output: tag data
 * @return 0 on success, negative error code otherwise
 */
int e310_parse_auto_upload_tag(const uint8_t *data, size_t length,
                                 e310_tag_data_t *tag);

/* ========================================================================
 * Utility Functions
 * ======================================================================== */

/**
 * @brief Get command name string
 *
 * @param cmd Command code
 * @return Human-readable command name
 */
const char *e310_get_command_name(uint8_t cmd);

/**
 * @brief Get status description string
 *
 * @param status Status code
 * @return Human-readable status description
 */
const char *e310_get_status_desc(uint8_t status);

/**
 * @brief Format EPC data as hex string
 *
 * @param epc EPC data buffer
 * @param epc_len EPC length in bytes
 * @param output Output string buffer
 * @param output_size Size of output buffer
 * @return Number of characters written (excluding null terminator)
 */
int e310_format_epc_string(const uint8_t *epc, uint8_t epc_len,
                            char *output, size_t output_size);

/** @} */ /* End of e310_protocol group */

#ifdef __cplusplus
}
#endif

#endif /* E310_PROTOCOL_H_ */
