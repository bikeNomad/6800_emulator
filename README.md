# MC6800 Emulator for RP2350

A cycle-accurate MC6802/MC6808 emulator running on the Raspberry Pi RP2350 microcontroller.
Designed to replace the MC6808 processor and EPROMs in early 1980s Williams, Stern and Bally pinball machines,
providing hardware-level compatibility with vintage MC6800-based systems.

## Key Features

- **Cycle-Accurate Emulation**: Every MC6800 instruction executes in exactly the correct number of E clock cycles
- **Hardware Bus Interface**: Physical GPIO connections allow interfacing with real MC6800 peripheral chips (PIAs, etc.)
- **Flexible Memory System**: Configurable RAM/ROM regions with write-through to target CMOS storage
- **USB CDC Interface**: Command-line control and Intel HEX file loading
- **Web Interface**: Browser-based GUI for easy ROM loading and monitoring
- **Debug Features**: SPI and UART debug output, LED indicators, and comprehensive diagnostics
- **ROM Mapping**: On-demand page-based ROM mapping for efficient flash access

## Hardware Platform

- **Microcontroller**: Raspberry Pi RP2350B (dual Cortex-M33 cores)
- **Supported Board**: Ned's System 7 Board (BOARD_NED_SYS7) with 48 GPIO pins
- **Memory**: 520KiB SRAM, 16MiB Flash, 8MiB PSRAM (configurable system clock: 150-300MHz)
- **Bus Interface**: 16-bit address bus, 8-bit data bus, full control signals
- **Clock Generation**: PIO-based E clock at 894.886 kHz (Williams System 7 compatible)

## Target System Memory Architecture

- **RAM**: 5KB shadow RAM ($0000-$13FF) with Williams System 7 mirroring
- **ROM**: 16KB flash storage ($4000-$7FFF) with page-based mapping
- **CMOS**: 256 bytes persistent flash storage ($0100-$01FF) with auto-save
- **Bus Access**: Unmapped addresses route to physical GPIO bus for peripherals

## Software Architecture

The emulator consists of several key components:

1. **CPU Core**: MC6800 instruction execution with accurate cycle counting
2. **Memory System**: Flexible address translation and address line masking
3. **Bus Interface**: PIO-accelerated GPIO operations for cycle-accurate timing
4. **USB Interface**: Dual-core design with dedicated USB CDC processing
5. **Debug System**: SPI output, LED indicators, and comprehensive diagnostics

## Development Status

✅ **Implemented Features**:

- Full MC6800 instruction set with cycle-accurate timing
- Physical bus interface with PIO acceleration
- USB CDC command interface
- Web-based control interface
- Flexible memory configuration
- ROM page mapping system
- CMOS write-through to target memory
- LED indicators for memory access visualization
- Optional SPI debug output for logic analyzer
- Breakpoint system

🔄 **In Development**:

- Flexible address decoding support for other board architectures
- Support for banked memory
- Support for faster (2MHz) clocks
- Support of MC6809 processor
- Support of 6502 processor

## Quick Start

1. **Build the firmware**:

   ```bash
   git clone https://github.com/bikeNomad/6800_emulator.git
   cd 6800_emulator
   make
   ```

2. **Flash the device**:
   - Put RP2350 into bootloader mode (hold `BOOT` while pressing `RESET`)
   - Drag `mc6800_emulator.uf2` to the `RP2350` drive

3. **Connect and test**:

   ```bash
   screen /dev/tty.usbmodem14201
   help
   load
   [paste your HEX file]
   reset
   run
   ```

## Technical Specifications

**CPU Emulation**:

- 16-bit PC (program counter)
- 8-bit A and B accumulators
- 16-bit SP (stack pointer)
- 16-bit X (index register)
- 8-bit CCR (condition codes: H I N Z V C)

**Bus Interface**:

- 8 data lines (bidirectional with level shifting)
- 16 address lines (full 64KB address space)
- Control signals: VMA, E clock, R/W
- Interrupt inputs: /IRQ, /NMI, /RESET

**Clock Generation**:

- E clock: 894.886 kHz (Williams System 7 standard)
- PIO-based generation for jitter-free timing
- Cycle-accurate instruction execution

**USB Interface**:

- CDC serial device for command interface
- Intel HEX file loading for ROM and CMOS
- Interactive diagnostics and memory access

The emulator must emulate all the documented MC6800 instructions, and will include a structure that holds at least:

- the 16-bit PC register (program counter)
- the 8-bit A and B accumulators
- the 16-bit SP (stack pointer)
- the 16-bit X (index) register
- the 8-bit CCR (condition code) register, which includes these bits:
  - C (carry (from bit 7))
  - V (overflow)
  - Z (zero)
  - N (negative)
  - I (interrupt)
  - H (half-carry (from bit 3))
  - the two top bits are both 1

The code is in several sections:

- A general-purpose MC6800 emulator, which interfaces through the rest of the system via a simple API that includes:
  - Read byte or bytes from a given memory address (used by the emulator for reading instructions or data)
  - Write byte or bytes to a given memory address (used by the emulator for writing data)
  - Execute next instruction (used by the system to run the program); returns current PC address

- The code to connect the RP2350's GPIO and PIO peripherals to the target system. These connections include:
  - 8 data lines (bi-directional), plus a R/W output used to drive the level translators between the RP2350 pins and the TTL data bus
  - 16 address lines (outputs), plus a VMA (valid memory address) strobe that is used by peripherals and EPROM to latch addresses
  - An E clock, running at 3.579545/4 MHz, output to one pin and used to synchronize the processor execution.
  - /IRQ, /NMI, and /RESET inputs, which force the PC (Program Counter) to specific vectors

The emulator MUST execute each of the MC6808 instructions within 1.12µs (the period of the E clock).

The PIO and DMA peripherals MAY be used to speed bus operations, which include:
    - Generation of the E clock
    - Driving the address lines, VMA output, R/W output (high), and output to the data bus for writing bytes
    - Driving the address lines, VMA output, R/W output (low), and input from the data bus for reading bytes

The USB interface presents as a CDC device to the host PC for loading EPROM contents into RP2350 flash, as well as for interactive diagnostics.
These diagnostics include:

- Read memory
- Write memory
- Checksum memory range

A browser-based web interface (`web-interface/emulator-control.html`) provides a graphical control panel for the emulator, featuring:

- WebSerial USB connection
- Real-time CPU status monitoring
- One-click ROM loading (Intel HEX and binary files)
- Automatic IC number detection for System 7 boards
- Built-in terminal for command-line access
- See: [Web Interface Documentation](Web-Interface.md)

For debugging, a clocked serial data stream is available via a hardware SPI port that outputs for every instruction:
    - current PC value (16 bits)
    - R/W status (1 bit)
    - data bus value (8 bits)

The code uses the Raspberry Pi Pico SDK and TinyUSB for the USB support.
