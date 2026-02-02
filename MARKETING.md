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

# MC6800 Emulator - Marketing Guide

## Executive Summary

The MC6800 Emulator is a modern replacement for vintage Motorola MC6800/MC6808 processors, designed to breathe new life into classic 1980s pinball machines, arcade games, and other MC6800-based systems. Built on the powerful Raspberry Pi RP2350 microcontroller, this cycle-accurate emulator provides hardware-level compatibility while adding modern conveniences like USB programming, web-based control, and persistent flash storage.

**Key Differentiators:**

- Drop-in replacement for failed MC6800 processors
- Eliminates need for rare, expensive EPROMs
- Cycle-accurate emulation ensures perfect timing compatibility
- Browser-based interface makes ROM loading effortless
- Open-source design with active development

---

## For Pinball Machine Collectors and Restorers

### Bring Your Dead Pinball Machine Back to Life

If you own a Williams System 7, System 9, System 11, Stern MPU-100/200, or early Bally pinball machine with a dead processor or corrupted EPROMs, this emulator is your solution.

**Your Challenge:**

- Original MC6800 processors are increasingly rare and expensive
- EPROMs degrade over time, losing game code
- Finding matching ROM chips for your specific game revision is difficult
- Socketed chips fail from decades of thermal cycling
- One bad chip means a non-working machine worth thousands of dollars

**Our Solution:**

The MC6800 Emulator replaces both the processor AND the EPROMs in a single, modern package. Simply remove your old MC6800 and EPROMs, install the emulator board, and load your game ROMs via USB.

**What This Means for You:**

- **Get Your Game Running Today**: No more searching eBay for rare ROM chips
- **Multiple Game Support**: Store and switch between different game versions
- **Future-Proof**: Flash storage won't degrade like 40-year-old EPROMs
- **Easy Updates**: Fix bugs or try different ROM versions instantly via USB
- **Auto-Configuration**: Memory fingerprinting automatically detects your board type
- **Preserve Authenticity**: Interfaces with original PIAs, switches, and displays
- **Web Interface**: Load ROMs from your phone or laptop - no terminal required

**Supported Systems:**

- Williams System 3, 6, 7, 9, and 11
- Stern MPU-100 and MPU-200
- Early Bally (AS-2518-17, AS-2518-35)

**Real-World Example:**

Installing a Williams System 7 game (Black Knight, Jungle Lord, Pharaoh, etc.):

1. Remove old CPU and four ROM chips (IC26, IC14, IC20, IC17)
2. Install emulator board in CPU socket
3. Connect USB cable to laptop
4. Open web interface, drag and drop your ROM files
5. Click "Reset" and "Run"
6. Your game boots up exactly as it did in 1980

No EPROM programmer required. No waiting for chips to arrive. No guessing at addresses.

---

## For Vintage Arcade Cabinet Restorers

### Restore Classic Arcade Games with Confidence

Some golden-age arcade games used the MC6800 processor family (especially in sound boards). When these boards fail, restoration becomes a challenge. The MC6800 Emulator provides a modern, reliable alternative.

**Why Choose This Emulator:**

- **Cycle-Accurate Timing**: Timing-critical game logic works perfectly
- **Hardware Bus Interface**: Connects to original graphics, sound, and control hardware
- **External Clock Support**: Auto-detects and synchronizes to your board's crystal oscillator
- **Diagnostic Tools**: Built-in memory dump, register inspection, and breakpoints
- **SPI Debug Output**: Connect a logic analyzer to trace game execution

**For Board Repair Technicians:**

- Replace failed processors without finding exact vintage replacements
- Test and validate ROM sets before burning expensive EPROMs
- Diagnose board issues by isolating CPU vs peripheral problems
- Create working backups of rare game code

**Applications:**

- Replacement CPU for dead arcade boards
- Development platform for homebrew games
- ROM testing and validation
- Board troubleshooting and repair

---

