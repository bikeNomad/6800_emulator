# MC6800 Emulator Documentation

## Overview

Welcome to the MC6800 Emulator documentation. This collection covers everything you need to know about using, connecting, and understanding the cycle-accurate MC6800 hardware emulator running on the NED_SYS7 board.

## Documentation Guide

### 🚀 [Getting Started](Getting-Started.md)

**Start here if you're new to the emulator!**

Quick setup guide that gets you running in 5 minutes:

- Flash firmware
- Connect via USB
- Load and run your first program
- Basic debugging
- Example programs
- Troubleshooting

**Best for**: First-time users, quick reference

### 🏗️ [Architecture](Architecture.md)

**Deep dive into system design and implementation**

Complete architectural overview:

- Dual-core design (Core 0: CPU emulation, Core 1: USB)
- Memory subsystem (RAM, ROM, CMOS, Flash)
- Clock generation (PIO-based E clock)
- Bus interface and timing
- Interrupt handling
- Instruction execution
- Cycle accuracy

**Best for**: Understanding how it works, developers, contributors

### ⚡ [PIO Bus Cycles](PIO-Bus-Cycles.md)

**Hardware-timed bus operations using PIO**

PIO-based bus cycle implementation:

- Problem: Software polling timing issues
- Solution: Hardware-timed data sampling
- PIO state machine configuration
- Timing analysis (150ns data setup)
- API usage and debugging
- Comparison: polling vs PIO

**Best for**: Understanding bus timing, debugging data stability issues

### 🔌 [Hardware Connection](Hardware-Connection.md)

**Physical interfacing with target systems**

Everything about connecting to real hardware:

- Pin assignments (NED_SYS7 board)
- Level shifting (3.3V ↔ 5V)
- Connection scenarios:
  - Standalone operation
  - PIA connection
  - System replacement (CPU drop-in)
  - Williams System 7 pinball
- Troubleshooting hardware issues
- Safety precautions
- Test points and logic analyzer setup

**Best for**: Hardware integration, PIA interfacing, system replacement

### 🗺️ [Memory Map](Memory-Map.md)

**Complete target system memory layout and configuration**

Detailed memory architecture:

- Memory regions (RAM, ROM, CMOS, Unmapped)
- Address translation (A15 handling)
- Flash storage layout
- Memory access timing
- Special features (stack, zero page, mirroring)
- Configuration options
- Memory testing procedures

**Best for**: Understanding memory layout, debugging memory issues

### 🌐 [Web Interface](Web-Interface.md)

**Browser-based control panel**

Graphical interface for controlling the emulator:

- One-click connection via WebSerial API
- Real-time CPU status with auto-refresh
- Visual CPU control buttons (Run/Halt/Reset)
- Drag-and-drop ROM loading (HEX and binary)
- Automatic IC number detection (System 7)
- Built-in terminal for advanced commands
- No installation required (single HTML file)

**Best for**: Beginners, visual learners, quick ROM loading

### 💻 [USB Commands](USB-Commands.md)

**Complete command reference**

Full USB CDC interface documentation:

- All commands with syntax and examples
- Configuration commands
- Memory access (read/write)
- CPU control (run/halt/reset)
- CMOS management
- ROM loading (Intel HEX)
- Debugging tools
- Error messages and solutions

**Best for**: Daily use reference, scripting, automation, terminal users

## Quick Navigation

### By User Type

**First-Time User**:

1. [Getting Started](Getting-Started.md) → Flash and run
2. [Web Interface](Web-Interface.md) → Easy graphical control (recommended)
3. [Getting Started - Examples](Getting-Started.md#example-programs) → Try example programs
4. [USB Commands](USB-Commands.md) → Advanced command-line control

**Hardware Developer**:

1. Hardware Connection → Pin assignments
2. Memory Map → Memory layout
3. Architecture - Bus Interface → Timing diagrams

**System Integrator**:

1. Hardware Connection - Scenarios → Choose your setup
2. Memory Map → Configure memory
3. USB Commands → Control interface

**Software Developer**:

1. Architecture → System design
2. Memory Map → Memory model
3. Getting Started - Building → Compile from source

### By Task

**Loading Programs**:

- Web Interface - ROM Upload → Easiest method (recommended)
- Getting Started - Loading → Load your own programs
- USB Commands - load → Command-line ROM loading
- Memory Map - ROM → ROM storage details

**Connecting Hardware**:

- Hardware Connection - Scenarios → Choose your setup
- Hardware Connection - Pin Assignments → GPIO pin mapping
- Hardware Connection - Level Shifting → 3.3V ↔ 5V conversion

**Debugging Issues**:

- Getting Started - Debugging → Basic debugging workflow
- USB Commands - Error Messages → Command error handling
- Hardware Connection - Troubleshooting → Hardware issues

**Understanding Memory**:

- Memory Map → Memory layout overview
- Architecture - Memory Subsystem → Internal memory handling
- USB Commands - Memory Commands → Memory access commands

**Configuring System**:

- USB Commands - config → Configuration commands
- Memory Map - Configuration → Memory setup options
- Architecture - Board Abstraction → Board-specific features

## Features Overview

### Hardware

- **Platform**: NED_SYS7 board (RP2350)
- **CPU**: Dual Cortex-M33 @ 150MHz
- **Board**: BOARD_NED_SYS7

### Emulation

- **Cycle-Accurate**: Every instruction takes exact MC6800 cycle count
- **E Clock**: 894.886 kHz (Williams System 7), PIO-generated
- **Physical Bus**: Real hardware interfacing via GPIO
- **Interrupts**: IRQ, NMI, RESET fully supported

### Memory

- **RAM**: Up to 8KB (shadow, volatile)
- **ROM**: Up to 32KB (flash, persistent)
- **CMOS**: 256 bytes (flash, persistent, auto-save)
- **Address Space**: 64KB with A15 aliasing

### Interface

- **Web Interface**: Browser-based GUI (Chrome/Edge/Opera)
- **USB CDC**: Command-line interface via serial terminal
- **UART**: Debug output (GPIO 16-17)
- **SPI**: Debug trace (GPIO 18-19)
- **Intel HEX**: Standard program loading format

## Key Specifications

### Memory Map (Default)

```
$0000-$13FF: RAM (5KB)
$0100-$01FF:   └─ CMOS (persistent)
$1000-$10FF:   └─ Mirror ($0000-$00FF)
$1400-$4FFF: Unmapped (physical bus)
$5000-$7FFF: ROM (12KB)
$7FF8-$7FFF:   └─ Vectors
```

### Clock Timing

- **Frequency**: 894.886 kHz
- **Period**: 1.117 µs
- **Duty Cycle**: 50%
- **Source**: PIO state machine (jitter-free)

### Pin Count

| Board | GPIO | Data | Address | Total Pins |
|-------|------|------|---------|------------|
| NED_SYS7 | 48 | 8 | 16 | 24 |

## Common Tasks

### Load and Run Program

**Via Web Interface (Easiest)**:

1. Open `web-interface/emulator-control.html` in Chrome
2. Click "Connect to Emulator"
3. Choose ROM file (HEX or binary)
4. Click "Upload ROM"
5. Click "Reset" → "Run"

**Via USB Terminal**:

```bash
load
[paste HEX file]
reset
run
```

### Check Execution

```bash
halt
status
read 0100 10
```

### Save Configuration

```bash
write 0100 DE AD BE EF
cmos save
```

### Inspect Vectors

```bash
read 7FF8 8
```

## External Resources

### MC6800 Documentation

- [MC6800 Datasheet (Motorola)](https://www.nxp.com/docs/en/data-sheet/MC6800.pdf)
- [MC6800 Programming Manual](http://bitsavers.org/components/motorola/6800/MC6800_Programming_Manual_1975.pdf)
- [6821 PIA Datasheet](https://www.jameco.com/Jameco/Products/ProdDS/43596.pdf)

### RP2350 Documentation

- [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [Pico 2 W Pinout](https://datasheets.raspberrypi.com/pico/Pico-2-W-pinout.pdf)

### Tools

- [AS6800 Assembler](http://shop-pdp.net/ashtml/as6800.htm)
- [VASM Assembler](http://sun.hasenbraten.de/vasm/)
- [IntelHex Python Library](https://pypi.org/project/intelhex/)

### Example Systems

- [Williams System 7 Info](https://www.ipdb.org/)
- [MC6800 Projects](http://www.6800.org/)

## Document Versions

| Document | Last Updated | Version |
|----------|--------------|---------|
| Getting Started | 2024-12-05 | 1.0 |
| Architecture | 2024-12-05 | 1.0 |
| PIO Bus Cycles | 2024-12-18 | 1.0 |
| Hardware Connection | 2024-12-05 | 1.0 |
| Memory Map | 2024-12-05 | 1.0 |
| Web Interface | 2024-12-09 | 1.0 |
| USB Commands | 2024-12-09 | 1.1 |

## Contributing

Found an error? Have a suggestion? See the main project README for contribution guidelines.

## License

See project LICENSE file.

---

## Need Help?

1. **Start with**: Getting Started
2. **Can't find it?**: Search all docs (Ctrl+F / Cmd+F)
3. **Still stuck?**: Check Troubleshooting sections
4. **Report issue**: File a bug on GitHub

Happy emulating! 🎮
