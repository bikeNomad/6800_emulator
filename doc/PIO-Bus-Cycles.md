# PIO-Based Bus Cycles

## Overview

The MC6800 emulator uses the RP2350's PIO (Programmable I/O) hardware for precise bus cycle timing. This document describes the PIO-based bus cycle implementation that provides hardware-timed data sampling with guaranteed setup times.

## Problem Statement

### Original Polling-Based Approach

The original bus cycle implementation used software polling to synchronize with the E clock:

```c
uint8_t bus_read_cycle(uint16_t address) {
    bus_sync();                    // Wait for E low
    gpio_set_dir_masked(...);      // Set data bus to input
    drive_address_bus(address);    // Drive address
    drive_control_read();          // Assert VMA, R/W=1
    eclock_wait_high();            // Poll until E high
    uint8_t data = gpio_get_all(); // Read data immediately
    eclock_wait_low();             // Wait for E low
    deassert_vma();                // De-assert VMA
    return data;
}
```

### The Timing Problem

This approach has a race condition:

```text
E rises ──┐
          │ Software detects E high (0-80ns variable latency)
          │
          └──► Immediate data read ──► DATA MAY NOT BE STABLE
```

**Root cause**: MC6821 PIA peripherals have a worst-case data delay time of **290ns** after E rises before data is stable on the bus. The polling-based approach reads data immediately (0-80ns after E rises), often capturing invalid data.

### PIO Solution

The PIO-based approach eliminates this race condition by using hardware-timed delays that guarantee data stability before sampling.

## Architecture

### Hardware-Timed Data Sampling

The PIO-based approach guarantees a precise 304.5ns delay after E rises before sampling data:

```text
E rises ──┐
          │ PIO detects E high (0 cycle latency)
          │
          ├── 81 PIO cycles (304.5ns @ 266MHz)
          │
          └──► Sample data ──► DATA IS STABLE
```

The implementation uses two PIO state machines on `pio0`:

| State Machine | Function           | Pin Control           |
|---------------|--------------------|-----------------------|
| SM0           | E clock generation | GPIO 24 (E)           |
| SM1           | Bus cycle timing   | GPIO 25-26 (VMA, R/W) |

```text
┌─────────────────────────────────────────────────────────┐
│                        pio0                              │
│                                                          │
│  ┌──────────────┐          ┌──────────────────────────┐ │
│  │     SM0      │          │          SM1             │ │
│  │              │          │                          │ │
│  │  E Clock     │──GPIO24──│  Bus Cycle Timing        │ │
│  │  Generator   │  (WAIT)  │                          │ │
│  │              │          │  • VMA control (GPIO25)  │ │
│  │  894.886 kHz │          │  • R/W control (GPIO26)  │ │
│  │  50% duty    │          │  • Data sampling (0-7)   │ │
│  └──────────────┘          └──────────────────────────┘ │
│                                                          │
└─────────────────────────────────────────────────────────┘
```

## Pin Configuration

### GPIO Assignments (NED_SYS7)

| GPIO | Signal | PIO Function             | Direction     |
|------|--------|--------------------------|---------------|
| 0-7  | D0-D7  | IN pins (data bus)       | Bidirectional |
| 24   | E      | WAIT source (SM0 output) | Output        |
| 25   | VMA    | SIDE-SET bit 0           | Output        |
| 26   | R/W    | SIDE-SET bit 1           | Output        |

### SIDE-SET Encoding

With 2-bit SIDE-SET controlling GPIO 25-26:

| Side Value | VMA | R/W | Mode           |
|------------|-----|-----|----------------|
| `0b11` (3) | 1   | 1   | **Read mode**  |
| `0b01` (1) | 1   | 0   | **Write mode** |
| `0b10` (2) | 0   | 1   | **Idle state** |

## PIO Programs

### Read Cycle Program

Located in `src/bus_cycle.pio`:

```asm
.program bus_read_cycle

.side_set 2         ; GPIO 25=VMA, GPIO 26=R/W

public read_cycle:
    wait 0 gpio 24      side 0b10       ; Sync: Wait for E low, idle
    nop                 side 0b11       ; Assert VMA=1, R/W=1 (read)
    wait 1 gpio 24      side 0b11       ; Wait for E high
    ; Data setup delay: 1 + (10 x 8) = 81 cycles = 304.5ns @ 266MHz
    ; MC6821 PIA worst-case data delay is 290ns, so use 300ns+ for margin
    set x, 9            side 0b11       ; Initialize loop counter (1 cycle)
read_delay:
    jmp x-- read_delay [7] side 0b11   ; Loop 10 times x 8 cycles = 80 cycles
    in pins, 8          side 0b11       ; Sample data (now stable!), keep signals
    push noblock        side 0b11       ; Push to RX FIFO, keep signals
    wait 0 gpio 24      side 0b11       ; Wait for E low, keep signals
    nop                 side 0b10       ; Deassert VMA=0, R/W=1 (idle)

.wrap
```