## For Retro Computing Enthusiasts

### Experience Authentic MC6800 Computing

The MC6800 was one of the most important 8-bit processors of the 1970s, powering everything from the Motorola MEK6800D2 development kit to industrial control systems. This emulator lets you explore this historic architecture with modern tools.

**What Makes This Special:**

- **Authentic Instruction Set**: Complete MC6800 instruction implementation
- **Learn By Doing**: Write assembly code and see immediate results
- **Physical Hardware Interface**: Connect real MC6800 peripherals (6821 PIA, 6850 ACIA)
- **Modern Development Tools**: USB interface, web-based control, Intel HEX loading
- **No Vintage Hardware Required**: No need to find expensive development systems

**Educational Features:**

- **Instruction Counting**: See exactly how many times each instruction executes
- **Cycle Counting**: Verify your code meets timing requirements
- **Memory Inspection**: Read and write RAM/ROM in real-time
- **Breakpoint System**: Pause execution at specific addresses
- **SPI Debug Stream**: Watch every instruction execute on a logic analyzer

**Perfect For:**

- Learning MC6800 assembly language
- Understanding 1970s computer architecture
- Building retro computing projects
- Recreating classic systems like the SWTPC 6800 or Motorola MEK6800D2
- Interfacing with vintage peripherals

**Example Projects:**

- Build a minimal MC6800 computer on a breadboard
- Interface with period-correct peripherals
- Run original MIKBUG or SWTBUG monitor programs
- Create custom control systems using MC6800 assembly

---

## For Students and Educators

### Teach Computer Architecture with Real Hardware

Computer science and electrical engineering students benefit from hands-on experience with processor architecture. The MC6800 Emulator provides an accessible, affordable platform for learning fundamental concepts.

**Why MC6800 for Education:**

- **Simple, Elegant Architecture**: Easier to understand than modern complex processors
- **Complete Documentation**: Full datasheet and instruction set available
- **Manageable Instruction Set**: ~72 instructions vs thousands in modern CPUs
- **Clear Addressing Modes**: Direct, extended, indexed, immediate, inherent
- **Real Hardware Interface**: Students see actual bus cycles, not just simulation

**Teaching Applications:**

**Assembly Language Programming:**

- Write, load, and execute MC6800 assembly code
- Understand registers, flags, and program flow
- Learn stack operations and subroutines
- Debug real programs with hardware tools

**Computer Architecture:**

- Observe address and data bus operation
- Understand memory-mapped I/O
- Explore interrupt handling (IRQ, NMI, RESET)
- Measure instruction cycle timing

**Embedded Systems:**

- Interface with real peripheral chips
- Implement hardware control systems
- Work with ROM, RAM, and CMOS memory
- Develop timing-critical applications

**Digital Logic:**

- Connect bus signals to oscilloscope or logic analyzer
- Verify timing diagrams from datasheets
- Understand control signals (VMA, R/W, E clock)
- Debug hardware issues

**Advantages Over Simulation:**

- Real hardware teaches real-world constraints
- Physical debugging with test equipment
- Instant feedback from actual execution
- No simulation artifacts or incorrect timing
- Prepares students for professional embedded work

**Classroom-Friendly:**

- USB-powered, no special power supply
- Web interface works on student laptops
- Open-source design for modification and learning
- Comprehensive documentation included
- Affordable compared to vintage hardware

---

## For Professional Engineers and Developers

### Maintain and Modernize Legacy Systems

Many industrial control systems, medical devices, and specialized equipment still use MC6800-based controllers. When these systems fail, replacement parts are scarce and expensive.

**Professional Applications:**

**Legacy System Maintenance:**

- Replace failed processors in industrial equipment
- Create backup systems for critical machinery
- Extend life of irreplaceable control systems
- Eliminate dependency on NOS (New Old Stock) components

**Reverse Engineering:**

