# MC6800 Emulator Architecture

## Overview

The MC6800 Emulator is a cycle-accurate hardware emulator that runs on the Raspberry Pi Pico 2 W (RP2350). It provides a bridge between modern development tools and vintage MC6800-based systems, allowing you to develop, test, and debug MC6800 code without requiring original hardware.

## Design Philosophy

### Cycle-Accurate Emulation
The emulator is designed to be **cycle-accurate**, meaning every instruction takes exactly the same number of E clock cycles as it would on real MC6800 hardware. This is critical for:
- Real-time system emulation
- Timing-sensitive code (e.g., pinball machines, arcade games)
- Hardware interfacing with strict timing requirements

### Physical Bus Interface
Unlike pure software emulators, this design includes a **physical bus interface** that allows it to:
- Interface with real MC6800 peripheral chips (PIAs, ACIAs, etc.)
- Act as a replacement CPU in existing systems
- Provide hardware-level debugging capabilities

## Hardware Platform

### RP2350 Microcontroller
- **CPU**: Dual Cortex-M33 cores @ 150MHz
- **Memory**: 520KB SRAM, 2MB Flash
- **GPIO**: 26 pins (Pico 2 W) or 48 pins (Waveshare board)
- **Connectivity**: USB CDC for development, UART for debug

### Supported Boards
1. **Raspberry Pi Pico 2 W (BOARD_PICO2)**
   - Limited GPIO (26 pins)
   - Partial address bus (A0-A1, A10-A14) = 7 address lines
   - Address space: 128 addresses (0x7C03 mask)
   - Best for: PIA-based systems, development, testing

2. **Waveshare RP2350B-Plus-W (BOARD_WAVESHARE)**
   - Full GPIO (48 pins)
   - Complete address bus (A0-A15) = 16 address lines
   - Address space: 64KB full range
   - Best for: Complete system replacement, full memory access

## System Architecture

### Dual-Core Design

```
┌─────────────────────────────────────────────────┐
│            RP2350 (150MHz)                      │
│                                                 │
│  ┌──────────────────┐    ┌──────────────────┐  │
│  │   Core 0         │    │   Core 1         │  │
│  │                  │    │                  │  │
│  │  • CPU Emulation │    │  • USB CDC Task  │  │
│  │  • Instruction   │    │  • Command Parse │  │
│  │    Execution     │    │  • HEX Loading   │  │
│  │  • Interrupt     │    │  • User I/O      │  │
│  │    Handling      │    │                  │  │
│  │  • Bus Control   │    │                  │  │
│  │  • Clock Gen     │    │                  │  │
│  └──────────────────┘    └──────────────────┘  │
│           │                       │             │
│           └───────────────────────┘             │
│               Shared Memory                     │
└─────────────────────────────────────────────────┘
```

**Core 0 (CPU Emulation)**:
- Executes MC6800 instructions
- Manages E clock generation (PIO)
- Handles memory access and bus cycles
- Services interrupts (IRQ, NMI, RESET)
- Implements cycle-accurate timing

**Core 1 (USB Interface)**:
- Dedicated USB CDC processing
- Command line interface
- Intel HEX file loading
- Configuration and diagnostics
- No impact on emulation timing

### Memory Subsystem

```
┌─────────────────────────────────────────────────────────────┐
│                    MC6800 Address Space                     │
│                         (64KB)                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐                                          │
│  │ RAM (Shadow) │  0x0000-0x13FF (5KB)                    │
│  │  - Volatile  │  • Mirroring: $0000-$00FF ↔ $1000-$10FF │
│  │  - Fast      │  • System 7 compatibility               │
│  └──────────────┘                                          │
│                                                             │
│  ┌──────────────┐                                          │
│  │ CMOS (Flash) │  0x0100-0x01FF (256 bytes)              │
│  │  - Persistent│  • Auto-save on write (30s delay)       │
│  │  - Settings  │  • Survives power cycles                │
│  └──────────────┘                                          │
│                                                             │
│  ┌──────────────┐                                          │
│  │ Unmapped     │  0x1400-0x4FFF                          │
│  │ (Physical    │  • Routes to physical bus               │
│  │  Bus)        │  • For PIAs, ACIAs, etc.                │
│  └──────────────┘                                          │
│                                                             │
│  ┌──────────────┐                                          │
│  │ ROM (Flash)  │  0x5000-0x7FFF (12KB)                   │
│  │  - Read-only │  • A15 not decoded                      │
│  │  - Persistent│  • Aliases at $D000-$FFFF               │
│  │              │  • Vectors at $7FF8-$7FFF               │
│  └──────────────┘                                          │
│                                                             │
│  Note: Addresses $8000-$FFFF map to $0000-$7FFF           │
│        due to missing A15 decode                           │
└─────────────────────────────────────────────────────────────┘
```

