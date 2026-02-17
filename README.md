# PARP-01 RFID Reader System

> **STM32H723ZG-based RFID Reader with PC Configuration Interface**

## Quick Start

### Build and Flash
```bash
# Navigate to workspace root
cd $HOME/work/zephyr_ws/zephyrproject

# Activate virtual environment
source .venv/bin/activate

# Build
west build -b nucleo_h723zg_parp01 apps/parp_01

# Flash to board
west flash
```

### System Overview
PARP-01 is a dual-mode RFID reader system:
- **Configuration Mode**: Transparent UART bridge between PC and RFID module
- **Inventory Mode**: Autonomous RFID tag reading with USB HID keyboard output

## Key Features
- ✅ STM32H723ZG @ 275 MHz
- ✅ Dual UART (USART1 for PC, UART4 for RFID module)
- ✅ USB Composite Device (HID Keyboard + Virtual Serial)
- ✅ EPC tag parsing and filtering
- ✅ I2C EEPROM for persistent settings
- ✅ LED/Switch based mode control

## Hardware
- **MCU**: STM32H723ZG (1MB Flash, 320KB RAM)
- **Console**: USART1 (PB14-TX, PB15-RX) @ 115200 baud
- **RFID Interface**: UART4 (PD0-TX, PD1-RX)
- **USB**: USB OTG HS (PA11/PA12)
- **Storage**: M24C64 EEPROM (8KB) via I2C4
- **User Interface**: 3 LEDs, 2 Switches

## Documentation
- **[CLAUDE.md](CLAUDE.md)**: Development guidelines for Claude Code
- **[docs/RFID_SYSTEM_DESIGN.md](docs/RFID_SYSTEM_DESIGN.md)**: Complete system architecture
- **[docs/SESSION_NOTES.md](docs/SESSION_NOTES.md)**: Development session history
- **[docs/README.md](docs/README.md)**: Documentation index

## Current Status
- ✅ Hardware configuration complete
- ✅ Custom board definition complete
- ✅ Clock configuration optimized (275 MHz)
- ✅ Build system functional
- ⚠️ Software implementation: Basic LED demo (Phase 1 in progress)

## Development Status

### Completed
- [x] Custom board structure (complete, not overlay-based)
- [x] STM32H723 clock configuration
- [x] All peripherals defined in device tree
- [x] Build system configuration
- [x] System architecture design

### In Progress
- [ ] Phase 1: UART infrastructure
- [ ] Phase 2: Mode control system
- [ ] Phase 3: EPC parser
- [ ] Phase 4: USB composite device
- [ ] Phase 5: EEPROM settings

### Planned
- [ ] Hardware testing on actual board
- [ ] Integration testing
- [ ] Advanced features (RTC, statistics, etc.)

## Operating Modes

### Configuration Mode (SW0)
PC configuration software communicates directly with RFID module through transparent UART bridge.

```
PC (USART1) ←→ [PARP-01 Bridge] ←→ RFID Module (UART4)
```

### Inventory Mode (SW1)
RFID module reads tags, PARP-01 extracts EPC codes and sends them as USB HID keyboard input to PC.

```
RFID Module (UART4) → [EPC Parser] → USB HID Keyboard → PC
```

## Build Requirements
- Zephyr RTOS 4.3+
- Zephyr SDK 0.17.4
- Python virtual environment at workspace root
- ST-Link programmer (for flashing)

## License
See project LICENSE file.

## Contact
For issues or questions, refer to [docs/SESSION_NOTES.md](docs/SESSION_NOTES.md) for development history.
