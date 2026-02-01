# MC6800 Emulator for RP2350

A cycle-accurate MC6800/MC6808 emulator running on the Raspberry Pi RP2350 microcontroller.
Designed to replace the MC6800/MC6808 processor and EPROMs in early 1980s Williams, Stern and Bally pinball machines,
providing hardware-level compatibility with vintage MC6800-based systems.

## Key Features

- **Cycle-Accurate Emulation**: Every MC6800 instruction executes in exactly the correct number of E clock cycles
- **Hardware Bus Interface**: Physical GPIO connections allow interfacing with real MC6800 peripheral chips (PIAs, etc.)
- **Auto-Configuration**: Memory fingerprinting automatically detects and configures system architecture
- **Multiple Board Support**: Works with Raspberry Pi Pico 2 (26 GPIO) and Ned's System 7 Board (48 GPIO)
- **Flexible Address Decoding**: Full 16-bit addressing or optimized 7-line mode (A0, A1, A10-A14) for PIA access
- **External Clock Support**: Auto-detect and synchronize to external E clock source
- **USB CDC Interface**: Command-line control, Intel HEX file loading, and comprehensive diagnostics
- **Web Interface**: Browser-based GUI for easy ROM loading and monitoring
- **Debug Features**: SPI debug output, UART console, LED indicators (ROM/RAM/unmapped access)
- **Flash-Based Storage**: ROM (up to 48KB) and CMOS (256 bytes) with persistent flash storage

## Hardware Platform

- **Microcontroller**: Raspberry Pi RP2350 (dual Cortex-M33 cores)
- **Supported Boards**:
  - **Raspberry Pi Pico 2 W** (BOARD_PICO2): 26 GPIO pins, optimized address bus (A0, A1, A10-A14)
  - **Ned's System 7 Board** (BOARD_NED_SYS7): 48 GPIO pins, full 16-bit address bus, LED indicators
- **Memory**: 520KiB SRAM, 16MiB Flash, 8MiB PSRAM (NED_SYS7 only)
- **System Clock**: Configurable 150-300MHz (default: 266MHz for 133MHz QSPI flash)
- **Bus Interface**: 8-bit data bus, configurable address bus (7 or 16 lines), full control signals
- **Clock Generation**: PIO-based E clock at 894.886 kHz (3.579545 MHz / 4) or external clock input

## Target System Memory Architecture

The emulator automatically detects and configures memory regions for the following architectures:

- **Early Bally** (AS-2518-17/35): ROM down to $D000, CMOS write-through to bus
- **Stern MPU-100/200**: Similar to Early Bally architecture
- **Williams System 3/6**: Compatible memory mapping
- **Williams System 7**: 5KB RAM ($0000-$13FF) with mirroring at $1000-$10FF
- **Williams System 9**: Extended memory configuration
- **Williams System 11**: Up to 48KB ROM support
- **Williams WPC**: WPC pinball controller support

### Memory Configuration

- **RAM**: Up to 8KB shadow RAM with configurable base address and aliasing support
- **ROM**: Up to 48KB flash-backed storage (minimum address $4000)
- **CMOS**: 256 bytes persistent flash storage with write-through to target hardware
- **Bus Access**: Unmapped addresses route to physical GPIO bus for peripheral chips (PIAs, etc.)

## Software Architecture

The emulator consists of several key components:

1. **CPU Core** (`src/cpu_state.c`, `src/instructions.c`): MC6800 instruction execution with accurate cycle counting
2. **Memory System** (`src/memory.c`, `src/memory_map.c`): Flexible address translation with aliasing support
3. **Memory Fingerprinting** (`src/memory_fingerprint.c`): Auto-detection of system architecture and memory layout
4. **Bus Interface** (`src/bus.c`, `src/bus_cycle.pio`): PIO-accelerated GPIO operations for cycle-accurate timing
5. **Clock Management** (`src/clock.c`, `src/clock.pio`): E clock generation and external clock synchronization
6. **USB Interface** (`src/usb_cdc.c`): Dual-core design with dedicated USB CDC processing
7. **Debug System** (`src/debug_spi.c`): SPI output, LED indicators, and comprehensive diagnostics
8. **Interrupts** (`src/interrupts.c`): /IRQ, /NMI, and /RESET handling with proper vector support

