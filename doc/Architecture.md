# MC6800 Emulator Architecture

## Overview

The MC6800 Emulator is a cycle-accurate hardware emulator that runs on the RP2350B microcontroller. It provides a bridge between modern development tools and vintage MC6800-based systems, allowing you to develop, test, and debug MC6800 code without requiring original hardware.

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

- **CPU**: Dual Cortex-M33 cores @ 150-300MHz (default 266MHz)
- **Memory**: 520KB SRAM, 2MB Flash
- **GPIO**: 48 pins (NED_SYS7 board)
- **Connectivity**: USB CDC for development, UART for debug

### Hardware Board

**Ned's System 7 Board (BOARD_NED_SYS7)**

- RP2350B with 48 GPIO pins
- Complete address bus (A0-A15) = 16 address lines
- Full 64KB address space
- LED indicators for memory access visualization
- 8MB PSRAM interface support
- Designed for complete MC6800 system replacement

## System Architecture

### Dual-Core Design

```
┌─────────────────────────────────────────────────┐
│            RP2350 (266MHz default)              │
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

Showing Williams System 7 as an example:

```text
┌─────────────────────────────────────────────────────────────┐
│                    MC6800 Address Space                     │
│                         (64KB)                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│  ┌──────────────┐                                           │
│  │ RAM (Shadow) │  0x0000-0x13FF (5KB)                      │
│  │  - Volatile  │  • Mirroring: $0000-$00FF ↔ $1000-$10FF   │
│  │  - Fast      │  • System 7 compatibility                 │
│  └──────────────┘                                           │
│                                                             │
│  ┌──────────────┐                                           │
│  │ CMOS (Flash) │  0x0100-0x01FF (256 bytes)                │
│  │  - Persistent│  • Treated as RAM                         │
│  │  - Settings  │  • Target CMOS is not modified            │
│  └──────────────┘                                           │
│                                                             │
│  ┌──────────────┐                                           │
│  │ Unmapped     │  0x1400-0x3FFF                            │
│  │ (Physical    │  • Routes to physical bus                 │
│  │  Bus)        │  • For PIAs, ACIAs, etc.                  │
│  └──────────────┘                                           │
│                                                             │
│  ┌──────────────┐                                           │
│  │ ROM (Flash)  │  0x4000-0x7FFF (16KB)                     │
│  │  - Read-only │  • A15 not decoded                        │
│  │  - Persistent│  • Aliases at $C000-$FFFF                 │
│  │              │  • Vectors at $7FF8-$7FFF                 │
│  │  - Page-based│  • Mapped on-demand from flash            │  
│  └──────────────┘                                           │
│                                                             │
│  Note: Addresses $8000-$FFFF map to $0000-$7FFF             │
│        due to missing A15 decode                            │
└─────────────────────────────────────────────────────────────┘
```

### Flash Storage Layout

```text
In RP2350 Flash, at Offset 0x100000 (1MiB):
┌──────────────────────────────────────┐
│ ROM Image (up to 48KB)               │
│ • Target system program code         │
│ • Preserved across power cycles      │
├──────────────────────────────────────┤
│ Memory Map (1024 bytes)              │
│ • 256 × 32-bit entries               │
├──────────────────────────────────────┤
│ Memory Config (256 bytes)            │
│ • ROM base, size                     │
│ • RAM base, size                     │
│ • Architecture info                  │
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
Output Pin:      GPIO 24 (NED_SYS7)
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

**Address Bus** (NED_SYS7): Contiguous mapping

- A0-A15 → GPIO 8-23
- 16 address lines total
- Complete 64KB address space

**Control Signals**:

- VMA (Valid Memory Address): GPIO 25
- E (E Clock): GPIO 24 - PIO-generated
- R/W (Read/Write): GPIO 26

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
  • read/write    - Memory access
  • run/halt      - CPU control
  • reset         - CPU reset
  • break         - Set/clear breakpoints
  • status        - Display CPU state
  • scan_memory   - Auto-detect memory map
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

### Instruction Counting

When enabled at compile time, the emulator can count instruction executions:

```cmake
# CMakeLists.txt
target_compile_definitions(mc6800_emulator PRIVATE
    COUNT_INSTRUCTIONS=1
)
```

Commands available when counting is enabled:

- `count print` - Display execution counts for all instructions
- `count reset` - Reset all counters
- `count on/off` - Enable/disable counting

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

    // Check ROM at 0x4000-0x7FFF
    if (physical_addr >= 0x4000 && physical_addr < 0x8000) {
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

The NED_SYS7 board is configured via CMake:

```bash
# Build for NED_SYS7 board (default)
cmake ..
make
```

### Static Inline Functions

GPIO mapping is implemented using static inline functions in `bus.h`:

```c
static inline void drive_address_bus(uint16_t address) {
    // Contiguous mapping for NED_SYS7
    uint32_t gpio_value = (uint32_t)address << 8;
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);
}
```

Benefits:

- Zero overhead (inline)
- Type-safe
- Efficient GPIO operations
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