- Extract and analyze ROM code from vintage systems
- Verify instruction-level behavior
- Document undocumented systems
- Create replacement firmware

**Product Development:**

- Prototype MC6800-compatible systems
- Test peripheral integration
- Validate timing requirements
- Develop replacement boards for discontinued products

**Technical Advantages:**

- **Cycle-Accurate Emulation**: Exact timing matches original hardware
- **Full Address Space**: 64KB addressing with complete bus (NED_SYS7 board)
- **Diagnostic Tools**: Memory dump, breakpoints, instruction counting
- **Command-Line Interface**: Scriptable via USB CDC serial
- **Flash Storage**: Persistent ROM with 10,000+ write cycles
- **Debug Output**: SPI stream for logic analyzer integration
- **Open Source**: Full source code available for customization

**Integration Features:**

- Intel HEX file support for standard tool compatibility
- Automatic memory map configuration
- External clock input for synchronization
- Hardware interrupt inputs (IRQ, NMI, RESET)

**Development Workflow:**

1. Write code using standard MC6800 assembler
2. Generate Intel HEX file
3. Load via USB (command-line or web interface)
4. Test with real peripherals
5. Debug with breakpoints and memory inspection
6. Deploy with flash-backed persistence

---

## For Makers and Hardware Hackers

### Build Amazing Projects with Vintage Technology

The MC6800 represents the golden age of hackable hardware - simple enough to understand completely, powerful enough to do real work. This emulator brings that spirit into the modern era.

**Hacker-Friendly Features:**

- **Complete GPIO Access**: Every bus signal available on pins
- **Programmable I/O (PIO)**: Hardware-accelerated bus cycles
- **Open Source**: Modify, extend, or fork the design
- **Modern MCU**: RP2350 with 266MHz ARM cores and 520KB RAM
- **Dual Core**: CPU emulation on core 0, USB on core 1
- **MicroPython Support**: Test and debug with Python on-device

**Project Ideas:**

**Retro Gaming:**

- Build custom pinball or arcade controllers
- Create homebrew games for vintage hardware
- Interface with original game peripherals

**Control Systems:**

- Home automation using vintage PIAs
- Robot controllers with MC6800 compatibility
- Custom MIDI instruments using period hardware

**Art and Music:**

- Generative art using MC6800 algorithms
- Vintage synthesizer control
- LED matrix displays driven by PIA chips

**Education and Workshops:**

- Teach computer fundamentals at makerspaces
- Demonstrate 1970s computing technology
- Build minimal computers on breadboards

**Technical Capabilities:**

- **8-bit Data Bus**: Bidirectional with configurable direction
- **16-bit Address Bus**: Full 64KB addressing (NED_SYS7)
- **E Clock Output**: 894.886 kHz (3.579545 MHz ÷ 4), PIO-generated
- **Control Signals**: VMA, R/W for peripheral interfacing
- **Interrupt Inputs**: IRQ, NMI, RESET (active low)
- **Debug Interface**: SPI, UART, USB CDC
- **LED Indicators**: Visual feedback for memory access

**Expansion Possibilities:**

- Add PSRAM for 8MB storage
- Connect multiple PIAs for GPIO expansion
- Interface with modern sensors via custom peripherals
- Create hybrid vintage/modern systems

**Developer Resources:**

- Complete source code in C
- Detailed architecture documentation
- PIO programs for bus timing
- MicroPython test modules
- Web interface source (single HTML file)
- Example programs included

---

## Key Features Overview

### Hardware

- **Processor**: Raspberry Pi RP2350B (dual Cortex-M33 @ 266MHz)
- **Memory**: 520KB SRAM, 16MB Flash, 8MB PSRAM interface
- **GPIO**: 48 pins on NED_SYS7 board
- **Bus Interface**: 8-bit data, 16-bit address, full control signals

### Emulation

