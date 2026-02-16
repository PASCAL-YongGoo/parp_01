/**
 * @file e310_protocol.h
 * @brief Impinj E310 RFID Reader Protocol Library (v2 - Bug Fixed)
 *
 * This library implements the protocol for communicating with the Impinj E310
 * RFID reader module. Based on UHFEx10 User Manual V2.20.
 *
 * **Version 2.0 Changes**:
 * - Fixed CRC documentation (confirmed: polynomial 0x8408, LSB-first, reflected)
 * - Added error code constants
 * - Added buffer overflow protection
 * - Fixed length check issues
 * - Improved EPC+TID parsing
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

/** Maximum mask data length (in bytes) */
#define E310_MAX_MASK_LENGTH        64

/** Maximum mask bit length */
#define E310_MAX_MASK_BIT_LENGTH    (E310_MAX_MASK_LENGTH * 8)

/** CRC-16 length */
#define E310_CRC16_LENGTH           2

/** Minimum response frame size (Len + Adr + reCmd + Status + CRC) */
#define E310_MIN_RESPONSE_SIZE      6

/** Broadcast address */
#define E310_ADDR_BROADCAST         0xFF

/** Default reader address (0xFF = broadcast, works with most E310 modules) */
#define E310_ADDR_DEFAULT           0x00

/* ========================================================================
 * Error Codes
 * ======================================================================== */

/** @defgroup e310_errors Error Codes
 * @{
 */

/** Success */
#define E310_OK                     0

/** Frame too short */
#define E310_ERR_FRAME_TOO_SHORT    -1

/** CRC verification failed */
#define E310_ERR_CRC_FAILED         -2

/** Length field mismatch */
#define E310_ERR_LENGTH_MISMATCH    -3

/** Buffer overflow (would exceed available space) */
#define E310_ERR_BUFFER_OVERFLOW    -4

/** Invalid parameter */
#define E310_ERR_INVALID_PARAM      -5

/** Missing required data */
#define E310_ERR_MISSING_DATA       -6

/** Parsing error */
#define E310_ERR_PARSE_ERROR        -7

/** @} */

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

/** Enable/Disable Antenna Check */
#define E310_CMD_ENABLE_ANTENNA_CHECK       0x66

/** Modify Antenna Return Loss Threshold */
#define E310_CMD_MODIFY_RETURN_LOSS         0x6E

/** Measure Antenna Return Loss */
#define E310_CMD_MEASURE_RETURN_LOSS        0x91

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

/** Set Work Mode / Initialize (used in connection sequence) */
#define E310_CMD_SET_WORK_MODE              0x7F

/** @} */

/* Baud rate indices for 0x28 command */
#define E310_BAUD_9600                      0
#define E310_BAUD_19200                     1
#define E310_BAUD_38400                     2
#define E310_BAUD_57600                     5
#define E310_BAUD_115200                    6

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

#define E310_ANT_NONE                       0x00  /* No antenna specified */
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
	uint8_t antenna;            /**< Antenna selection (use E310_ANT_NONE for none) */
	uint8_t scan_time;          /**< Scan time (* 100ms, 0 = no scan time) */
} e310_inventory_params_t;

/**
 * @brief Read Data parameters for 0x02 command
 */
typedef struct {
	uint8_t epc[E310_MAX_EPC_LENGTH];  /**< Target tag EPC */
	uint8_t epc_len;                    /**< EPC length in bytes (0 = use mask) */
	uint8_t mem_bank;                   /**< Memory bank (0x00-0x03) */
	uint8_t word_ptr;                   /**< Start word address */
	uint8_t word_count;                 /**< Number of words to read (1-120) */
	uint8_t password[4];                /**< Access password */
	/* Mask options (used when epc_len == 0 or 0xFF) */
	uint8_t mask_mem;                   /**< Mask memory bank */
	uint16_t mask_addr;                 /**< Mask start bit address */
	uint8_t mask_len;                   /**< Mask bit length */
	uint8_t mask_data[E310_MAX_MASK_LENGTH]; /**< Mask data */
} e310_read_params_t;

/**
 * @brief Read Data response structure
 */
typedef struct {
	uint8_t data[240];          /**< Read data (max 120 words = 240 bytes) */
	uint8_t word_count;         /**< Number of words read */
	uint8_t status;             /**< Response status code */
} e310_read_response_t;