## Development Status

✅ **Implemented Features**:

- Full MC6800 instruction set with cycle-accurate timing
- Physical bus interface with PIO acceleration
- External E clock input with auto-detection
- Multiple board support (Pico 2, NED_SYS7)
- Automatic memory fingerprinting and architecture detection
- Support for Early Bally, Stern MPU-100/200, Williams System 3/6/7/9/11, WPC
- USB CDC command interface with Intel HEX loading
- Web-based control interface with automatic IC detection
- Flexible memory configuration with address aliasing
- Flash-backed ROM (up to 48KB) and CMOS (256 bytes) storage
- LED indicators for memory access visualization (NED_SYS7)
- Optional SPI debug output for logic analyzer
- Breakpoint system with up to 16 breakpoints
- Comprehensive diagnostics and memory dump commands

🔄 **In Development**:

- Support for banked memory systems
- Support for faster (2MHz) E clocks
- MC6809 processor emulation
- 6502 processor emulation
- PSRAM utilization for extended storage

## Quick Start

### Prerequisites

- Raspberry Pi Pico SDK installed
- CMake and build tools
- Set `PICO_SDK_PATH` environment variable

### Building

1. **Clone the repository**:

   ```bash
   git clone https://github.com/bikeNomad/6800_emulator.git
   cd 6800_emulator
   ```

2. **Build the firmware**:

   ```bash
   mkdir build
   cd build
   cmake ..
   make
   ```

   **Build options** (pass to CMake with `-D`):
   - `BOARD_TYPE`: `BOARD_PICO2` or `BOARD_NED_SYS7` (default: `BOARD_NED_SYS7`)
   - `SYS_CLOCK_MHZ`: 150-300 (default: 266)
   - `QSPI_CLOCK_DIVISOR`: 1-4 (default: 2)
   - `DEBUG_INTERRUPTS`: 0 or 1 (default: 0)

   Example for Pico 2:
   ```bash
   cmake -DBOARD_TYPE=BOARD_PICO2 ..
   make
   ```

3. **Flash the device**:
   - Put RP2350 into bootloader mode (hold `BOOTSEL` while connecting USB or pressing `RESET`)
   - Copy `build/mc6800_emulator.uf2` to the `RP2350` USB drive

### First-Time Setup

1. **Connect via serial terminal**:

   ```bash
   screen /dev/tty.usbmodem14201 115200
   ```

2. **Auto-configure memory** (if connected to target system):

   ```
   scan
   ```

   This will detect the system architecture and configure memory regions automatically.

3. **Load ROM** (Intel HEX format):

   ```
   load
   [paste Intel HEX content]
   [blank line to finish]
   ```

4. **Run the emulator**:

   ```
   reset
   run
   ```

### Using the Web Interface

Open `web-interface/emulator-control.html` in a Chrome/Edge browser for a graphical control panel with:
- One-click ROM loading (HEX or binary files)
- Real-time CPU status monitoring
- Automatic IC number detection (System 7)
- Built-in terminal access

## Technical Specifications

### CPU Emulation

- **Registers**:
  - 16-bit PC (program counter)
  - 8-bit A and B accumulators
  - 16-bit SP (stack pointer)
  - 16-bit X (index register)
  - 8-bit CCR (condition codes: H I N Z V C, top 2 bits always 1)

- **Instruction Set**: Complete MC6800 instruction set with cycle-accurate timing
- **Cycle Accuracy**: Each instruction executes in exactly the correct number of E clock cycles
- **Performance Constraint**: All instructions must complete within 1.12µs per cycle

### Bus Interface

- **Data Bus**: 8 lines (bidirectional with level shifting)
- **Address Bus**:
  - **BOARD_NED_SYS7**: Full 16-bit (A0-A15 on GPIO 8-23)
  - **BOARD_PICO2**: Optimized 7-bit (A0, A1, A10-A14) for PIA access
