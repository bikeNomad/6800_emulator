# MC6800 Emulator for RP2350

A cycle-accurate MC6800/MC6808 emulator running on the Raspberry Pi RP2350 microcontroller.
Designed to replace the MC6800/MC6808 processor and EPROMs in early 1980s Williams, Stern and Bally pinball machines,
providing hardware-level compatibility with vintage MC6800-based systems.

## Key Features

- **Cycle-Accurate Emulation**: Every MC6800 instruction executes in exactly the correct number of E clock cycles
- **Hardware Bus Interface**: Physical GPIO connections allow interfacing with real MC6800 peripheral chips (PIAs, etc.)
- **Auto-Configuration**: Memory fingerprinting automatically detects and configures system architecture
- **Custom Hardware**: Designed for Ned's System 7 Board (RP2350B with 48 GPIO)
- **Full Address Bus**: Complete 16-bit address bus (A0-A15) for full 64KB address space
- **External Clock Support**: Auto-detect and synchronize to external E clock source
- **USB CDC Interface**: Command-line control, Intel HEX file loading, and comprehensive diagnostics
- **Web Interface**: Browser-based GUI for easy ROM loading and monitoring
- **Debug Features**: SPI debug output, UART console, LED indicators (ROM/RAM/unmapped access)
- **Flash-Based Storage**: Persistent flash-based storage for ROM (up to 48KB).

## Hardware Platform

- **Microcontroller**: Raspberry Pi RP2350B (dual Cortex-M33 cores, 48 GPIO package)
- **Board**: Ned's System 7 Board (BOARD_NED_SYS7)
- **Memory**: 520KiB SRAM, 16MiB Flash, 8MiB PSRAM
- **System Clock**: Configurable 150-300MHz (default: 266MHz for 133MHz QSPI flash)
- **Bus Interface**: 8-bit data bus, full 16-bit address bus, complete control signals
- **Clock Generation**: PIO-based E clock at 894.886 kHz (3.579545 MHz / 4) or external clock input

## Target System Memory Architecture

The emulator automatically detects and configures memory regions for the following architectures:

- **Early Bally** (AS-2518-17/35): ROM down to $D000, CMOS write-through to bus
- **Stern MPU-100/200**: Similar to Early Bally architecture
- **Williams System 3/6**: Compatible memory mapping
- **Williams System 7**: 5KB RAM ($0000-$13FF) with mirroring at $1000-$10FF
- **Williams System 9**: Extended memory configuration
- **Williams System 11**: Up to 48KB ROM support

### Memory Configuration

- **RAM**: Up to 8KB shadow RAM with configurable base address and aliasing support
- **ROM**: Up to 48KB flash-backed storage (minimum address $4000)
- **Bus Access**: Unmapped addresses route to physical GPIO bus for peripheral chips (PIAs, etc.)
- **CMOS**: Handled as unmapped, with write-through to target hardware

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
- Auto-detection of external E clock input (MC6800 systems)
- Automatic memory fingerprinting and architecture detection
- Support for Early Bally, Stern MPU-100/200, Williams System 3/6/7/9/11
- USB CDC command interface with Intel HEX loading
- Optional Web-based control interface
- Flexible memory configuration with address aliasing
- Flash-backed ROM (up to 48KB) storage
- LED indicators for memory access visualization (NED_SYS7 board)
- Optional SPI debug output for logic analyzer
- Breakpoint system with up to 16 breakpoints
- Comprehensive diagnostics and memory dump commands

🔄 **In Development**:

- Support for banked memory systems
- Support for faster (2MHz) E clocks
- MC6809 processor emulation
- PSRAM utilization for extended storage

## Quick Start

For detailed setup and usage instructions, see the documentation:

- **[Getting Started Guide](doc/Getting-Started.md)** - Complete setup, building, and first program
- **[Web Interface Guide](doc/Web-Interface.md)** - Browser-based control panel (easiest method)
- **[Hardware Connection](doc/Hardware-Connection.md)** - Pin assignments and wiring
- **[USB Commands Reference](doc/USB-Commands.md)** - Command-line interface
- **[Memory Configuration](doc/Memory-Map.md)** - Memory layout and configuration
- **[Architecture Overview](doc/Architecture.md)** - System design and implementation

