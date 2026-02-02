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

The implementation uses **three dedicated PIO state machines** on `pio0` and `pio1`:

| State Machine | Function | Program | Pin Control |
| --- | --- | --- | --- |
| `pio0.SM0` | E clock generation | `eclock` | GPIO 24 (E) |
| `pio0.SM1` | Sync cycles | `sync` | GPIO 30 (TEST) |
| `pio1.SM0` | Read/Write cycles | `bus_cycle` | GPIO 25-26 (VMA, R/W) |

### Three-State-Machine Architecture

**Key Benefits:**

- **No state machine switching**: Each SM runs its dedicated program permanently
- **FIFO-triggered writes**: Write operations triggered by pushing data to TX FIFO
- **Always ready**: Read SM waits for enable trigger, Write SM waits for TX FIFO data
- **Simplified control**: Eliminates complex program switching logic

**Operation Flow:**

**Read Operations:**

1. Software sets up address bus and data direction
2. Software enables SM1 (read state machine)
3. SM1 waits for E low, executes read cycle, pushes data to RX FIFO
4. Software reads data from SM1's RX FIFO
5. SM1 returns to waiting state

**Write Operations:**

1. Software sets up address bus and data bus
2. Software pushes data byte to SM2's TX FIFO
3. SM2 detects data, executes write cycle automatically
4. SM2 returns to waiting for next TX FIFO data

## Pin Configuration

### GPIO Assignments (NED_SYS7)

| GPIO | Signal | PIO Function | Direction |
| --- | --- | --- | --- |
| 0-7 | D0-D7 | IN pins (data bus) | Bidirectional |
| 24 | E | WAIT source (SM0 output) | Output |
| 25 | VMA | SIDE-SET bit 0 | Output |
| 26 | R/W | SIDE-SET bit 1 | Output |

### SIDE-SET Encoding

With 2-bit SIDE-SET controlling GPIO 25-26:

| Side Value | VMA | R/W | Mode |
| --- | --- | --- | --- |
| `0b11` (3) | 1 | 1 | **Read mode** |
| `0b01` (1) | 1 | 0 | **Write mode** |
| `0b10` (2) | 0 | 1 | **Idle state** |

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
    wait 0 gpio 24      side 0b10       ; Wait for E low, idle
    pull block          side 0b10       ; Pull data from TX FIFO (blocks if empty)
    out pins, 8         side 0b01       ; Drive data bus, assert VMA=1, R/W=0 (write mode)
    wait 1 gpio 24      side 0b01       ; Wait for E high (latch)
    wait 0 gpio 24      side 0b01       ; Wait for E low (end of cycle)
    nop                 side 0b10       ; Deassert VMA=0, R/W=1 (idle), data bus returns to idle