### Flash Storage Layout

```
RP2350 Flash (2MB):
┌──────────────────────────────────────┐
│  Program Code (< 1MB)                │  0x000000
├──────────────────────────────────────┤
│  ROM Image (32KB max)                │  0x100000 (1MB offset)
│  • MC6800 program code               │
│  • Loaded via USB                    │
│  • Persistent across resets          │
├──────────────────────────────────────┤
│  CMOS RAM (4KB sector)               │  0x108000 (1MB + 32KB)
│  • 256 bytes used                    │
│  • Auto-saved on changes             │
│  • Deferred write (30s idle)         │
└──────────────────────────────────────┘
```

## Clock Generation

### E Clock (PIO-based)

The MC6800 E (Enable) clock is generated using the RP2350's PIO (Programmable I/O) for precise, jitter-free timing:

```
Target Frequency: 894.886 kHz (Williams System 7)
Period:          1.117 µs
Duty Cycle:      50%
Implementation:  PIO state machine
Output Pin:      GPIO 21 (Pico 2) / GPIO 24 (Waveshare)
```

**Why PIO?**
- Hardware-timed, no software jitter
- Runs independently of CPU cores
- Precise frequency control
- No interrupt overhead

### Bus Cycle Timing

Each bus cycle is synchronized to the E clock:

```
E Clock:  ___╱‾‾‾‾╲___╱‾‾‾‾╲___
          Low  High  Low  High

Read Cycle:
  E Low:   Drive address, assert VMA, R/W=1
  E High:  Sample data bus
  E Low:   De-assert VMA

Write Cycle:
  E Low:   Drive address, data, assert VMA, R/W=0
  E High:  Peripheral latches data
  E Low:   De-assert VMA
```

## Bus Interface

### GPIO Mapping

**Data Bus (GPIO 0-7)**: Bi-directional
- D0-D7 mapped to GPIO 0-7
- Switched between input (read) and output (write)
- Pull-ups for floating bus detection

**Address Bus**: Board-dependent
- **BOARD_PICO2**: Non-contiguous mapping
  - A0-A1 → GPIO 8-9
  - A10-A14 → GPIO 10-14
  - 7 address lines total

- **BOARD_WAVESHARE**: Contiguous mapping
  - A0-A15 → GPIO 8-23
  - 16 address lines total

**Control Signals**:
- VMA (Valid Memory Address): GPIO 22 (Pico 2) / GPIO 25 (Waveshare)
- E (E Clock): GPIO 21 (Pico 2) / GPIO 24 (Waveshare) - PIO-generated
- R/W (Read/Write): GPIO 23 (Pico 2) / GPIO 26 (Waveshare)

**Interrupt Inputs** (Active Low):
- /IRQ: GPIO 27
- /NMI: GPIO 28
- /RESET: GPIO 29

### Bus Cycle Implementation

The emulator implements cycle-accurate bus operations:

1. **Synchronization**: All operations wait for E clock edges
2. **Address Translation**: Board-specific inline functions handle GPIO mapping
3. **Physical Bus Access**: Unmapped addresses route to real hardware
4. **Cycle Counting**: Every operation increments cycle counter

## Interrupt Handling

### Interrupt Priority

1. **RESET** (Highest)
   - Edge-triggered
   - Unconditional
   - Vectors from $FFFE-$FFFF (physical $7FFE-$7FFF)