- **Cycle-Accurate**: Every instruction takes exact MC6800 cycle count
- **Complete Instruction Set**: All 72 MC6800 instructions implemented
- **Hardware Timing**: PIO-generated E clock at 894.886 kHz
- **External Clock**: Auto-detect external clock input

### Memory Management

- **ROM Storage**: Up to 48KB in flash
- **RAM Shadow**: Up to 8KB with configurable mirroring
- **Address Aliasing**: Supports incomplete address decoding (System 7, etc.)

### Interfaces

- **USB CDC**: Virtual serial port for programming and control
- **Web Interface**: Browser-based control panel (Chrome/Edge)
- **UART Debug**: Hardware serial output at 115200 baud
- **SPI Debug**: Logic analyzer interface

### Software Tools

- **Intel HEX Loading**: Standard format support
- **Auto-Configuration**: Memory fingerprinting detects system type
- **Breakpoints**: Up to 16 hardware breakpoints
- **Memory Commands**: Read, write, dump, checksum
- **Instruction Counting**: Execution profiling

### Supported Systems

- Williams System 3, 6, 7, 9, 11
- Stern MPU-100, MPU-200
- Early Bally (AS-2518-17, AS-2518-35)
- Custom/homebrew systems

---

## Getting Started is Easy

### Quick Start (5 Minutes)

1. **Flash Firmware**
   - Download UF2 file
   - Put board in bootloader mode (hold BOOTSEL, press RESET)
   - Drag UF2 file to USB drive
   - Board reboots automatically

2. **Connect via USB**
   - Any serial terminal works
   - Or use the web interface (Chrome/Edge)

3. **Load Your Program**
   - Paste Intel HEX file, or
   - Upload binary ROM via web interface
   - Auto-detection handles ROM vs CMOS

4. **Run**
   - Click "Reset" to initialize CPU
   - Click "Run" to start execution
   - Watch status update in real-time

### No Special Tools Required

- No EPROM programmer needed
- No assembly toolchain required (use any MC6800 assembler)
- No debugger hardware needed
- Works with any modern computer (Windows, Mac, Linux)

---

## Why Choose This Emulator?

### Compared to Original Hardware

| Feature | Original MC6800 | This Emulator |
|---------|----------------|---------------|
| **Availability** | Increasingly rare | Available now |
| **EPROM Replacement** | Need programmer | USB upload |
| **ROM Storage** | Physical chips | Flash (48KB) |
| **Programming** | EPROM burner | Drag and drop |
| **Debugging** | External tools | Built-in |
| **Cost** | $20-100+ (NOS) | Open source |

### Compared to Software Emulation

| Feature | Software Simulator | Hardware Emulator |
|---------|-------------------|-------------------|
| **Physical Interface** | No | Yes - real bus |
| **Peripheral Support** | Limited | Full - real PIAs |
| **Timing Accuracy** | Approximate | Cycle-perfect |
| **CPU Replacement** | No | Yes |
| **Learning Value** | Moderate | High - real HW |
| **Debugging** | Virtual only | Hardware + software |

---

## Pricing and Availability

### Open Source Project

This is an **open-source hardware and software project**:

- **Hardware Design**: Available for custom board fabrication
- **Firmware**: Full C source code included
- **Documentation**: Comprehensive guides and specifications
- **License**: See LICENSE file for details

### Build Your Own

All files needed to build your own emulator:

- Schematics and PCB layouts (NED_SYS7 board)
- Firmware source code
- Build instructions
- BOM (Bill of Materials)

### Pre-Built Boards

Contact the developer for information about pre-built boards.

---

## Technical Support and Community

### Documentation

- Getting Started Guide
- Architecture Overview
- Hardware Connection Guide
- USB Commands Reference
- Memory Configuration Guide
- PIO Bus Cycles Documentation
- Web Interface Guide

### Example Code

- Test programs in assembly
- Intel HEX samples
- MicroPython test modules
- C firmware source

### Development

- GitHub repository with full source
- Issue tracker for bug reports
- Active development and updates
- Community contributions welcome