### Write Cycle Program

```asm
.program bus_write_cycle

.side_set 2         ; GPIO 25=VMA, GPIO 26=R/W

public write_cycle:
    wait 0 gpio 24      side 0b10       ; Sync: Wait for E low, VMA=0, R/W=1 (idle)
    nop                 side 0b01       ; Assert VMA=1, R/W=0 (write mode)
    wait 1 gpio 24      side 0b01       ; Wait for E high (latch), keep signals
    ; Hold time delay: 1 + (10 x 8) = 81 cycles = 304.5ns @ 266MHz
    ; (max delay is 7 with 2-bit side-set)
    set x, 9            side 0b01       ; Initialize loop counter (1 cycle)
write_delay:
    jmp x-- write_delay [7] side 0b01  ; Loop 10 times x 8 cycles = 80 cycles
    wait 0 gpio 24      side 0b01       ; Wait for E low, keep signals
    nop                 side 0b10       ; Deassert VMA=0, R/W=1 (idle)

.wrap
```

### Delay Instruction Limit

With 2-bit SIDE-SET, only 3 bits remain for delay encoding, limiting the maximum delay per instruction to 7 cycles (`[7]`). To achieve 80 cycles of delay, both programs use a loop-based approach.

### Loop-Based Optimization

Both read and write cycles use the same efficient loop-based approach instead of separate `nop [7]` instructions:

```asm
set x, 9            side 0b11       ; Initialize loop counter (1 cycle)
read_delay:         ; (or write_delay:)
    jmp x-- read_delay [7] side 0b11   ; Loop 10 times x 8 cycles = 80 cycles
```

This reduces program size while maintaining the same timing (81 total cycles including the `set` instruction) for both read and write operations.

## Timing Analysis

### Clock Configuration

| Parameter          | Value    | Notes                 |
|--------------------|----------|-----------------------|
| System Clock       | 266 MHz  | RP2350 default        |
| PIO Clock          | 266 MHz  | No divider            |
| PIO Cycle Time     | 3.759 ns | 1/266MHz              |
| Read Data Setup    | 81 cycles| 1 + (10 × 8) cycles   |
| Write Hold Time    | 81 cycles| 1 + (10 × 8) cycles   |
| Read Delay         | 304.5 ns | 81 × 3.759ns          |
| Write Delay        | 304.5 ns | 81 × 3.759ns          |

### Peripheral Timing Requirements

| Peripheral | Parameter                    | Spec     | This Implementation |
|------------|------------------------------|----------|---------------------|
| MC6821 PIA | Data delay time (tDDR) max   | 290 ns   | 304.5 ns (read)     |
| MC6800     | Data setup time (tDSR) min   | 100 ns   | 304.5 ns (read)     |
| MC6821 PIA | Data hold time (tDH) min     | 0 ns     | 304.5 ns (write)    |
| Safety     | Margin over MC6821 spec      | -        | 14.5 ns (read/write)|

### Bus Cycle Timing Diagram

```text
Read Cycle:
E Clock:    ___╱‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲___
                ↑                    ↓
                │                    │
VMA:        ____╱‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲___
                ↑                    ↓
                │                    │
R/W:        ‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾ (read)
                │                    │
                │←─── 304.5ns ───→│  │
Data:       ────┼────────────────┼───┤
                │                │   │
                E↑              READ E↓
                               (sample)

Write Cycle:
E Clock:    ___╱‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲___
                ↑                    ↓
                │                    │
VMA:        ____╱‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾‾╲___
                ↑                    ↓
                │                    │
R/W:        ‾‾‾‾‾╲___________________╱‾‾‾‾ (write)
                │   ↑               │
                │   │←─ 304.5ns ─→│  │
Data:       ────┼───┼───────────────┼───┤
                │   │               │   │
                E↑  LATCH           E↓
```

## C API

### Initialization

```c
#include "bus.h"

// Initialize PIO bus cycles (call after bus_init() and eclock_init())
bus_cycle_pio_init();
```

**Output:**

```text
PIO bus cycles initialized (SM1 on pio0)
  Read program offset:  0
  Write program offset: 19
  E clock: GPIO 24 (WAIT source)
  VMA:     GPIO 25 (SIDE-SET bit 0)
  R/W:     GPIO 26 (SIDE-SET bit 1)
  Read data setup delay: 81 cycles @ 266MHz = 304.5ns
  Write hold time delay: 81 cycles @ 266MHz = 304.5ns
```

### Read Operation

```c
uint8_t bus_read_cycle_pio(uint16_t address);
```

**Sequence:**