.wrap
```

### Write Cycle Optimization

**Important**: Write cycles do **not** need a delay loop. Unlike read cycles, write cycles only need to wait for E clock edges:

1. **E clock low**: Begin cycle, wait for data in TX FIFO
2. **Pull data**: Read data from TX FIFO
3. **Drive data bus**: Use `out pins, 8` to drive data bus, assert VMA=1, R/W=0 (write mode)
4. **E clock high**: Peripheral latches data immediately
5. **E clock low**: End of cycle, deassert VMA

**Key Timing Points:**

- **Data bus driving**: Data bus is driven by the `out pins, 8` instruction after data is pulled from TX FIFO
- **FIFO synchronization**: `pull block` instruction waits for data in TX FIFO before proceeding
- **Combined operation**: `out pins, 8` simultaneously drives data bus and sets VMA/R/W via SIDE-SET
- **Peripheral latching**: MC6821 PIA latches data on the E clock rising edge
- **Cycle completion**: Write cycle ends on the next E clock falling edge

The E clock period (1.117µs @ 894.886kHz) provides more than sufficient time for the peripheral to latch data.

**Benefits of the optimized write cycle:**

- **Faster write operations**: No unnecessary 300ns delay
- **Proper timing**: Data bus driven only when E is low, ensuring stable data before E rises
- **FIFO-triggered**: Write operations triggered by pushing data to TX FIFO
- **Efficient instruction**: `out pins, 8` drives data bus and sets control signals in one instruction
- **Simplified control**: No state machine switching needed
- **More accurate**: Matches actual MC6800 timing requirements

## Timing Analysis

### Clock Configuration

| Parameter | Value | Notes |
| --- | --- | --- |
| System Clock | Configurable | Set by SYS_CLOCK_MHZ |
| PIO Clock | System Clock | No divider |
| PIO Cycle Time | 1/System Clock | Variable |
| Read Data Setup | Calculated | Based on SYS_CLOCK_MHZ |
| Write Hold Time | Calculated | Based on SYS_CLOCK_MHZ |
| Read Delay | \~300 ns | Target for MC6821 PIA |
| Write Delay | \~300 ns | Target for MC6821 PIA |

### Dynamic Timing Calculation

The PIO programs now use dynamic timing calculation based on the actual system clock frequency:

```c
// Target delay: 300ns (exceeds MC6821 PIA 290ns requirement with margin)
// Convert to cycles: (300ns * sys_clock_hz) / 1000000000
uint64_t cycles = ((uint64_t)300 * sys_clock_hz) / 1000000000;
```

**Example timing at different clock speeds:**

| System Clock | Cycle Time | Required Cycles | Actual Delay |
| --- | --- | --- | --- |
| 125 MHz | 8.000 ns | 38 cycles | 304.0 ns |
| 200 MHz | 5.000 ns | 60 cycles | 300.0 ns |
| 266 MHz | 3.759 ns | 81 cycles | 304.5 ns |
| 300 MHz | 3.333 ns | 90 cycles | 300.0 ns |

### Peripheral Timing Requirements

| Peripheral | Parameter | Spec | This Implementation |
| --- | --- | --- | --- |
| MC6821 PIA | Data delay time (tDDR) max | 290 ns | 304.5 ns (read) |
| MC6800 | Data setup time (tDSR) min | 100 ns | 304.5 ns (read) |
| MC6821 PIA | Data hold time (tDH) min | 0 ns | 304.5 ns (write) |
| Safety | Margin over MC6821 spec | - | 14.5 ns (read/write) |

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
4. PIO waits for E high (peripheral latches data immediately)
5. PIO waits for E low (end of cycle - no delay loop needed)
6. PIO deasserts VMA, sets R/W to idle state

**Note**: Write cycles do **not** use a delay loop. The peripheral latches data on the E clock rising edge, and the cycle completes on the next E clock falling edge. This provides faster write operations and more accurate MC6800 timing.

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

| Aspect | Polling | PIO |
| --- | --- | --- |
| Data setup time | 0-80ns (variable) | 300.7ns (fixed) |
| Jitter | High (software dependent) | Zero (hardware timed) |
| CPU overhead | Polling loops | Minimal (FIFO read) |
| Stability | Race condition possible | Guaranteed stable |
| Debugging | Harder (timing dependent) | Easier (deterministic) |

## Debugging

### Logic Analyzer Setup

For timing verification:

| Channel | Signal | Expected |
| --- | --- | --- |
| CH1 | E clock (GPIO 24) | 894.886 kHz, 50% duty |
| CH2 | VMA (GPIO 25) | Assert on E low, deassert on next E low |
| CH3 | R/W (GPIO 26) | High for read, low for write |
| CH4 | D0 (GPIO 0) | Data valid 300ns after E rises |

### Measurement Points

1. **E rising edge to data sample**: Should be \~300ns
2. **VMA assertion to E rising**: Should be \~560ns (E low period)
3. **Complete cycle time**: Should be \~1.117µs (E period)

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

| File | Description |
| --- | --- |
| `src/bus_cycle.pio` | PIO assembly programs |
| `src/bus.c` | C implementation (`bus_*_pio` functions) |
| `src/bus.h` | Header with function declarations |
| `build/bus_cycle.pio.h` | Generated header (CMake) |

## References

- MC6821 PIA Datasheet - Data delay time (tDDR) specification: 290ns max
- MC6800 Datasheet - Data setup time (tDSR) specification: 100ns min

## See Also

- [Architecture](Architecture.md) - System overview and clock generation
- [Hardware Connection](Hardware-Connection.md) - GPIO pin assignments
- [SPI Bus Debug](SPI-Bus-Debug.md) - Debug interface for timing analysis