<!--
SPDX-License-Identifier: MIT

Copyright 2026 Ned Konz <ned@metamagix.tech>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
-->

# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

This is a MC6808 emulator for the Raspberry Pi 2350 (RP2350), designed to replace the MC6808 processor in early 1980s Williams and Bally pinball machines. The emulator is instruction-compatible with the MC6800 and replaces both the processor and EPROMs.

## Build System

This project uses the Raspberry Pi Pico SDK for RP2350 development:

- `PICO_SDK_PATH`: /Users/ned/src/Micropython/micropython/lib/pico-sdk
- TinyUSB path: /Users/ned/src/Micropython/micropython/lib/tinyusb/src/portable/raspberrypi/rp2040

To build, use the Makefile (which calls Cmake):

```bash
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

- PIO can generate the E clock
- The RP2350 has the ability to read all the GPIO pin states at once. This could be used instead of DMA and PIO.
- The emulator must execute cycle-accurately. So if a MC6800 instruction takes 4 cycles, the emulator must perform the same 4 cycles and finish before the 5th E clock.
- Any memory addresses outside of defined RAM and ROM ranges must be handled via the bus (for memory-mapped peripherals).
- It is not necessary to drive the bus for ROM and RAM accesses but it must be cycle accurate anyway.
- I want to use a dev board for testing at first.
- On system 7 games, the RAM space 0000-00FF is mirrored at 1000-10FF. And there could be RAM all the way up to 13FF.
- There could be ROM down to 0x4000
- We don't have any physical hardware other than the PIAs to worry about. They use only A10-A14 for address decoding and A0 and A1 for register select. We are emulating all the ROM and RAM.
