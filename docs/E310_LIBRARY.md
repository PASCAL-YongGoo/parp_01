# E310 RFID Protocol Library Documentation

> **Library for Impinj E310 RFID Reader Module**
> **Version**: 1.0.0
> **Date**: 2026-01-11

---

## Table of Contents
1. [Overview](#overview)
2. [Features](#features)
3. [File Structure](#file-structure)
4. [API Reference](#api-reference)
5. [Usage Examples](#usage-examples)
6. [Protocol Details](#protocol-details)
7. [Build Instructions](#build-instructions)

---

## Overview

The E310 Protocol Library provides a complete, reusable implementation of the Impinj E310 RFID reader communication protocol. This library is designed to be portable, easy to integrate, and fully compliant with the UHFEx10 User Manual V2.20 specification.

### Key Design Principles
- **Portable**: Minimal dependencies, works on any embedded platform
- **Reusable**: Clean API, separate from application code
- **Memory Efficient**: Static buffers, no dynamic allocation
- **Well Documented**: Extensive inline documentation and examples

---

## Features

### Implemented Commands
✅ **Fast Inventory Mode** (0x50/0x51)
  - Start/Stop continuous tag reading
  - Auto-upload tag data in real-time
  - Ideal for inventory applications

✅ **Standard Inventory** (0x01)
  - Detailed inventory with Q-value, session, mask support
  - Optional TID reading
  - Antenna selection
  - Scan time configuration

✅ **Reader Configuration** (0x21)
  - Get firmware version
  - Read RF power settings
  - Check antenna configuration

✅ **Stop Immediately** (0x93)
  - Emergency stop command

### Protocol Features
✅ CRC-16 calculation and verification (CRC-16-CCITT-FALSE)
✅ Frame building with automatic CRC appending
✅ Response parsing with error checking
✅ EPC tag data extraction
✅ Reader information parsing
✅ Auto-upload tag parsing (fast inventory mode)
✅ Command/status string lookup utilities

---

## File Structure

```
src/
├── e310_protocol.h       # Header file (API definitions)
├── e310_protocol.c       # Implementation
└── e310_test.c           # Test suite and examples
```

### Source Files

#### **e310_protocol.h**
- Command codes (0x01, 0x50, 0x51, etc.)
- Status codes (0x00, 0x01, 0x26, etc.)
- Data structures (tag, reader info, inventory params)
- API function prototypes
- Protocol constants

#### **e310_protocol.c**
- CRC-16 implementation
- Frame building functions
- Command builders
- Response parsers
- Utility functions

#### **e310_test.c**
- Unit tests for all functions
- Usage examples
- CRC verification test
- Command building tests
- Response parsing tests

---

## API Reference

### Initialization

```c
void e310_init(e310_context_t *ctx, uint8_t reader_addr);
```

Initialize E310 protocol context with reader address (usually `0x00` or `0xFF` for broadcast).

**Parameters**:
- `ctx`: Pointer to context structure
- `reader_addr`: Reader address (0x00-0xFE, or 0xFF for broadcast)

**Example**:
```c
e310_context_t ctx;
e310_init(&ctx, E310_ADDR_DEFAULT);  // 0x00
```

---

### CRC Functions

```c
uint16_t e310_crc16(const uint8_t *data, size_t length);
bool e310_verify_crc(const uint8_t *frame, size_t length);
```

Calculate and verify CRC-16 checksums using CRC-16-CCITT-FALSE algorithm.

**Parameters**:
- `data`: Data buffer
- `length`: Length of data

**Returns**:
- `e310_crc16()`: 16-bit CRC value
- `e310_verify_crc()`: true if CRC is valid, false otherwise

---

### Command Builders

#### Start Fast Inventory (0x50)
```c
size_t e310_build_start_fast_inventory(e310_context_t *ctx, uint8_t target);
```

Start continuous inventory mode where tags are automatically sent as they're read.

**Parameters**:
- `ctx`: Context
- `target`: `E310_TARGET_A` (0x00) or `E310_TARGET_B` (0x01)

**Returns**: Frame length ready to transmit

**Usage**:
```c
size_t len = e310_build_start_fast_inventory(&ctx, E310_TARGET_A);
uart_send(ctx.tx_buffer, len);
```

#### Stop Fast Inventory (0x51)
```c
size_t e310_build_stop_fast_inventory(e310_context_t *ctx);
```

Stop continuous inventory mode.

**Returns**: Frame length ready to transmit

#### Tag Inventory (0x01)
```c
size_t e310_build_tag_inventory(e310_context_t *ctx,
                                 const e310_inventory_params_t *params);
```

Single inventory operation with detailed parameters.

**Parameters**:
- `ctx`: Context
- `params`: Inventory parameters (Q-value, session, mask, etc.)

**Example**:
```c
e310_inventory_params_t params = {
    .q_value = 4,                 // Q=4
    .session = E310_SESSION_S0,
    .mask_len = 0,                // No mask
    .tid_len = 0,                 // No TID
    .target = E310_TARGET_A,
    .antenna = E310_ANT_1,
    .scan_time = 10,              // 1 second (10 * 100ms)
};

size_t len = e310_build_tag_inventory(&ctx, &params);
uart_send(ctx.tx_buffer, len);
```

#### Obtain Reader Information (0x21)
```c
size_t e310_build_obtain_reader_info(e310_context_t *ctx);
```

Query reader firmware version, model, power settings, etc.

#### Stop Immediately (0x93)
```c
size_t e310_build_stop_immediately(e310_context_t *ctx);
```

Emergency stop command.

---

### Response Parsers

#### Parse Response Header
```c
int e310_parse_response_header(const uint8_t *frame, size_t length,
                                 e310_response_header_t *header);
```

Parse response frame header and verify CRC.

**Returns**:
- `0`: Success
- `-1`: Frame too short
- `-2`: CRC error
- `-3`: Length mismatch

**Example**:
```c
uint8_t rx_buffer[256];
size_t rx_len = uart_receive(rx_buffer, sizeof(rx_buffer));

e310_response_header_t header;
int ret = e310_parse_response_header(rx_buffer, rx_len, &header);

if (ret == 0) {
    printk("Command: 0x%02X, Status: 0x%02X\n", header.recmd, header.status);
}
```

#### Parse Auto-Upload Tag (Fast Inventory)
```c
int e310_parse_auto_upload_tag(const uint8_t *data, size_t length,
                                 e310_tag_data_t *tag);
```

Parse tag data from fast inventory mode (reCmd = 0xEE).

**Format**: `Ant | Len | EPC/TID | RSSI`

**Returns**: `0` on success, negative on error

**Example**:
```c
// In fast inventory mode, when reCmd == 0xEE:
e310_tag_data_t tag;
int ret = e310_parse_auto_upload_tag(&rx_buffer[4], rx_len - 6, &tag);

if (ret == 0) {
    char epc_str[128];
    e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));
    printk("EPC: %s, RSSI: %u\n", epc_str, tag.rssi);
}
```

#### Parse Tag Data (Standard Inventory)
```c
int e310_parse_tag_data(const uint8_t *data, size_t length,
                         e310_tag_data_t *tag);
```

Parse tag data from standard inventory response (0x01).

**Returns**: Number of bytes consumed, or negative error code

#### Parse Reader Information
```c
int e310_parse_reader_info(const uint8_t *data, size_t length,
                             e310_reader_info_t *info);
```

Parse reader information response (0x21).

**Example**:
```c
e310_reader_info_t info;
int ret = e310_parse_reader_info(&rx_buffer[4], 13, &info);

if (ret == 0) {
    printk("Firmware: %u.%u\n",
           (info.firmware_version >> 8) & 0xFF,
           info.firmware_version & 0xFF);
    printk("RF Power: %u dBm\n", info.power);
}
```

---

### Utility Functions

#### Get Command Name
```c
const char *e310_get_command_name(uint8_t cmd);
```

Returns human-readable command name string.

**Example**:
```c
printk("Command: %s\n", e310_get_command_name(0x50));
// Output: "Command: Start Fast Inventory"
```

#### Get Status Description
```c
const char *e310_get_status_desc(uint8_t status);
```

Returns human-readable status description.

#### Format EPC String
```c
int e310_format_epc_string(const uint8_t *epc, uint8_t epc_len,
                            char *output, size_t output_size);
```

Format EPC data as hex string with spaces every 4 bytes.

**Example**:
```c
char epc_str[128];
e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));
// Output: "E200 1234 5678 9ABC"
```

---

## Usage Examples

### Example 1: Fast Inventory Mode

```c
#include "e310_protocol.h"

e310_context_t ctx;
e310_init(&ctx, E310_ADDR_DEFAULT);

// Start fast inventory
size_t len = e310_build_start_fast_inventory(&ctx, E310_TARGET_A);
uart_send(ctx.tx_buffer, len);

// Wait for auto-uploaded tags (reCmd = 0xEE)
while (true) {
    uint8_t rx_buffer[256];
    size_t rx_len = uart_receive(rx_buffer, sizeof(rx_buffer));

    e310_response_header_t header;
    if (e310_parse_response_header(rx_buffer, rx_len, &header) == 0) {
        if (header.recmd == E310_RECMD_AUTO_UPLOAD) {
            e310_tag_data_t tag;
            if (e310_parse_auto_upload_tag(&rx_buffer[4], rx_len - 6, &tag) == 0) {
                // Process tag
                char epc_str[128];
                e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));
                printk("Tag: %s\n", epc_str);
            }
        }
    }
}

// Stop fast inventory
len = e310_build_stop_fast_inventory(&ctx);
uart_send(ctx.tx_buffer, len);
```

### Example 2: Single Inventory

```c
e310_context_t ctx;
e310_init(&ctx, E310_ADDR_DEFAULT);

// Configure inventory parameters
e310_inventory_params_t params = {
    .q_value = 4,
    .session = E310_SESSION_S0,
    .mask_mem = E310_MEMBANK_EPC,
    .mask_addr = 0,
    .mask_len = 0,
    .tid_addr = 0,
    .tid_len = 0,
    .target = E310_TARGET_A,
    .antenna = E310_ANT_1,
    .scan_time = 10,  // 1 second
};

// Send inventory command
size_t len = e310_build_tag_inventory(&ctx, &params);
uart_send(ctx.tx_buffer, len);

// Wait for response
uint8_t rx_buffer[256];
size_t rx_len = uart_receive(rx_buffer, sizeof(rx_buffer));

e310_response_header_t header;
if (e310_parse_response_header(rx_buffer, rx_len, &header) == 0) {
    if (header.status == E310_STATUS_OPERATION_COMPLETE) {
        // Parse tag data
        uint8_t *data_ptr = &rx_buffer[5];  // After Ant and Num fields
        e310_tag_data_t tag;
        int consumed = e310_parse_tag_data(data_ptr, rx_len - 7, &tag);
        if (consumed > 0) {
            char epc_str[128];
            e310_format_epc_string(tag.epc, tag.epc_len, epc_str, sizeof(epc_str));
            printk("EPC: %s, RSSI: %u\n", epc_str, tag.rssi);
        }
    }
}
```

### Example 3: Get Reader Information

```c
e310_context_t ctx;
e310_init(&ctx, E310_ADDR_DEFAULT);

// Send command
size_t len = e310_build_obtain_reader_info(&ctx);
uart_send(ctx.tx_buffer, len);

// Wait for response
uint8_t rx_buffer[256];
size_t rx_len = uart_receive(rx_buffer, sizeof(rx_buffer));

e310_response_header_t header;
if (e310_parse_response_header(rx_buffer, rx_len, &header) == 0) {
    if (header.status == E310_STATUS_SUCCESS) {
        e310_reader_info_t info;
        if (e310_parse_reader_info(&rx_buffer[4], 13, &info) == 0) {
            printk("Firmware: %u.%u\n",
                   (info.firmware_version >> 8) & 0xFF,
                   info.firmware_version & 0xFF);
            printk("Model: 0x%02X\n", info.model_type);
            printk("RF Power: %u dBm\n", info.power);
        }
    }
}
```

---

## Protocol Details

### Frame Format

**Request Frame**:
```
| Len | Adr | Cmd | Data[] | CRC-16 |
```

**Response Frame**:
```
| Len | Adr | reCmd | Status | Data[] | CRC-16 |
```

### CRC-16 Algorithm
- **Algorithm**: CRC-16-CCITT-FALSE
- **Polynomial**: 0x8408 (reversed 0x1021)
- **Initial Value**: 0xFFFF
- **Input Reflection**: No
- **Output Reflection**: No
- **XOR Output**: 0x0000
- **Byte Order**: LSB first

### Status Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | SUCCESS | Command executed successfully |
| 0x01 | OPERATION_COMPLETE | Inventory operation completed |
| 0x02 | INVENTORY_TIMEOUT | No tags found within scan time |
| 0x03 | MORE_DATA | Additional frames will follow |
| 0x04 | MEMORY_FULL | Reader memory buffer full |
| 0x26 | STATISTICS_DATA | Inventory statistics packet |
| 0xF8 | ANTENNA_ERROR | Antenna connection error |
| 0xFD | INVALID_LENGTH | Invalid frame length |
| 0xFE | INVALID_COMMAND_CRC | Invalid command or CRC error |
| 0xFF | UNKNOWN_PARAMETER | Unrecognized parameter |

### Fast Inventory Mode

**Start Command (0x50)**:
```
Request:  05 00 50 00 CRC CRC
Response: 05 00 50 00 CRC CRC  (Confirmation)
```

**Auto-Upload Tags (reCmd = 0xEE)**:
```
Auto-upload: Len Adr EE 00 Ant Len EPC... RSSI CRC CRC
```

**Stop Command (0x51)**:
```
Request:  04 00 51 CRC CRC
Response: 05 00 51 00 CRC CRC
```

---

## Build Instructions

### Integration into Project

1. **Copy Files**:
   ```
   cp e310_protocol.h src/
   cp e310_protocol.c src/
   cp e310_test.c src/      # Optional (for testing)
   ```

2. **Update CMakeLists.txt**:
   ```cmake
   target_sources(app PRIVATE
       src/main.c
       src/e310_protocol.c
       src/e310_test.c  # Optional
   )
   ```

3. **Include Header**:
   ```c
   #include "e310_protocol.h"
   ```

### Build Command

```bash
cd $HOME/work/zephyr_ws/zephyrproject
source .venv/bin/activate
west build -b nucleo_h723zg_parp01 apps/parp_01
```

### Memory Usage

With E310 library included:
- **Flash**: 61 KB (5.85% of 1 MB)
- **RAM**: 9.4 KB (2.87% of 320 KB)

---

## Testing

The library includes a comprehensive test suite in `e310_test.c`.

### Run Tests

```c
extern void e310_run_tests(void);

int main(void) {
    e310_run_tests();  // Run all tests
    return 0;
}
```

### Test Coverage

✅ CRC-16 calculation and verification
✅ Start Fast Inventory command building
✅ Stop Fast Inventory command building
✅ Obtain Reader Info command building
✅ Tag Inventory command building
✅ Auto-upload tag parsing
✅ Reader information parsing

---

## Next Steps

1. **UART Integration**: Connect library to UART4 driver
2. **Mode Control**: Implement bypass/inventory mode switching
3. **Tag Buffer**: Add duplicate filtering and tag history
4. **USB HID Output**: Send EPCs as keyboard input
5. **EEPROM Settings**: Store inventory parameters

---

## References

- **UHFEx10 User Manual V2.20** - Impinj E310 Protocol Specification
- **Reference Documents**: `/reference/protocol/*.md`
- **System Design**: [RFID_SYSTEM_DESIGN.md](RFID_SYSTEM_DESIGN.md)

---

**Library Version**: 1.0.0
**Last Updated**: 2026-01-11
**Author**: PARP Development Team