/**
 * @brief Write Data parameters for 0x03 command
 */
typedef struct {
	uint8_t epc[E310_MAX_EPC_LENGTH];  /**< Target tag EPC */
	uint8_t epc_len;                    /**< EPC length in bytes */
	uint8_t mem_bank;                   /**< Memory bank (0x00-0x03) */
	uint8_t word_ptr;                   /**< Start word address */
	uint8_t data[240];                  /**< Data to write */
	uint8_t word_count;                 /**< Number of words to write */
	uint8_t password[4];                /**< Access password */
} e310_write_params_t;

/**
 * @brief Select command parameters for 0x9A command
 */
typedef struct {
	uint8_t antenna;            /**< Antenna bitmask (1-8 port: 1 byte) */
	uint8_t target;             /**< Select target (S0, S1, S2, S3, SL) */
	uint8_t action;             /**< Select action (0-7) */
	uint8_t mem_bank;           /**< Mask memory bank (EPC, TID, User) */
	uint16_t pointer;           /**< Mask start bit address */
	uint8_t mask_len;           /**< Mask bit length */
	uint8_t mask[E310_MAX_MASK_LENGTH]; /**< Mask data */
	uint8_t truncate;           /**< Truncation (0: disable, 1: enable) */
} e310_select_params_t;

/**
 * @brief Write EPC parameters for 0x04 command
 */
typedef struct {
	uint8_t old_epc[E310_MAX_EPC_LENGTH]; /**< Current EPC */
	uint8_t old_epc_len;                   /**< Current EPC length */
	uint8_t new_epc[E310_MAX_EPC_LENGTH]; /**< New EPC to write */
	uint8_t new_epc_len;                   /**< New EPC length */
	uint8_t password[4];                   /**< Access password */
} e310_write_epc_params_t;

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
 * **CRC Algorithm**: Polynomial 0x8408 (reflected 0x1021), Initial 0xFFFF, LSB-first
 * This is commonly known as CRC-16/MODBUS or CRC-16/CCITT (reflected variant).
 *
 * **Confirmed by E310 Protocol Specification**: UHFEx10 Manual V2.20 states:
 * - Polynomial: 0x8408
 * - Initial: 0xFFFF
 * - Byte order: LSB first
 *
 * @param data Pointer to data buffer
 * @param length Length of data
 * @return CRC-16 value (16-bit)
 */
uint16_t e310_crc16(const uint8_t *data, size_t length);

/**
 * @brief Verify CRC-16 of received frame
 *
 * @param frame Pointer to complete frame (including CRC at end)
 * @param length Total frame length
 * @return E310_OK if CRC is valid, E310_ERR_CRC_FAILED otherwise
 */
int e310_verify_crc(const uint8_t *frame, size_t length);

/**
 * @brief Build command frame header
 *
 * @param ctx Context
 * @param cmd Command code
 * @param data_len Length of data payload
 * @return Total frame length (header + data + CRC), or negative error code
 */
int e310_build_frame_header(e310_context_t *ctx, uint8_t cmd, size_t data_len);

/**
 * @brief Finalize frame by adding CRC
 *
 * @param ctx Context
 * @param frame_len Current frame length (without CRC)
 * @return Total length including CRC, or negative error code
 */
int e310_finalize_frame(e310_context_t *ctx, size_t frame_len);

