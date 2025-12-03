# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a MC6808 emulator for the Raspberry Pi 2350 (RP2350), designed to replace the MC6808 processor in early 1980s Williams and Bally pinball machines. The emulator is instruction-compatible with the MC6800 and replaces both the processor and EPROMs.

**Critical Constraint**: Each MC6808 instruction MUST execute within 1.12µs (the period of the E clock at 3.579545/4 MHz).

## Build System

This project uses the Raspberry Pi Pico SDK for RP2350 development:
- `PICO_SDK_PATH`: /Users/ned/src/Micropython/micropython/lib/pico-sdk
- TinyUSB path: /Users/ned/src/Micropython/micropython/lib/tinyusb/src/portable/raspberrypi/rp2040

Standard Pico SDK build commands:
```bash
mkdir build
cd build
cmake ..
make
```

## Architecture

### Core Components

1. **MC6800 Emulator Core**
   - Implements all documented MC6800 instructions
   - Processor state includes:
     - 16-bit PC (program counter)
     - 8-bit A and B accumulators
     - 16-bit SP (stack pointer)
     - 16-bit X (index register)
     - 8-bit CCR (condition code register): C, V, Z, N, I, H flags (top 2 bits always 1)
   - Provides abstracted API:
     - Read byte(s) from memory address
     - Write byte(s) to memory address
     - Execute next instruction (returns current PC)

2. **Hardware Interface Layer (GPIO/PIO)**
   - **Data Bus**: 8 bi-directional data lines + R/W output for level translator control
   - **Address Bus**: 16 output lines + VMA (valid memory address) strobe
   - **E Clock**: 3.579545/4 MHz clock output for synchronization
   - **Control Inputs**: /IRQ, /NMI, /RESET (vector the PC to specific addresses)

3. **Memory Management**
   - On-chip flash: Stores EPROM code (loaded via USB)
   - On-chip RAM: Emulates internal and external target system RAM (typically <512 bytes)

4. **USB Interface (CDC Device)**
   - EPROM code loading
   - Interactive diagnostics:
     - Read memory
     - Write memory
     - Checksum memory range

5. **Debug Interface (SPI)**
   - Outputs per instruction:
     - Current PC value (16 bits)
     - R/W status (1 bit)
     - Data bus value (8 bits)

### Performance Optimization

PIO and DMA peripherals should be used to accelerate bus operations:
- E clock generation
- Address line driving with VMA and R/W outputs
- Data bus reading/writing operations
- The RP2350 has the ability to read all the GPIO pin states at once. This could be used instead of DMA and PIO.
- To clarify: the emulator must execute cycle-accurately. So if a MC6800 instruction takes 4 cycles, the emulator must perform the same 4 cycles and finish before the 5th E clock.
- Any memory addresses outside of defined ROM and ROM ranges must be handled via the bus (for memory-mapped peripherals).
- Every ROM or RAM access must also drive the address bus because there is watchdog circuitry examining bus activity.
- I want to use a dev board for testing at first.