1. Software sets data bus direction to input
2. Software drives address bus
3. PIO waits for E low, asserts VMA + R/W
4. PIO waits for E high
5. PIO delays 304.5ns (81 cycles)
6. PIO samples data bus → RX FIFO
7. Software reads data from FIFO
8. PIO waits for E low, deasserts VMA

### Write Operation

```c
void bus_write_cycle_pio(uint16_t address, uint8_t data);
```

**Sequence:**

1. Software sets data bus direction to output
2. Software drives address bus and data bus
3. PIO waits for E low, asserts VMA, clears R/W
4. PIO waits for E high (peripheral latches data)
5. PIO delays 304.5ns (81 cycles, hold time)
6. PIO waits for E low, deasserts VMA, sets R/W

### Enable/Disable

```c
// Disable PIO bus cycles (use polling instead)
bus_cycle_pio_enable(false);

// Enable PIO bus cycles
bus_cycle_pio_enable(true);

// Check status
bool enabled = bus_cycle_pio_is_enabled();
```

## Hybrid Architecture

The implementation uses a **hybrid approach** where software and PIO share responsibilities:

### Software Responsibilities

- Set up address bus (GPIO 8-23)
- Set data bus direction (GPIO 0-7)
- Set data bus value for writes
- Read data from PIO RX FIFO

### PIO Responsibilities

- Wait for E clock edges
- Control VMA signal
- Control R/W signal
- Precise data sampling timing
- Push sampled data to FIFO

### Advantages

1. **Works with current pin mapping** - No hardware changes required
2. **Precise timing** - Hardware guarantees 300.7ns delay
3. **Simple PIO programs** - Easy to understand and debug
4. **Reuses existing code** - Address bus drive functions unchanged

## Comparison: Polling vs PIO

| Aspect           | Polling                    | PIO                      |
|------------------|----------------------------|--------------------------|
| Data setup time  | 0-80ns (variable)          | 300.7ns (fixed)          |
| Jitter           | High (software dependent)  | Zero (hardware timed)    |
| CPU overhead     | Polling loops              | Minimal (FIFO read)      |
| Stability        | Race condition possible    | Guaranteed stable        |
| Debugging        | Harder (timing dependent)  | Easier (deterministic)   |

## Debugging

### Logic Analyzer Setup

For timing verification:

| Channel | Signal           | Expected                               |
|---------|------------------|----------------------------------------|
| CH1     | E clock (GPIO 24)| 894.886 kHz, 50% duty                  |
| CH2     | VMA (GPIO 25)    | Assert on E low, deassert on next E low|
| CH3     | R/W (GPIO 26)    | High for read, low for write           |
| CH4     | D0 (GPIO 0)      | Data valid 300ns after E rises         |

### Measurement Points

1. **E rising edge to data sample**: Should be ~300ns
2. **VMA assertion to E rising**: Should be ~560ns (E low period)
3. **Complete cycle time**: Should be ~1.117µs (E period)

### Tuning Data Setup/Hold Delays

To adjust the delays (if needed for different peripherals):

**Read/Write Cycle Delay (currently 81 cycles = 304.5ns):**

Both read and write cycles now use the same loop-based approach with identical timing:

```asm
; Current: 81 cycles = 304.5ns (for MC6821 PIA)
set x, 9            side 0b11       ; Initialize loop counter (1 cycle)
read_delay:         ; (or write_delay:)
    jmp x-- read_delay [7] side 0b11   ; Loop 10 times x 8 cycles = 80 cycles

; For 200ns delay at 266MHz: 53 cycles
; Use set x, 5 + jmp x-- read_delay [7] + nop [4] = 40 + 5 = 53 cycles = 199.2ns

; For 150ns delay at 266MHz: 40 cycles
; Use set x, 4 + jmp x-- read_delay [7] = 32 + 1 = 33 cycles = 124.0ns
; Or use 5 x nop[7] = 40 cycles = 150.4ns (replace loop with separate nops)
```

**Note**: Since both read and write cycles now use the same loop-based implementation, timing adjustments apply to both operations simultaneously.

## Files

| File                   | Description                             |
|------------------------|-----------------------------------------|
| `src/bus_cycle.pio`    | PIO assembly programs                   |
| `src/bus.c`            | C implementation (`bus_*_pio` functions)|
| `src/bus.h`            | Header with function declarations       |
| `build/bus_cycle.pio.h`| Generated header (CMake)                |

## References

- MC6821 PIA Datasheet - Data delay time (tDDR) specification: 290ns max
- MC6800 Datasheet - Data setup time (tDSR) specification: 100ns min

## See Also

- [Architecture](Architecture.md) - System overview and clock generation
- [Hardware Connection](Hardware-Connection.md) - GPIO pin assignments
- [SPI Bus Debug](SPI-Bus-Debug.md) - Debug interface for timing analysis
