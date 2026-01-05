# Development Session Notes

> This document tracks development progress across sessions and machines.
> **Update this file at the end of each significant development session.**

---

## Session 1: Custom Board Structure Migration (2026-01-05)

### Environment
- **Location**: Remote PC
- **Zephyr Version**: 4.3.99 (v4.3.0-1307-ge3ef835ffec7)
- **Toolchain**: Zephyr SDK 0.17.4
- **Workspace**: `/home/lyg/work/zephyr_ws/zephyrproject`

### Accomplishments

#### 1. Project Structure Migrated from Overlay to Custom Board
**Problem Identified**:
- Original project used overlay-based approach (`boards/nucleo_h723zg.overlay`)
- Board was not functioning correctly on hardware
- Clock configuration conflicts between base Nucleo board and custom hardware

**Solution Implemented**:
- Created complete custom board definition: `nucleo_h723zg_parp01`
- Migrated all overlay content to standalone DTS file
- Followed best practices from reference project (`apps/STM32F469I-DISCO_20260103-01/`)

**Files Created**:
```
boards/arm/nucleo_h723zg_parp01/
├── nucleo_h723zg_parp01.dts              # Complete board DTS (253 lines)
├── nucleo_h723zg_parp01_defconfig        # Board defaults
├── Kconfig.nucleo_h723zg_parp01          # Kconfig entry
├── Kconfig.defconfig                     # Auto-config
├── board.cmake                           # Flash/debug config
└── board.yml                             # Board metadata
```

**Files Modified**:
- `CMakeLists.txt`: Added `set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})`
- `CLAUDE.md`: Complete rewrite with development rules

#### 2. Build System Configuration
- Successfully configured custom board detection
- Resolved Kconfig circular dependency issues
- Fixed DTS schema validation errors
- Added power supply configuration (`&pwr`)

**Build Command Changed**:
```bash
# Old (overlay-based)
west build -b nucleo_h723zg apps/parp_01

# New (custom board)
west build -b nucleo_h723zg_parp01 apps/parp_01
```

#### 3. Hardware Configuration (DTS)
Key hardware settings in custom board definition:
- **SoC**: STM32H723ZG (ARM Cortex-M7)
- **System Clock**: 400 MHz (HSE 8MHz crystal → PLL)
  - VCO input: 2 MHz (M=4)
  - VCO output: 400 MHz (N=200)
  - SYSCLK: 400 MHz (P=1)
  - Peripheral clocks: 100 MHz (Q=4)
- **Clocks**: HSE 8MHz (crystal, not bypass), LSE 32.768kHz, HSI, HSI48
- **Power**: LDO supply
- **Console**: USART1 on PB14-TX/PB15-RX @ 115200 (TX/RX swapped)
- **UARTs**: UART4 (PD0/PD1), UART5 (PB5/PB6)
- **I2C**: I2C4 on PB7-SDA/PB8-SCL @ 400kHz
  - M24C64 EEPROM at address 0x50 (8KB, 32-byte pages)
- **USB**: USB OTG HS on PA11/PA12
- **LEDs**: PE2, PE3, PE6 (GPIO active-high)
- **Switches**: PD10, PD11 (GPIO active-low with pull-up)
- **RTC**: Configured with LSE clock source
- **Backup SRAM**: Enabled
- **RNG**: Enabled

#### 4. Documentation Structure Created
Following reference project patterns:
- Updated `CLAUDE.md` with critical development rules:
  - Rule 1: Never modify Zephyr source code
  - Rule 2: Always use workspace virtual environment
  - Rule 3: Always build from workspace root
- Created `docs/` directory
- Created `docs/README.md` - Documentation index
- Created `docs/SESSION_NOTES.md` - This file

#### 5. Build Verification
**Build Status**: ✅ SUCCESS

```bash
cd /home/lyg/work/zephyr_ws/zephyrproject
.venv/bin/west build -b nucleo_h723zg_parp01 apps/parp_01 -p auto
```

**Build Results**:
- Flash usage: 61,244 bytes (5.84% of 1 MB)
- RAM usage: 11,584 bytes (3.54% of 320 KB)
- Build artifacts:
  - `build/zephyr/zephyr.bin` (60 KB - flash binary)
  - `build/zephyr/zephyr.elf` (1.5 MB - debug symbols)
  - `build/zephyr/zephyr.hex` (Intel HEX format)