---

## Specifications

### Processor Emulation

- **Architecture**: MC6800/MC6808 compatible
- **Registers**: PC, A, B, X, SP, CCR (all 8/16-bit per spec)
- **Instructions**: Complete 72-instruction set
- **Addressing**: Immediate, direct, extended, indexed, inherent, relative
- **Interrupts**: IRQ, NMI, RESET with vector support

### Timing

- **E Clock**: 894.886 kHz (Williams System 7) or external
- **Cycle Accuracy**: ±0 cycles (exact match to datasheet)
- **Bus Timing**: PIO-synchronized to E clock
- **Execution Speed**: Real-time or faster with internal memory

### Physical Interface

- **Data Bus**: 8 lines, bidirectional (GPIO 0-7)
- **Address Bus**: 16 lines, output (GPIO 8-23, NED_SYS7)
- **E Clock**: Output or input (GPIO 24)
- **VMA**: Valid Memory Address output (GPIO 25)
- **R/W**: Read/Write output (GPIO 26)
- **Interrupts**: /IRQ, /NMI, /RESET inputs (GPIO 27-29)

### Storage

- **ROM**: 48KB maximum (flash-backed)
- **RAM**: 8KB maximum (SRAM shadow)
- **Flash Endurance**: 10,000+ erase cycles

### Development Interface

- **USB**: CDC virtual serial port
- **UART**: 115200 baud debug output (GPIO 40-41)
- **SPI**: Debug stream for logic analyzer (GPIO 33-36)
- **Web**: Browser-based control panel

### Power

- **Supply**: 5V USB (500mA typical)
- **Consumption**: <200mA typical operation

### Physical

- **Board**: NED_SYS7 (RP2350B, 48-pin GPIO)
- **Mounting**: Standard DIP socket compatible
- **Indicators**: 3× LEDs (ROM, RAM, unmapped access)

---

## Use Cases Summary

### Pinball/Arcade Restoration

**Replace dead processors and EPROMs in vintage machines**

- Drop-in replacement saves expensive NOS parts
- Load ROMs via USB - no EPROM programmer needed
- Auto-configuration for Williams, Stern, Bally systems

### Retro Computing Projects

**Build and experiment with MC6800 systems**

- Connect real peripheral chips (PIAs, ACIAs)
- Learn assembly programming with modern tools
- Create custom vintage-style computers

### Education

**Teach computer architecture and assembly language**

- Hands-on learning with real hardware
- Debug with modern tools (USB, web interface)
- Affordable alternative to vintage development systems

### Professional Development

**Maintain legacy industrial systems**

- Replace failed processors in critical equipment
- Reverse-engineer undocumented systems
- Develop replacement boards

### Maker Projects

**Build creative projects with vintage tech**

- Robot controllers with MC6800 compatibility
- Custom gaming systems
- Retro art installations

---

## Next Steps

### Learn More

Read the comprehensive documentation:

- **Getting Started Guide** - First program in 5 minutes
- **Architecture Overview** - How it works
- **Web Interface Guide** - Browser-based control
- **USB Commands** - Complete reference

### Download

- Firmware UF2 files
- Source code (GitHub)
- Hardware designs
- Example programs

### Get Support

- Documentation (included)
- GitHub issues
- Community forums
- Direct developer contact

### Build or Buy

- Build your own from schematics
- Source components from BOM
- Contact developer for pre-built boards

---

## About the Developer

Developed by **Ned Konz** for the vintage pinball and retro computing community.

This project combines modern microcontroller technology with vintage computer architecture, making classic systems accessible to a new generation while providing powerful tools for restoration and development.

Built with:

- Raspberry Pi Pico SDK
- TinyUSB library
- Open-source tools and libraries

**Join the community of makers, restorers, and enthusiasts bringing vintage computing back to life.**

---

*MC6800 Emulator - Modern Tools for Classic Hardware*