/**
 * @brief Parse response frame header
 *
 * @param frame Received frame data
 * @param length Frame length
 * @param header Output: parsed header
 * @return E310_OK on success, negative error code otherwise
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
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_start_fast_inventory(e310_context_t *ctx, uint8_t target);

/**
 * @brief Build "Stop Fast Inventory" command (0x51)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_stop_fast_inventory(e310_context_t *ctx);

/**
 * @brief Build "Tag Inventory" command (0x01) with default parameters
 *
 * Uses the same parameters as Windows configuration software.
 *
 * @param ctx Protocol context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_tag_inventory_default(e310_context_t *ctx);

/**
 * @brief Build "Set Work Mode" command (0x7F)
 *
 * Used in connection sequence to initialize the reader.
 *
 * @param ctx Protocol context
 * @param mode Work mode (0x00 = default)
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_set_work_mode(e310_context_t *ctx, uint8_t mode);

/**
 * @brief Build "Tag Inventory" command (0x01)
 *
 * Single inventory operation with detailed parameters.
 *
 * @param ctx Context
 * @param params Inventory parameters
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_tag_inventory(e310_context_t *ctx,
                               const e310_inventory_params_t *params);

/**
 * @brief Build "Obtain Reader Information" command (0x21)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_obtain_reader_info(e310_context_t *ctx);

/**
 * @brief Build "Stop Immediately" command (0x93)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_stop_immediately(e310_context_t *ctx);

/**
 * @brief Build "Read Data" command (0x02)
 *
 * @param ctx Context
 * @param params Read parameters
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_read_data(e310_context_t *ctx, const e310_read_params_t *params);

/**
 * @brief Build "Write Data" command (0x03)
 *
 * @param ctx Context
 * @param params Write parameters
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_write_data(e310_context_t *ctx, const e310_write_params_t *params);

/**
 * @brief Build "Write EPC" command (0x04)
 *
 * @param ctx Context
 * @param params Write EPC parameters
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_write_epc(e310_context_t *ctx, const e310_write_epc_params_t *params);

/**
 * @brief Build "Modify RF Power" command (0x2F)
 *
 * @param ctx Context
 * @param power RF power level (0-30 dBm)
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_modify_rf_power(e310_context_t *ctx, uint8_t power);

/**
 * @brief Build "Select" command (0x9A)
 *
 * @param ctx Context
 * @param params Select parameters
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_select(e310_context_t *ctx, const e310_select_params_t *params);

/**
 * @brief Build "Single Tag Inventory" command (0x0F)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_single_tag_inventory(e310_context_t *ctx);

/**
 * @brief Build "Obtain Reader SN" command (0x4C)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_obtain_reader_sn(e310_context_t *ctx);

/**
 * @brief Build "Get Data From Buffer" command (0x72)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_get_data_from_buffer(e310_context_t *ctx);

/**
 * @brief Build "Clear Memory Buffer" command (0x73)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_clear_memory_buffer(e310_context_t *ctx);

/**
 * @brief Build "Get Tag Count From Buffer" command (0x74)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_get_tag_count(e310_context_t *ctx);

/**
 * @brief Build "Measure Temperature" command (0x92)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_measure_temperature(e310_context_t *ctx);

/* ========================================================================
 * Command Builders - Configuration Commands
 * ======================================================================== */

/**
 * @brief Build "Modify Frequency" command (0x22)
 *
 * @param ctx Context
 * @param max_fre Encoded max frequency byte (band in bits 7:6, point in bits 5:0)
 * @param min_fre Encoded min frequency byte (band in bits 7:6, point in bits 5:0)
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_modify_frequency(e310_context_t *ctx, uint8_t max_fre, uint8_t min_fre);

/**
 * @brief Build "Modify Reader Address" command (0x24)
 *
 * @param ctx Context
 * @param new_addr New reader address (0x00-0xFE)
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_modify_reader_addr(e310_context_t *ctx, uint8_t new_addr);

/**
 * @brief Build "Modify Inventory Time" command (0x25)
 *
 * @param ctx Context
 * @param time_100ms Inventory time in 100ms units
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_modify_inventory_time(e310_context_t *ctx, uint8_t time_100ms);

/**
 * @brief Build "Modify Baud Rate" command (0x28)
 *
 * @param ctx Context
 * @param baud_index Baud rate index (0=9600, 1=19200, 2=38400, 5=57600, 6=115200)
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_modify_baud_rate(e310_context_t *ctx, uint8_t baud_index);

/**
 * @brief Build "LED/Buzzer Control" command (0x33)
 *
 * @param ctx Context
 * @param active_time ON time in 50ms units
 * @param silent_time OFF time in 50ms units
 * @param times Number of ON/OFF cycles
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_led_buzzer_control(e310_context_t *ctx, uint8_t active_time,
                                    uint8_t silent_time, uint8_t times);

/**
 * @brief Build "Setup Antenna Mux" command (0x3F)
 *
 * @param ctx Context
 * @param antenna_config Antenna configuration byte
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_setup_antenna_mux(e310_context_t *ctx, uint8_t antenna_config);

/**
 * @brief Build "Enable/Disable Buzzer" command (0x40)
 *
 * @param ctx Context
 * @param enable true to enable buzzer, false to disable
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_enable_buzzer(e310_context_t *ctx, bool enable);

/**
 * @brief Build "GPIO Control" command (0x46)
 *
 * @param ctx Context
 * @param gpio_state GPIO output state bits
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_gpio_control(e310_context_t *ctx, uint8_t gpio_state);

/**
 * @brief Build "Obtain GPIO State" command (0x47)
 *
 * @param ctx Context
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_obtain_gpio_state(e310_context_t *ctx);

/**
 * @brief Build "Kill Tag" command (0x05)
 *
 * @param ctx Context
 * @param epc Target tag EPC
 * @param epc_len EPC length in bytes
 * @param kill_password 4-byte kill password
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_kill_tag(e310_context_t *ctx, const uint8_t *epc, uint8_t epc_len,
                         const uint8_t *kill_password);

/**
 * @brief Build "Set Protection" command (0x06)
 *
 * @param ctx Context
 * @param epc Target tag EPC
 * @param epc_len EPC length in bytes
 * @param select_flag Memory selection flag
 * @param set_flag Protection setting flag
 * @param password 4-byte access password
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_set_protection(e310_context_t *ctx, const uint8_t *epc,
                               uint8_t epc_len, uint8_t select_flag,
                               uint8_t set_flag, const uint8_t *password);

/**
 * @brief Build "Block Erase" command (0x07)
 *
 * @param ctx Context
 * @param epc Target tag EPC
 * @param epc_len EPC length in bytes
 * @param mem_bank Memory bank (0=Reserved, 1=EPC, 2=TID, 3=User)
 * @param word_ptr Start word address
 * @param word_count Number of words to erase
 * @param password 4-byte access password
 * @return Frame length ready to transmit, or negative error code
 */