- **Control Signals**:
  - VMA (Valid Memory Address) output
  - R/W (Read/Write) output
  - E clock output (PIO-generated) or input (external clock)
- **Interrupt Inputs**: /IRQ, /NMI, /RESET (active low)

### GPIO Pin Assignments

**Data Bus** (all boards):
- GPIO 0-7: D0-D7 (bidirectional)

**Address Bus**:
- **NED_SYS7**: GPIO 8-23 (A0-A15)
- **PICO2**: GPIO 8-14 (A0, A1, A10-A14)

**Control Signals**:
- **NED_SYS7**: E clock (GPIO 24), VMA (GPIO 25), R/W (GPIO 26)
- **PICO2**: E clock (GPIO 21), VMA (GPIO 22), R/W (GPIO 23)

**Interrupts** (all boards):
- GPIO 27: /IRQ
- GPIO 28: /NMI
- GPIO 29: /RESET

**LED Indicators** (NED_SYS7 only):
- GPIO 37: ROM access (green)
- GPIO 38: RAM/CMOS access (red)
- GPIO 39: Unmapped/bus access (yellow)

### Clock Generation

- **E Clock Frequency**: 894.886 kHz (3.579545 MHz ÷ 4)
- **Internal Mode**: PIO-based generation for jitter-free timing
- **External Mode**: Auto-detect external E clock input
- **Synchronization**: PIO-based cycle counting ensures cycle-accurate execution

### Memory Subsystem

- **ROM Storage**: Up to 48KB in flash (minimum address $4000)
- **RAM Storage**: Up to 8KB shadow RAM with configurable base address
- **CMOS Storage**: 256 bytes persistent flash with write-through to bus
- **Address Aliasing**: Supports incomplete address decoding (e.g., System 7 RAM mirroring)
- **Memory Types**: ROM, RAM, CMOS, Unmapped (routed to GPIO bus)

### USB Interface

- **Device Class**: CDC (Communications Device Class) serial port
- **Baud Rate**: Virtual (USB full-speed)
- **Features**:
  - Intel HEX file loading (ROM and CMOS)
  - Interactive command shell
  - Memory read/write/dump commands
  - Breakpoint control
  - System configuration and status

### Debug Interfaces

- **UART**: Hardware serial output for diagnostics (115200 baud)
- **SPI**: Optional clocked debug stream with PC, R/W, and data bus values
- **LED Indicators**: Visual feedback for memory access patterns (NED_SYS7)

## Command Reference

The USB CDC interface provides an interactive command shell with the following commands:

### System Control
- `run` - Start emulator execution
- `halt` - Stop emulator execution
- `reset` - Reset CPU to initial state (vector from $FFFE)
- `step [count]` - Execute one or more instructions
- `status` - Display CPU registers and state

### Memory Operations
- `dump <start> [end]` - Display memory contents in hex/ASCII
- `write <addr> <byte> [byte...]` - Write bytes to memory
- `read <addr> [length]` - Read and display memory
- `fill <start> <end> <value>` - Fill memory range with value
- `checksum <start> <end>` - Calculate checksum of memory range

### ROM/CMOS Loading
- `load` - Load Intel HEX file (paste content, blank line to finish)
- `load_cmos` - Load CMOS data from Intel HEX
- `save_cmos` - Write CMOS shadow to flash storage
- `copy_roms` - Copy ROM contents from physical bus to flash

### Memory Configuration
- `map` - Display current memory map
- `map ram <base> <size>` - Configure RAM region
- `map rom <base> <size>` - Configure ROM region
- `map clear` - Clear all memory mappings
- `scan` - Auto-detect system architecture and configure memory

### Breakpoints
- `break <addr>` - Set breakpoint at address
- `break list` - List all breakpoints
- `break clear [addr]` - Clear breakpoint(s)

### Clock Control
- `clock internal` - Use PIO-generated E clock
- `clock external` - Use external E clock input
- `clock status` - Display clock mode and cycle counters

### Debugging
- `trace [on|off]` - Enable/disable instruction trace
- `spi [on|off]` - Enable/disable SPI debug output
- `help` - Display command help