### Quick Build

```bash
git clone https://github.com/bikeNomad/6800_emulator.git
cd 6800_emulator
make
# Flash images/BOARD_NED_SYS7.uf2 to device in BOOTSEL mode
```

Build options: `SYS_CLOCK_MHZ=266 QSPI_CLOCK_DIVISOR=2 DEBUG_INTERRUPTS=0`

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

### Bus Interface

- **Data Bus**: 8 lines (bidirectional)
- **Address Bus**: 16-bit (A0-A15 on GPIO 8-23)
- **Control Signals**:
  - `VMA` (Valid Memory Address) output
  - `R/W` (Read/Write) output
  - E clock output (PIO-generated) or input (external clock)
- **Interrupt Inputs**: `/IRQ`, `/NMI`, `/RESET` (active low)

### GPIO Pin Assignments (BOARD_NED_SYS7)

**Data Bus** (GPIO 0-7):

- D0-D7: Bidirectional 8-bit data bus

**Address Bus** (GPIO 8-23):

- A0-A15: Full 16-bit address bus

**Control Signals**:

- GPIO 24: E clock (output or input for external clock)
- GPIO 25: VMA (Valid Memory Address)
- GPIO 26: R/W (Read/Write)

**Interrupts**:

- GPIO 27: /IRQ (active low)
- GPIO 28: /NMI (active low)
- GPIO 29: /RESET (active low)

**LED Indicators**:

- GPIO 37: ROM access (green, active low)
- GPIO 38: RAM access (red, active low)
- GPIO 39: Unmapped/bus access (yellow, active low)

### Clock Generation

- **E Clock Frequency**: 894.886 kHz (3.579545 MHz ÷ 4) or external input
- **Internal Mode**: PIO-based generation for jitter-free timing
- **External Mode**: Auto-detect external E clock input
- **Synchronization**: PIO-based cycle counting ensures cycle-accurate execution

### Memory Subsystem

- **ROM Storage**: Up to 48KB in flash (minimum address $4000)
- **RAM Storage**: Up to 8KB shadow RAM with configurable base address
- **CMOS Storage**: 256 bytes persistent flash with write-through to bus
- **Address Aliasing**: Supports incomplete address decoding (e.g., System 7 RAM mirroring)
- **Memory Types**: ROM, RAM, Unmapped (routed to GPIO bus)

### USB Interface

- **Device Class**: CDC (Communications Device Class) serial port
- **Baud Rate**: Virtual (USB full-speed)
- **Features**:
  - Intel HEX file loading
  - Interactive command shell
  - Memory read/write/dump commands
  - Breakpoint control
  - System configuration and status

### Debug Interfaces

- **UART**: Hardware serial output for diagnostics (115200 baud, GPIO 40/41)
- **SPI**: Optional clocked debug stream with PC, R/W, and data bus values (GPIO 33-36)
- **LED Indicators**: Visual feedback for memory access patterns (GPIO 37-39)

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

### Supported System Architectures

The emulator auto-detects and supports:

- **Early Bally** (AS-2518-17, AS-2518-35): ROM from $D000, CMOS write-through
- **Stern MPU-100/200**: Early Bally-compatible configuration
- **Williams System 3/6**: Classic Williams architecture
- **Williams System 7**: 5KB RAM with mirroring, 16KB ROM
- **Williams System 9**: Extended ROM support
- **Williams System 11**: Up to 48KB ROM

## Development

### Dependencies

- Raspberry Pi Pico SDK (1.5.0 or later)
- CMake 3.13 or later
- GCC ARM cross-compiler
- TinyUSB (included with Pico SDK)

### Build Configuration

Key build options:

- `SYS_CLOCK_MHZ`: System clock frequency (150-300 MHz, default: 266)
- `QSPI_CLOCK_DIVISOR`: Flash access speed divisor (1-4, default: 2)
- `DEBUG_INTERRUPTS`: Enable interrupt debug output (0 or 1, default: 0)

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