int e310_build_block_erase(e310_context_t *ctx, const uint8_t *epc,
                            uint8_t epc_len, uint8_t mem_bank,
                            uint8_t word_ptr, uint8_t word_count,
                            const uint8_t *password);

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
 * @return E310_OK on success, negative error code otherwise
 */
int e310_parse_inventory_stats(const uint8_t *data, size_t length,
                                 e310_inventory_stats_t *stats);

/**
 * @brief Parse reader information response
 *
 * @param data Response data field
 * @param length Data length
 * @param info Output: reader information
 * @return E310_OK on success, negative error code otherwise
 */
int e310_parse_reader_info(const uint8_t *data, size_t length,
                             e310_reader_info_t *info);

/**
 * @brief Parse auto-upload tag data (fast inventory mode, reCmd=0xEE)
 *
 * @param data Response data field
 * @param length Data length
 * @param tag Output: tag data
 * @return Number of bytes consumed, or negative error code
 */
int e310_parse_auto_upload_tag(const uint8_t *data, size_t length,
                                 e310_tag_data_t *tag);

/**
 * @brief Parse Read Data response (0x02)
 *
 * @param data Response data field
 * @param length Data length
 * @param response Output: read response data
 * @return E310_OK on success, negative error code otherwise
 */
int e310_parse_read_response(const uint8_t *data, size_t length,
                               e310_read_response_t *response);

/**
 * @brief Parse Tag Count response (0x74)
 *
 * @param data Response data field
 * @param length Data length
 * @param count Output: tag count
 * @return E310_OK on success, negative error code otherwise
 */
int e310_parse_tag_count(const uint8_t *data, size_t length, uint32_t *count);

/**
 * @brief Parse Temperature response (0x92)
 *
 * @param data Response data field
 * @param length Data length
 * @param temperature Output: temperature in Celsius
 * @return E310_OK on success, negative error code otherwise
 */
int e310_parse_temperature(const uint8_t *data, size_t length, int8_t *temperature);

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
 * @brief Get error description string
 *
 * @param error_code Error code (E310_ERR_*)
 * @return Human-readable error description
 */
const char *e310_get_error_desc(int error_code);

/**
 * @brief Format EPC data as hex string
 *
 * @param epc EPC data buffer
 * @param epc_len EPC length in bytes
 * @param output Output string buffer
 * @param output_size Size of output buffer
 * @return Number of characters written (excluding null terminator), or negative error
 */
int e310_format_epc_string(const uint8_t *epc, uint8_t epc_len,
                            char *output, size_t output_size);

/** @} */ /* End of e310_protocol group */

#ifdef __cplusplus
}
#endif

#endif /* E310_PROTOCOL_H_ */