## Web Interface

A browser-based control panel is available in `web-interface/emulator-control.html`:

- **WebSerial Connection**: Direct USB CDC access from Chrome/Edge browsers
- **Real-Time Monitoring**: Live CPU status and cycle counters
- **ROM Loading**: Drag-and-drop support for Intel HEX and binary files
- **IC Detection**: Automatic IC number detection for Williams System 7
- **Built-in Terminal**: Full command-line access in the browser
- **Status Display**: Memory map, breakpoints, and system configuration

See [Web-Interface.md](Web-Interface.md) for detailed documentation.

## Architecture Details

### Code Organization

The firmware is organized into functional modules:

- **`src/main.c`**: System initialization and dual-core coordination
- **`src/emulator.c`**: Main emulation loop and state machine
- **`src/cpu_state.c`**: CPU register state and management
- **`src/instructions.c`**: MC6800 instruction decode and execution
- **`src/interrupts.c`**: Interrupt handling (/IRQ, /NMI, /RESET)
- **`src/memory.c`**: Memory subsystem with ROM/RAM shadow copies
- **`src/memory_map.c`**: Memory region mapping and address translation
- **`src/memory_fingerprint.c`**: Architecture auto-detection
- **`src/bus.c`**: GPIO bus interface and PIO coordination
- **`src/clock.c`**: E clock generation and cycle management
- **`src/usb_cdc.c`**: USB CDC device and command shell
- **`src/debug_spi.c`**: SPI debug output for logic analyzers

### PIO Programs

- **`src/clock.pio`**: E clock generation and cycle counting
- **`src/bus_cycle.pio`**: Cycle-accurate bus read/write operations

### Performance Optimization

The emulator uses several techniques to achieve cycle-accurate timing:

1. **Shadow Memory**: ROM and RAM are cached in fast SRAM for zero-wait-state access
2. **PIO Acceleration**: Bus cycles use PIO state machines for precise timing
3. **Cycle Accounting**: Every instruction tracks and synchronizes E clock cycles
4. **Fast Path**: Internal memory accesses bypass GPIO bus for speed
5. **Address Aliasing**: Incomplete decoding handled via memory map entries

### Cycle Timing

Each MC6800 instruction must complete within the exact number of E clock cycles:
- Memory reads/writes to ROM/RAM: No bus delay (shadow copy access)
- Memory access to unmapped regions: Full bus cycle with PIO synchronization
- Instruction execution: Precise cycle counting with periodic synchronization
- Critical constraint: Each E clock cycle is 1.12µs (894.886 kHz)

### Supported System Architectures

The emulator auto-detects and supports:

- **Early Bally** (AS-2518-17, AS-2518-35): ROM from $D000, CMOS write-through
- **Stern MPU-100/200**: Early Bally-compatible configuration
- **Williams System 3/6**: Classic Williams architecture
- **Williams System 7**: 5KB RAM with mirroring, 16KB ROM
- **Williams System 9**: Extended ROM support
- **Williams System 11**: Up to 48KB ROM
- **Williams WPC**: WPC pinball platform

## Development

### Dependencies

- Raspberry Pi Pico SDK (1.5.0 or later)
- CMake 3.13 or later
- GCC ARM cross-compiler
- TinyUSB (included with Pico SDK)

### Build Configuration

Key CMake options:
- `BOARD_TYPE`: Select target board
- `SYS_CLOCK_MHZ`: System clock frequency
- `QSPI_CLOCK_DIVISOR`: Flash access speed
- `DEBUG_INTERRUPTS`: Enable interrupt debug output

### Testing

The repository includes test programs in `tests/`:
- `test_program_2.asm`: Basic instruction test
- `test_program_2.hex`: Assembled Intel HEX format

### Contributing

Contributions are welcome! Key areas for development:
- Additional processor support (MC6809, 6502)
- Banked memory systems
- Faster E clock modes (2 MHz+)
- Enhanced debugging features

## License

See LICENSE file for details.

## Credits

Developed by Ned Konz for the vintage pinball community.

Uses the Raspberry Pi Pico SDK and TinyUSB library.