2. **NMI** (Non-Maskable Interrupt)
   - Edge-triggered (falling edge)
   - Cannot be masked
   - Vectors from $FFFC-$FFFD

3. **IRQ** (Interrupt Request)
   - Level-triggered
   - Maskable (CCR I flag)
   - Vectors from $FFF8-$FFF9

### Interrupt Processing

```c
// Simplified interrupt check flow
void interrupt_check(void) {
    // Read interrupt lines
    bool irq = bus_read_irq();
    bool nmi = bus_read_nmi();
    bool reset = bus_read_reset();

    // Priority: RESET > NMI > IRQ
    if (reset && !last_reset_state) {
        interrupt_service_reset();
        return;
    }

    if (nmi && !last_nmi_state) {
        cpu.nmi_pending = true;
    }

    if (cpu.nmi_pending) {
        interrupt_service_nmi();
        cpu.nmi_pending = false;
        return;
    }

    if (irq && !cpu_get_flag(CCR_I)) {
        interrupt_service_irq();
    }
}
```

## Instruction Execution

### Execution Flow

```
┌─────────────────────────────────────────────────────┐
│                   Main Loop (Core 0)                │
│                                                     │
│  while (1) {                                        │
│    if (cpu_is_running()) {                         │
│      1. Check for interrupts                       │
│      2. Fetch instruction (PC → opcode)            │
│      3. Decode opcode → handler                    │
│      4. Execute instruction                        │
│      5. Update PC                                  │
│      6. Count cycles                               │
│      7. Log to debug SPI                           │
│    }                                               │
│    8. Check CMOS auto-save                         │
│  }                                                 │
└─────────────────────────────────────────────────────┘
```

### Cycle Counting

Every instruction consumes the exact number of cycles specified in the MC6800 datasheet:

