# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a Zephyr RTOS application named `parp_01` for a custom board based on STM32H723ZG. The project includes a custom board definition and uses a complete board structure (not overlay-based).

**Development Environment**: This project is part of a larger Zephyr workspace at `$HOME/work/zephyr_ws/zephyrproject`.

## CRITICAL: Development Rules

### Rule 1: Never Modify Zephyr Source Code
**⚠️ NEVER modify files in the Zephyr source tree (`../../zephyr/`)**

- ❌ Do NOT edit files in `zephyr/boards/`, `zephyr/soc/`, `zephyr/drivers/`
- ❌ Do NOT modify Zephyr core files
- ✅ Only modify files within this project directory (`apps/parp_01/`)
- ✅ All customizations must be in the project's custom board definition or application code

**Reason**: Zephyr is shared across multiple projects. Modifications would affect all projects in the workspace and would be lost on Zephyr updates.

### Rule 2: Always Use Workspace Virtual Environment
**⚠️ MUST use the virtual environment from workspace root**

```bash
# Option 1: Activate the venv (recommended)
cd $HOME/work/zephyr_ws/zephyrproject
source .venv/bin/activate
west build -b nucleo_h723zg_parp01 apps/parp_01

# Option 2: Use venv directly without activation
cd $HOME/work/zephyr_ws/zephyrproject
.venv/bin/west build -b nucleo_h723zg_parp01 apps/parp_01
```

**Why**: The virtual environment (`$HOME/work/zephyr_ws/zephyrproject/.venv/`) contains:
- `west` - Zephyr's meta-tool
- Python dependencies for the build system
- CMake integration tools

Without the venv, all west commands will fail.

### Rule 3: Always Build from Workspace Root
**⚠️ Build commands MUST be run from `$HOME/work/zephyr_ws/zephyrproject`**

```bash
# ✅ CORRECT
cd $HOME/work/zephyr_ws/zephyrproject
.venv/bin/west build -b nucleo_h723zg_parp01 apps/parp_01

# ❌ WRONG - do not build from app directory
cd apps/parp_01
west build -b nucleo_h723zg_parp01  # This will fail!
```

**Build Output**: All artifacts go to `$HOME/work/zephyr_ws/zephyrproject/build/`

## Build System

### Building the Application

```bash
# Navigate to workspace root
cd $HOME/work/zephyr_ws/zephyrproject

# Activate virtual environment
source .venv/bin/activate

# Build for custom PARP-01 board
west build -b nucleo_h723zg_parp01 apps/parp_01

# Clean build
west build -b nucleo_h723zg_parp01 apps/parp_01 -p auto

# Flash to device
west flash
```

**Note**: The board name is `nucleo_h723zg_parp01` (custom board), NOT `nucleo_h723zg` (stock Nucleo board).

### Flashing and Debugging

```bash
# Flash binary to board (requires ST-Link)
west flash

# Start debug session
west debug

# View build artifacts
ls build/zephyr/
# - zephyr.bin  (raw binary)
# - zephyr.hex  (Intel HEX format)
# - zephyr.elf  (ELF with debug symbols)
```

The board uses ST-Link for flashing and debugging. Configuration is in [boards/arm/nucleo_h723zg_parp01/board.cmake](boards/arm/nucleo_h723zg_parp01/board.cmake).

## Architecture

### Directory Structure

- `src/` - Application source code
  - `main.c` - Entry point
- `boards/arm/nucleo_h723zg_parp01/` - **Custom board definition**
  - `nucleo_h723zg_parp01.dts` - Device tree (hardware configuration)
  - `nucleo_h723zg_parp01_defconfig` - Board default Kconfig options
  - `Kconfig.nucleo_h723zg_parp01` - Board Kconfig entry
  - `Kconfig.defconfig` - Automatic configuration
  - `board.cmake` - Build-time runner configuration
  - `board.yml` - Board metadata
- `prj.conf` - Application-specific Kconfig configuration
- `CMakeLists.txt` - CMake build configuration (includes `BOARD_ROOT` setting)

### Custom Board Definition

This project uses a **complete custom board definition**, not an overlay. This provides:
- ✅ Full control over all hardware settings
- ✅ Independent from stock Nucleo board configuration
- ✅ Explicit clock and peripheral configuration
- ✅ No conflicts with base board settings

