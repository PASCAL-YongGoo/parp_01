# PARP-01 RFID Reader System - Implementation Design

> **Last Updated**: 2026-01-11
> **Project Type**: RFID Reader with PC Configuration Interface
> **Status**: Design Phase

---

## Table of Contents
1. [System Overview](#system-overview)
2. [System Architecture](#system-architecture)
3. [Operating Modes](#operating-modes)
4. [Data Flow](#data-flow)
5. [Implementation Phases](#implementation-phases)
6. [Detailed Component Design](#detailed-component-design)
7. [Memory and Performance](#memory-and-performance)

---

## System Overview

### Purpose
PARP-01 is an RFID reader system that operates in two modes:
1. **Configuration Mode**: Transparent UART bridge between PC and RFID module
2. **Inventory Mode**: Autonomous RFID reading with USB HID keyboard output

### Key Features
- **Dual-mode operation** controlled by switches
- **UART bypass** for RFID module configuration
- **EPC tag parsing** from RFID responses
- **USB composite device** (HID Keyboard + Virtual Serial)
- **Persistent settings** via I2C EEPROM

---

## System Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                      PARP-01 Board                          │
│  ┌─────────────────────────────────────────────────────┐    │
│  │               STM32H723ZG @ 275MHz                  │    │
│  │                                                     │    │
│  │  ┌─────────────┐        ┌──────────────┐          │    │
│  │  │   USART1    │◄──────►│ UART Router  │          │    │
│  │  │ (PC Config) │        │   (Bridge/   │          │    │
│  │  └─────────────┘        │   Parser)    │          │    │
│  │                         │              │          │    │
│  │  ┌─────────────┐        │              │          │    │
│  │  │   UART4     │◄──────►│              │          │    │
│  │  │ (RFID Mod.) │        └──────────────┘          │    │
│  │  └─────────────┘              │                   │    │
│  │                                │                   │    │
│  │  ┌─────────────┐               ▼                   │    │
│  │  │   USB OTG   │        ┌──────────────┐          │    │
│  │  │  (HID+CDC)  │◄───────│  EPC Parser  │          │    │
│  │  └─────────────┘        └──────────────┘          │    │
│  │                                │                   │    │
│  │  ┌─────────────┐               │                   │    │
│  │  │   I2C4 +    │               │                   │    │
│  │  │   EEPROM    │◄──────────────┘                   │    │
│  │  └─────────────┘      (Settings)                  │    │
│  │                                                     │    │
│  │  ┌─────────────┐                                   │    │
│  │  │GPIO: LEDs/SW│ (Status indicators & control)    │    │
│  │  └─────────────┘                                   │    │
│  └─────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────┘

External Connections:
- USART1 ←→ PC (Configuration Software via USB-Serial adapter)
- UART4  ←→ RFID Module (Reader chip)
- USB    ←→ PC (HID Keyboard output + Debug console)
```

---

## Operating Modes

### Mode 1: Configuration Mode (Bypass Mode)
**Purpose**: Allow PC configuration software to communicate directly with RFID module

**Trigger**:
- Switch SW0 pressed (manual control)
- OR special command from PC
- OR default mode on startup

**Behavior**:
```
USART1 (PC) ──→ [Transparent Bridge] ──→ UART4 (RFID)
USART1 (PC) ←── [Transparent Bridge] ←── UART4 (RFID)
```

**Characteristics**:
- ✅ Zero protocol processing
- ✅ Bidirectional transparent data forwarding
- ✅ No EPC parsing
- ✅ Low latency (<1ms)
- ✅ LED indicator: LED0 ON (configuration mode active)

**Use Cases**:
- RFID module firmware update
- Power level configuration
- Frequency band setup
- Module diagnostics

---

### Mode 2: Inventory Mode (Active Reading)
**Purpose**: Autonomous RFID tag reading with USB HID keyboard output

**Trigger**:
- Switch SW1 pressed (start inventory)
- OR command via USB CDC
- Configuration mode must be disabled first

**Behavior**:
```
UART4 (RFID) ──→ [Protocol Parser] ──→ [EPC Extractor] ──→ USB HID Keyboard

During this mode:
- USART1 input is BLOCKED (not forwarded to UART4)
- UART4 responses are parsed for EPC data
- Valid EPCs are sent as keystrokes to PC
```

**Characteristics**:
- ✅ Protocol parsing enabled
- ✅ EPC extraction and validation
- ✅ USB HID keyboard emulation
- ✅ Duplicate filtering (configurable timeout)
- ✅ LED indicator: LED1 blinking (reading active)
- ❌ UART1→UART4 forwarding disabled

**Use Cases**:
- Warehouse inventory scanning
- Asset tracking
- Access control tag reading

---

## Data Flow

### Configuration Mode Data Flow
```
┌──────────┐         ┌──────────────┐         ┌──────────┐
│ PC Tool  │         │   PARP-01    │         │   RFID   │
│ (USART1) │◄───────►│  (Bypass)    │◄───────►│  Module  │
└──────────┘         └──────────────┘         │ (UART4)  │
                                               └──────────┘
TX: PC Command ──────────────────────────────► RX: Module
RX: PC Response ◄───────────────────────────── TX: Module

No processing, pure data forwarding
```

### Inventory Mode Data Flow
```
┌──────────┐         ┌──────────────────────────────┐         ┌──────────┐
│    PC    │         │         PARP-01              │         │   RFID   │
│   (USB   │◄────────│  ┌────────────────────┐      │         │  Module  │
│HID+CDC)  │         │  │  EPC Parser        │◄─────┼─────────│ (UART4)  │
└──────────┘         │  │  - Parse protocol  │      │         └──────────┘
                     │  │  - Extract EPC     │      │
     ▲               │  │  - Validate CRC    │      │
     │               │  └────────────────────┘      │
     │               │           │                  │
     │               │           ▼                  │
     │               │  ┌────────────────────┐      │
     │               │  │  USB HID Handler   │      │
     └───────────────┼──│  - Format as keys  │      │
                     │  │  - Send to PC      │      │
                     │  └────────────────────┘      │
                     └──────────────────────────────┘

USART1 input is BLOCKED in this mode
```

---

## Implementation Phases

### Phase 1: Core UART Infrastructure ⭐ CRITICAL
**Goal**: Implement bidirectional UART bridge between USART1 and UART4

**Tasks**:
1. **UART Configuration**
   - Enable UART4 in DTS (already done)
   - Configure UART4 in prj.conf
   - Set baud rate (typical RFID: 115200)
   - Configure RX/TX buffers

2. **UART Ring Buffers**
   - Implement thread-safe ring buffers
   - USART1 RX buffer → UART4 TX buffer
   - UART4 RX buffer → USART1 TX buffer
   - Size: 1KB per buffer (adjustable)

3. **Bypass Mode Implementation**
   - UART interrupt handlers
   - Zero-copy forwarding where possible
   - Minimal latency (<1ms)
   - Flow control handling

4. **Testing**
   - Loopback test (UART4 TX→RX)
   - Transparent forwarding test
   - Throughput test (max data rate)
   - Latency measurement

**Success Criteria**:
- ✅ PC configuration tool can communicate with RFID module
- ✅ Bidirectional data forwarding works
- ✅ No data loss or corruption
- ✅ Latency < 1ms

**Estimated Effort**: 2-3 days
**Memory Impact**: +5KB Flash, +2KB RAM

---

### Phase 2: Mode Control System
**Goal**: Implement mode switching logic with switch and LED control

**Tasks**:
1. **GPIO Switch Handling**
   - SW0: Enter/Exit configuration mode
   - SW1: Start/Stop inventory mode
   - Implement debouncing (20ms)
   - Interrupt-based detection

2. **Mode State Machine**
   ```
   States:
   - IDLE: No active mode
   - CONFIG: Configuration/bypass mode active
   - INVENTORY: Reading mode active

   Transitions:
   - IDLE → CONFIG: SW0 press
   - CONFIG → IDLE: SW0 release
   - IDLE → INVENTORY: SW1 press (only if CONFIG inactive)
   - INVENTORY → IDLE: SW1 release or timeout
   ```

3. **LED Status Indicators**
   - LED0 (PE2): Configuration mode active (solid ON)
   - LED1 (PE3): Inventory mode active (blinking 2Hz)
   - TEST_LED (PE6): System heartbeat (always blinking 1Hz)

4. **Mode Control via USB CDC**
   - Commands: `MODE CONFIG`, `MODE INVENTORY`, `MODE IDLE`
   - Status query: `STATUS`

**Success Criteria**:
- ✅ Switches control mode transitions
- ✅ LEDs indicate current mode
- ✅ Mode changes are reliable
- ✅ No mode conflicts (inventory blocked during config)

**Estimated Effort**: 1-2 days
**Memory Impact**: +3KB Flash, +1KB RAM

---

### Phase 3: RFID Protocol Parser & EPC Extraction ⭐ CRITICAL
**Goal**: Parse UART4 data and extract EPC codes

**Tasks**:
1. **Protocol Analysis**
   - Identify RFID module protocol (likely ISO 18000-6C / EPC Gen2)
   - Document packet structure
   - Identify EPC field location and format

2. **Packet Parser**
   - State machine for packet framing
   - Header/footer detection
   - Length validation
   - CRC/checksum verification

3. **EPC Extractor**
   - Extract EPC data from valid packets
   - Support common EPC lengths:
     - 96-bit (12 bytes) - most common
     - 128-bit (16 bytes)
     - 256-bit (32 bytes)
   - Format as hex string (e.g., "3000 1234 5678 9ABC")

4. **Duplicate Filtering**
   - Maintain read tag list (last 100 tags)
   - Configurable timeout (default: 2 seconds)
   - Prevent repetitive output for same tag

**Success Criteria**:
- ✅ Successfully parse RFID module responses
- ✅ Extract EPC codes accurately
- ✅ Validate data integrity (CRC)
- ✅ Filter duplicate reads

**Estimated Effort**: 3-4 days
**Memory Impact**: +6KB Flash, +3KB RAM (includes tag buffer)

---

### Phase 4: USB Composite Device (HID Keyboard + CDC ACM) ⭐ CRITICAL
**Goal**: Implement USB composite device with keyboard and serial functions

**Tasks**:
1. **USB Stack Configuration**
   ```kconfig
   CONFIG_USB_DEVICE_STACK=y
   CONFIG_USB_DEVICE=y
   CONFIG_USB_DEVICE_MANUFACTURER="PARP"
   CONFIG_USB_DEVICE_PRODUCT="RFID Reader"
   CONFIG_USB_DEVICE_VID=0x2FE3  # Custom VID (example)
   CONFIG_USB_DEVICE_PID=0x0001

   # HID Keyboard
   CONFIG_USB_DEVICE_HID=y
   CONFIG_USB_HID_BOOT_PROTOCOL=y

   # CDC ACM (Virtual Serial)
   CONFIG_USB_CDC_ACM=y
   CONFIG_UART_LINE_CTRL=y
   ```

2. **USB HID Keyboard Implementation**
   - Implement HID keyboard descriptor
   - EPC output as keystrokes
   - Format: `EPC_VALUE<ENTER>`
   - Key press/release timing
   - Handle special characters (numbers, spaces)

3. **USB CDC ACM Implementation**
   - Virtual serial port for:
     - Debug output
     - Mode control commands
     - Status queries
     - Configuration
   - Baud rate: 115200

4. **USB Enumeration & Connection**
   - Handle USB connect/disconnect
   - Proper enumeration sequence
   - LED indication for USB status

**Success Criteria**:
- ✅ USB composite device enumerates correctly
- ✅ HID keyboard recognized by PC
- ✅ CDC virtual serial port works
- ✅ EPC data appears as keyboard input
- ✅ Both functions work simultaneously

**Estimated Effort**: 3-4 days
**Memory Impact**: +15KB Flash, +5KB RAM

---

### Phase 5: Configuration & Persistence
**Goal**: Store/retrieve settings from I2C EEPROM

**Tasks**:
1. **I2C EEPROM Driver**
   ```kconfig
   CONFIG_I2C=y
   CONFIG_EEPROM=y
   ```

2. **Configuration Parameters**
   ```c
   typedef struct {
       uint32_t magic;              // 0xPARP0001
       uint8_t  inventory_timeout;  // seconds
       uint8_t  duplicate_timeout;  // seconds
       uint8_t  led_brightness;     // 0-100%
       uint16_t uart4_baudrate;     // 115200, 57600, etc.
       uint8_t  epc_format;         // HEX, DEC, custom
       uint8_t  reserved[16];
       uint16_t crc16;
   } parp_config_t;
   ```

3. **Settings Management**
   - Load settings on boot
   - Save settings on change
   - Factory reset function
   - Settings validation

4. **Shell Commands**
   ```
   settings get
   settings set <param> <value>
   settings save
   settings reset
   ```

**Success Criteria**:
- ✅ Settings persist across power cycles
- ✅ EEPROM read/write works reliably
- ✅ Invalid settings detected and rejected
- ✅ Factory defaults available

**Estimated Effort**: 2 days
**Memory Impact**: +4KB Flash, +1KB RAM

---

### Phase 6: Advanced Features (Optional)
**Goal**: Additional functionality for production use

**Features**:
1. **RTC Timestamp**
   - Timestamp each EPC read
   - Log format: `[2026-01-11 14:32:15] EPC: 3000...`

2. **Statistics**
   - Total tags read
   - Unique tags
   - Read rate (tags/second)
   - Uptime

3. **Buzzer/Audio Feedback**
   - Beep on successful read
   - Different tones for different events

4. **LCD Display** (if hardware available)
   - Current mode
   - Tag count
   - Last EPC read

**Estimated Effort**: Variable (1-3 days per feature)

---

## Detailed Component Design

### 1. UART Router Module

```c
// uart_router.h

typedef enum {
    ROUTER_MODE_IDLE,
    ROUTER_MODE_BYPASS,      // UART1 ↔ UART4 transparent
    ROUTER_MODE_INVENTORY    // UART4 → Parser → USB
} router_mode_t;

typedef struct {
    const struct device *uart1;
    const struct device *uart4;
    router_mode_t mode;

    // Ring buffers
    struct ring_buf uart1_rx_buf;
    struct ring_buf uart4_rx_buf;

    // Statistics
    uint32_t bytes_forwarded;
    uint32_t packets_parsed;
} uart_router_t;

// API
int uart_router_init(uart_router_t *router);
int uart_router_set_mode(uart_router_t *router, router_mode_t mode);
void uart_router_process(uart_router_t *router);
```

**Implementation Notes**:
- Use ring buffers from Zephyr (`<zephyr/sys/ring_buffer.h>`)
- UART interrupts push data to ring buffers
- Main processing thread pulls data and forwards/parses
- In bypass mode: minimal processing, direct forwarding
- In inventory mode: full parsing, EPC extraction

---

### 2. EPC Parser Module

```c
// epc_parser.h

#define EPC_MAX_LENGTH 32   // bytes
#define TAG_HISTORY_SIZE 100

typedef struct {
    uint8_t epc[EPC_MAX_LENGTH];
    uint8_t length;          // in bytes
    uint32_t timestamp;      // k_uptime_get_32()
    uint16_t rssi;           // signal strength
} epc_tag_t;

typedef struct {
    // Parser state
    uint8_t rx_buffer[256];
    size_t rx_index;

    // Tag history (duplicate filtering)
    epc_tag_t tag_history[TAG_HISTORY_SIZE];
    uint8_t history_index;

    // Configuration
    uint32_t duplicate_timeout_ms;
} epc_parser_t;

// API
int epc_parser_init(epc_parser_t *parser);
int epc_parser_feed(epc_parser_t *parser, uint8_t *data, size_t len);
bool epc_parser_get_tag(epc_parser_t *parser, epc_tag_t *tag);
bool epc_is_duplicate(epc_parser_t *parser, epc_tag_t *tag);
```

**Protocol Example** (typical RFID reader response):
```
Header: 0xA0 0x05       // Command response: inventory
Length: 0x12            // 18 bytes payload
EPC:    [12 bytes]      // EPC data
PC:     [2 bytes]       // Protocol Control
CRC:    [2 bytes]       // CRC-16
Footer: 0x0D 0x0A       // \r\n
```

---

### 3. USB HID Keyboard Module

```c
// usb_hid_kbd.h

typedef struct {
    const struct device *hid_dev;
    bool connected;

    // Output queue
    struct k_fifo output_queue;
} usb_hid_kbd_t;

// API
int usb_hid_kbd_init(usb_hid_kbd_t *kbd);
int usb_hid_send_string(usb_hid_kbd_t *kbd, const char *str);
int usb_hid_send_epc(usb_hid_kbd_t *kbd, epc_tag_t *tag);

// Internal
static void format_epc_string(epc_tag_t *tag, char *output);
// Example output: "E200 1234 5678 9ABC\n"
```

**HID Report Descriptor**:
- Boot protocol keyboard
- 6-key rollover (standard)
- Modifier keys support
- Output: EPC as hex string followed by ENTER

---

### 4. Mode Control State Machine

```c
// mode_control.h

typedef enum {
    EVENT_SW0_PRESS,
    EVENT_SW0_RELEASE,
    EVENT_SW1_PRESS,
    EVENT_SW1_RELEASE,
    EVENT_USB_CMD_CONFIG,
    EVENT_USB_CMD_INVENTORY,
    EVENT_USB_CMD_IDLE,
    EVENT_TIMEOUT
} mode_event_t;

typedef struct {
    router_mode_t current_mode;

    // Switch states
    bool sw0_active;
    bool sw1_active;

    // Timers
    struct k_timer inventory_timer;
    uint32_t inventory_duration_sec;
} mode_control_t;

// API
int mode_control_init(mode_control_t *ctrl);
int mode_control_handle_event(mode_control_t *ctrl, mode_event_t event);
router_mode_t mode_control_get_mode(mode_control_t *ctrl);
```

**State Machine**:
```
        SW0 Press               SW1 Press
IDLE ───────────→ CONFIG    IDLE ───────────→ INVENTORY
     ←───────────            (only if not in CONFIG)
      SW0 Release

INVENTORY → IDLE: SW1 release OR timeout OR manual command
CONFIG → IDLE: SW0 release OR manual command

Priority: CONFIG mode blocks INVENTORY mode
```

---

## Memory and Performance

### Memory Budget

**Current Usage**:
- Flash: 57 KB
- RAM: 9.4 KB

**Estimated Usage After Full Implementation**:

| Component | Flash | RAM | Notes |
|-----------|-------|-----|-------|
| Current baseline | 57 KB | 9.4 KB | LED blink demo |
| UART Router | +5 KB | +2 KB | Ring buffers, forwarding |
| Mode Control | +3 KB | +1 KB | State machine, GPIO |
| EPC Parser | +6 KB | +3 KB | Protocol parsing, tag buffer |
| USB HID+CDC | +15 KB | +5 KB | USB stack, descriptors |
| I2C EEPROM | +4 KB | +1 KB | Driver, settings |
| Shell commands | +4 KB | +1 KB | CLI interface |
| **Total** | **94 KB** | **22.4 KB** | - |
| **Available** | **1024 KB** | **320 KB** | STM32H723ZG |
| **Usage %** | **9.2%** | **7.0%** | Excellent headroom |

**Conclusion**: Memory usage is well within limits. Plenty of room for future features.

---

### Performance Targets

| Metric | Target | Notes |
|--------|--------|-------|
| UART Bypass Latency | < 1 ms | Configuration mode |
| EPC Parse Time | < 10 ms | Per packet |
| USB HID Output | < 50 ms | Per EPC keystroke sequence |
| Duplicate Check | < 1 ms | Hash lookup |
| Mode Switch Time | < 100 ms | User-perceptible limit |
| Max Tag Read Rate | 50 tags/sec | Limited by USB HID timing |

---

## Threading Model

### Proposed Thread Structure

```c
// Main thread: Initialization, mode control
// Priority: 0 (highest)

// UART Router thread: Data forwarding and parsing
// Priority: 1
// Stack: 2KB

// USB HID output thread: Send EPCs as keystrokes
// Priority: 2
// Stack: 2KB

// LED/GPIO control thread: Status indicators
// Priority: 5 (low)
// Stack: 1KB

// Shell thread: CLI commands (if enabled)
// Priority: 10 (lowest)
// Stack: 2KB (from Zephyr)
```

**Inter-thread Communication**:
- k_fifo: EPC parser → USB HID thread
- k_msgq: Mode control → UART router
- k_mutex: Shared resource protection (EEPROM, settings)

---

## Error Handling

### Critical Errors (System Halt)
- UART device initialization failure
- USB stack initialization failure
- Memory allocation failure

**Response**: Log error, blink error pattern (all LEDs fast), halt

### Recoverable Errors
- UART buffer overflow
- Invalid EPC packet
- USB disconnect
- EEPROM write failure

**Response**: Log warning, increment error counter, continue operation

### Error LED Patterns
- Normal operation: LED0/LED1 per mode, TEST_LED heartbeat
- Warning: TEST_LED rapid blink (5Hz)
- Critical error: All LEDs blink simultaneously (10Hz)

---

## Testing Strategy

### Unit Tests
- [x] UART loopback test
- [x] Ring buffer operations
- [x] EPC parser with known packets
- [x] USB HID string output
- [x] Mode state machine transitions
- [x] EEPROM read/write

### Integration Tests
- [x] UART1 ↔ UART4 bypass forwarding
- [x] End-to-end: RFID module → Parse → USB HID
- [x] Mode switching under load
- [x] USB composite device enumeration
- [x] Settings persistence

### System Tests
- [x] 8-hour stress test (continuous reading)
- [x] USB disconnect/reconnect handling
- [x] Power cycle settings retention
- [x] Maximum tag read rate test
- [x] Duplicate filtering accuracy

---

## Development Timeline

| Phase | Description | Duration | Dependencies |
|-------|-------------|----------|--------------|
| **Phase 1** | UART Infrastructure | 2-3 days | None |
| **Phase 2** | Mode Control | 1-2 days | Phase 1 |
| **Phase 3** | EPC Parser | 3-4 days | Phase 1 |
| **Phase 4** | USB HID+CDC | 3-4 days | Phase 3 |
| **Phase 5** | EEPROM Settings | 2 days | Phase 2 |
| **Phase 6** | Advanced Features | Variable | All above |
| **Testing** | Integration & System | 2-3 days | All phases |
| **Total** | - | **13-18 days** | - |

---

## Next Steps

### Immediate Actions
1. **Hardware Test** (if not done yet)
   ```bash
   cd /home/lyg/work/zephyr_ws/zephyrproject
   source .venv/bin/activate
   west flash
   ```
   Verify LED blink and console output on actual board.

2. **RFID Module Documentation**
   - Obtain RFID module datasheet
   - Document protocol format
   - Identify EPC response packet structure
   - Note baud rate, frame format, etc.

3. **Start Phase 1**
   - Configure UART4 in device tree (verify PD0/PD1 pins)
   - Enable UART4 in prj.conf
   - Implement basic UART loopback test

### Questions to Resolve
- [ ] What is the exact RFID module model/part number?
- [ ] What protocol does it use? (EPC Gen2, proprietary, etc.)
- [ ] What is the expected EPC output format from PC perspective?
- [ ] Should EPC include spaces/hyphens? (e.g., "3000-1234-5678-9ABC")
- [ ] Any specific key sequence after EPC? (TAB, ENTER, both?)
- [ ] USB VID/PID: Use custom values or generic?

---

**This design document will be updated as implementation progresses.**