- **Inherent**: 2 cycles (e.g., NOP, CLC)
- **Immediate**: 2 cycles (e.g., LDAA #$42)
- **Direct**: 3-4 cycles (e.g., LDAA $10)
- **Extended**: 4-6 cycles (e.g., LDAA $1000)
- **Indexed**: 5-7 cycles (e.g., LDAA 0,X)
- **Relative**: 4 cycles (e.g., BRA label)

Stack operations and interrupts add additional cycles for push/pull operations.

## USB CDC Interface

### Command Processing (Core 1)

Core 1 runs a dedicated USB CDC task that processes commands without affecting emulation timing:

```
Commands:
  • load          - Load Intel HEX file (ROM or CMOS)
  • config        - Show/set memory configuration
  • cmos save     - Manually save CMOS to flash
  • cmos dump     - Display CMOS contents
  • read/write    - Memory access
  • run/halt      - CPU control
  • reset         - CPU reset + CMOS save
  • cycletest     - Verify cycle counts
  • bootloader    - Enter RP2350 bootloader
```

### Intel HEX Loading

The emulator automatically detects ROM vs CMOS data based on address ranges:

- **ROM**: Addresses 0x5000-0x7FFF → Flash at 0x100000
- **CMOS**: Addresses 0x0100-0x01FF → Flash at 0x108000

Loading process:
1. User sends HEX file via USB
2. Parser validates checksums
3. Data loaded to appropriate buffer
4. Flash programmed and verified
5. Auto-detection ensures correct storage

## CMOS Persistence

### Auto-Save Strategy

To minimize flash wear (10K-100K erase cycles), CMOS writes are deferred:

```
Write to CMOS → Mark dirty + timestamp
              ↓
Wait 30 seconds of idle time
              ↓
Auto-save to flash
```

**Manual save triggers**:
- `cmos save` command
- `halt` command
- `reset` command
- 30 seconds after last write

**Flash wear analysis**:
- Typical game: 100 CMOS writes → 1 flash erase
- 10,000 cycles = 10,000 games minimum
- At 100 games/day = 100+ days of continuous use
- At 20 games/day = 13+ years

## Debug Features

### SPI Debug Output

GPIO 18-19 provide SPI debug output for logic analyzer monitoring:
- Instruction execution trace
- Register states
- Memory accesses
- Interrupt events

### Conditional Debug Output

Interrupt debugging can be enabled/disabled at compile time:

```cmake
# CMakeLists.txt
target_compile_definitions(mc6800_emulator PRIVATE
    DEBUG_INTERRUPTS=1  # 1=enabled, 0=disabled
)
```

When enabled:
```
*** RESET ***
Reset vector: $5000
*** IRQ at PC=$5234 ***
IRQ vector: $5180
```

### Cycle Testing

The `cycletest` command verifies cycle-accurate execution:
```
$00 NOP      : 2 cycles
$01 NOP      : 2 cycles
$86 LDAA#    : 2 cycles
$96 LDAA     : 3 cycles
$B6 LDAA     : 4 cycles
...
```

## Address Translation (A15 Handling)

### Hardware Limitation

The current hardware design does not decode address line A15. This means:
- A15 is effectively ignored
- Addresses $8000-$FFFF map to $0000-$7FFF
- ROM must be located in the lower 32KB

### Software Solution

The emulator masks A15 in software:

```c
#define ADDR_MASK_A15 0x7FFF

memory_type_t memory_get_type(uint16_t address) {
    uint16_t physical_addr = address & ADDR_MASK_A15;

    // Check ROM at 0x5000-0x7FFF
    if (physical_addr >= 0x5000 && physical_addr < 0x8000) {
        return MEM_TYPE_ROM;
    }
    // ...
}
```

**Vector handling**:
- MC6800 expects vectors at $FFF8-$FFFF
- Physical ROM contains vectors at $7FF8-$7FFF
- Address translation makes this transparent

## Board Abstraction

### Compile-Time Configuration

Different boards are supported via CMake:

```bash
# Build for Pico 2 W (default)
cmake ..
make

# Build for Waveshare board
cmake .. -DBOARD_TYPE=BOARD_WAVESHARE
make
```

### Static Inline Functions

Board-specific GPIO mapping is abstracted using static inline functions in `bus.h`:

```c
#if defined(BOARD_PICO2)
static inline void drive_address_bus(uint16_t address) {
    // Non-contiguous mapping for Pico 2
    uint32_t gpio_value = ((address & 0x0003) << 8) | (address & 0x7C00);
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);
}

#elif defined(BOARD_WAVESHARE)
static inline void drive_address_bus(uint16_t address) {
    // Contiguous mapping for Waveshare
    uint32_t gpio_value = (uint32_t)address << 8;
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);
}
#endif
```

Benefits:
- Zero overhead (inline)
- Type-safe
- Easy to add new boards
- No runtime conditionals

## Performance Characteristics

### Emulation Speed
- **Target**: 894.886 kHz (Williams System 7)
- **Actual**: Cycle-accurate, hardware-timed
- **Overhead**: Minimal (< 5% for instruction decode)

### USB Latency
- **Command response**: < 1ms
- **HEX loading**: ~100KB/sec
- **No impact on emulation** (separate core)

### Memory Access
- **RAM**: Single RP2350 cycle (~6.7ns)
- **ROM (flash)**: XIP cache (~100ns worst case)
- **Physical bus**: Hardware-timed to E clock

## Future Enhancements

### Planned Features
- DMA-based bus operations
- Trace buffer (circular log)
- Breakpoint support
- Single-step debugging
- Remote GDB interface
- Full A15 decode hardware

### Expansion Possibilities
- Multi-processor support
- Networked debugging
- Logic analyzer integration
- FPGA co-processor

## References

- MC6800 Datasheet: Motorola MC6800 8-Bit Microprocessor
- RP2350 Datasheet: Raspberry Pi RP2350 Microcontroller
- Williams System 7 Technical Manual
- Intel HEX Format Specification

## See Also

- [Hardware Connection Guide](Hardware-Connection.md)
- [PIO Bus Cycles](PIO-Bus-Cycles.md)
- [Memory Map](Memory-Map.md)
- [USB Commands](USB-Commands.md)
- [Getting Started](Getting-Started.md)