Key hardware configuration (from device tree):
- **SoC**: STM32H723ZG (ARM Cortex-M7)
- **Clock**: 400 MHz system clock (from 8 MHz HSE crystal via PLL)
- **Console**: USART1 on PB14/PB15 @ 115200 baud (TX/RX swapped)
- **LEDs**: PE2, PE3, PE6 (custom board LEDs)
- **Switches**: PD10, PD11 (custom board switches)
- **I2C**: I2C4 on PB7/PB8 with M24C64 EEPROM @ 0x50
- **USB**: USB OTG HS on PA11/PA12
- **Power**: LDO power supply configuration

### CMake Configuration

The `CMakeLists.txt` includes this critical line:
```cmake
set(BOARD_ROOT ${CMAKE_CURRENT_LIST_DIR})
```

This tells Zephyr to look for custom board definitions in this project directory.

### Configuration System

Zephyr uses Kconfig for configuration:
- [boards/arm/nucleo_h723zg_parp01/nucleo_h723zg_parp01_defconfig](boards/arm/nucleo_h723zg_parp01/nucleo_h723zg_parp01_defconfig) - Board-level defaults
- [prj.conf](prj.conf) - Application-level configuration

### Application Code

The current application ([src/main.c](src/main.c)) is a minimal bring-up application.

## Workspace Context

This application is part of a larger Zephyr workspace at `$HOME/work/zephyr_ws/zephyrproject/` which contains:
- `zephyr/` - Zephyr RTOS source tree (**DO NOT MODIFY**)
- `modules/` - Zephyr modules and HALs (**DO NOT MODIFY**)
- `apps/` - Application projects (this project is in `apps/parp_01/`)
  - `apps/STM32F469I-DISCO_20260103-01/` - Reference project with similar structure
- **`.venv/`** - **Python virtual environment with west and all build tools** (MUST be used)
- `.west/` - West workspace metadata
- `build/` - Build output directory

### Virtual Environment Requirements

**Critical**: This project cannot be built without using the workspace virtual environment:

```bash
# From workspace root
cd $HOME/work/zephyr_ws/zephyrproject
source .venv/bin/activate

# Or from project directory (relative path)
cd $HOME/work/zephyr_ws/zephyrproject/apps/parp_01
source ../../.venv/bin/activate
```

The virtual environment contains:
- `west` - Zephyr's meta-tool for building, flashing, and debugging
- Python dependencies required by the Zephyr build system
- CMake integration tools

Without activating the venv, west commands will fail with "command not found" errors.

## Device Tree and Hardware Modifications

When modifying hardware configuration:
1. Update [nucleo_h723zg_parp01.dts](boards/arm/nucleo_h723zg_parp01/nucleo_h723zg_parp01.dts) for peripheral instances and board-level hardware
2. Enable necessary Kconfig options in [prj.conf](prj.conf) (e.g., CONFIG_I2C, CONFIG_SPI)
3. Add corresponding driver calls in application code

**Important**: Never use overlay files (`.overlay`). All hardware configuration is in the complete board DTS file.

The device tree uses the STM32H723XG SoC definition from Zephyr's HAL as a base and defines all board-specific configuration explicitly.

## Documentation Management

### Directory Structure
- **Root directory**: Keep minimal - only essential project files
  - `CLAUDE.md` (this file)
  - `CMakeLists.txt`, `prj.conf` (build configs)
  - `.gitignore` (version control)
- **docs/**: All documentation files
  - `SESSION_NOTES.md` - Development session tracking (UPDATE REGULARLY)
  - `README.md` - Documentation index

### Session Tracking (IMPORTANT)
**Always update [docs/SESSION_NOTES.md](docs/SESSION_NOTES.md) at the end of each development session:**
- What was accomplished
- What issues were encountered
- What needs to be done next
- Build status and memory usage
- Files modified

This is **critical** for:
- Resuming work on different machines
- Tracking progress across sessions
- Understanding project history
- Onboarding other developers

### Documentation Guidelines
Create new documents in `docs/` for:
- Technical specifications
- How-to guides
- Architecture documentation
- Testing procedures

Keep the root directory clean - move detailed documentation to `docs/`.

## Reference Project

For best practices and similar patterns, refer to:
- **apps/STM32F469I-DISCO_20260103-01/** - Similar custom board project structure
  - Uses complete board definition (not overlay)
  - Comprehensive documentation in `docs/`
  - Proper session tracking in `SESSION_NOTES.md`