### Issues Encountered and Resolved

1. **Board not recognized**
   - Solution: Added `set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})` to CMakeLists.txt

2. **DTS parse errors (pwm12, timers12)**
   - Solution: Removed references to peripherals not present in STM32H723

3. **Kconfig circular dependency**
   - Problem: `depends on SOC_STM32H723XX` + `select SOC_STM32H723XX`
   - Solution: Removed `depends`, kept only `select` (following official board pattern)

4. **ARCH not defined**
   - Problem: board.yml missing architecture field
   - Solution: Verified Zephyr 4.x doesn't use `arch:` field in board.yml

5. **Power supply error**
   - Problem: Missing `power-supply` property
   - Solution: Added `&pwr { power-supply = "ldo"; }` to DTS

6. **SOC_SERIES configuration error**
   - Problem: SOC_SERIES_STM32H7X is not user-configurable
   - Solution: Removed from _defconfig (auto-selected by Kconfig)

### Current Project State

#### Enabled Features
✅ Custom board definition (complete, not overlay)
✅ 400 MHz system clock with proper PLL configuration
✅ Console via USART1 (TX/RX swapped)
✅ Multiple UARTs (UART1, UART4, UART5)
✅ I2C4 bus with EEPROM device
✅ USB OTG HS configured
✅ GPIO (3 LEDs, 2 switches)
✅ RTC with LSE clock
✅ Backup SRAM
✅ RNG
✅ Build system fully functional

#### Disabled Peripherals
❌ USART2, USART3, LPUART1
❌ I2C1
❌ SPI1
❌ Ethernet (MAC, MDIO)
❌ SDMMC1
❌ FDCAN1

### Next Steps

#### Hardware Verification (CRITICAL)
🔧 **Flash firmware to board and verify operation**:
```bash
cd /home/lyg/work/zephyr_ws/zephyrproject
.venv/bin/west flash
```

Expected behavior after flashing:
1. Board should boot successfully
2. Console output on USART1 (PB14-TX, 115200 baud)
3. System running at 400 MHz

**Test Points**:
- [ ] Console output visible and stable
- [ ] System clock running at 400 MHz (verify via printk timing)
- [ ] No hard faults or clock-related crashes
- [ ] GPIO LEDs controllable
- [ ] Switches readable

#### If Hardware Test Fails
1. Check console connection (PB14/PB15, TX/RX swapped)
2. Verify HSE crystal is 8MHz (not bypass mode)
3. Check power supply (should be LDO, not SMPS)
4. Review clock configuration if system doesn't start
5. Try lowering clock to 200MHz for stability testing

#### Future Development Tasks
1. **Application Development**:
   - Create LED blink demo
   - Implement button interrupt handlers
   - Test I2C EEPROM read/write
   - Test USB functionality

2. **Peripheral Testing**:
   - UART4, UART5 communication tests
   - I2C bus scan and EEPROM verification
   - RTC configuration and timekeeping
   - USB enumeration test

3. **Documentation**:
   - Hardware pin mapping document
   - Peripheral configuration guide
   - Troubleshooting guide

### Build Instructions Reminder

```bash
# From workspace root
cd /home/lyg/work/zephyr_ws/zephyrproject

# Activate virtual environment
source .venv/bin/activate

# Build
west build -b nucleo_h723zg_parp01 apps/parp_01

# Clean build
west build -b nucleo_h723zg_parp01 apps/parp_01 -p auto

# Flash
west flash
```

### Key Lessons Learned

1. **Custom Board vs Overlay**:
   - Overlay approach can cause subtle hardware conflicts
   - Complete board definition provides full control
   - Worth the extra files for reliability

2. **Kconfig Dependencies**:
   - Board Kconfig should only `select` the SoC, not `depends on` it
   - Avoid circular dependencies in Kconfig

3. **Virtual Environment Critical**:
   - All west commands require workspace venv
   - Document this prominently for future sessions

4. **Documentation Standards**:
   - Follow reference project structure
   - Keep comprehensive session notes
   - Document development rules explicitly

### Reference Materials
- Reference project: `apps/STM32F469I-DISCO_20260103-01/`
- Zephyr board definition docs: https://docs.zephyrproject.org/latest/hardware/porting/board_porting.html
- STM32H723 datasheet for pin/clock verification
