# Memory Map

## Overview

The MC6800 Emulator implements a flexible memory system that combines internal storage (flash and RAM) with physical bus access for external peripherals. This document details the complete memory architecture and configuration options.

## Standard Memory Map

### Default Configuration (Williams System 7 Compatible)

```
┌────────────────────────────────────────────────────┐
│  MC6800 Address Space (64KB with A15 aliasing)    │
├────────────────────────────────────────────────────┤
│                                                    │
│  $0000 ┌──────────────────────────────────────┐   │
│        │                                      │   │
│        │  System RAM (5KB)                    │   │
│        │  • Read/Write                        │   │
│        │  • Shadow RAM (fast access)          │   │
│        │  • Volatile                          │   │
│        │                                      │   │
│  $0100 │  ┌────────────────────────────────┐ │   │
│        │  │ CMOS RAM (256 bytes)           │ │   │
│        │  │ • Persistent (flash-backed)    │ │   │
│        │  │ • Auto-save on write (30s)     │ │   │
│        │  │ • Configuration, high scores   │ │   │
│  $01FF │  └────────────────────────────────┘ │   │
│        │                                      │   │
│  $1000 │  ┌────────────────────────────────┐ │   │
│        │  │ RAM Mirror                     │ │   │
│        │  │ $1000-$10FF mirrors $0000-$00FF│ │   │
│  $10FF │  └────────────────────────────────┘ │   │
│        │                                      │   │
│  $13FF └──────────────────────────────────────┘   │
│                                                    │
│  $1400 ┌──────────────────────────────────────┐   │
│        │                                      │   │
│        │  Unmapped / Peripheral Space         │   │
│        │  • Routes to physical bus            │   │
│        │  • PIAs, ACIAs, etc.                 │   │
│        │  • Cycle-accurate bus operations     │   │
│        │                                      │   │
│  $2100 │  ┌────────────────────────────────┐ │   │
│        │  │ PIA #1 (optional)              │ │   │
│        │  │ $2100-$2103                    │ │   │
│  $2103 │  └────────────────────────────────┘ │   │
│        │                                      │   │
│  $2104 │  ┌────────────────────────────────┐ │   │
│        │  │ PIA #2 (optional)              │ │   │
│        │  │ $2104-$2107                    │ │   │
│  $2107 │  └────────────────────────────────┘ │   │
│        │                                      │   │
│  $2108 │  ┌────────────────────────────────┐ │   │
│        │  │ PIA #3 (optional)              │ │   │
│        │  │ $2108-$210B                    │ │   │
│  $210B │  └────────────────────────────────┘ │   │
│        │                                      │   │
│  $4FFF └──────────────────────────────────────┘   │
│                                                    │
│  $5000 ┌──────────────────────────────────────┐   │
│        │                                      │   │
│        │  ROM / EPROM (12KB)                  │   │
│        │  • Read-only                         │   │
│        │  • Flash-backed                      │   │
│        │  • Program code                      │   │
│        │  • Loaded via USB                    │   │
│        │                                      │   │
│  $7FF8 │  ┌────────────────────────────────┐ │   │
│        │  │ Interrupt Vectors              │ │   │
│        │  │ $7FF8: IRQ vector (2 bytes)    │ │   │
│        │  │ $7FFA: SWI vector (2 bytes)    │ │   │
│        │  │ $7FFC: NMI vector (2 bytes)    │ │   │
│        │  │ $7FFE: RESET vector (2 bytes)  │ │   │
│  $7FFF │  └────────────────────────────────┘ │   │
│        └──────────────────────────────────────┘   │
│                                                    │
│  Note: Addresses $8000-$FFFF mirror $0000-$7FFF   │
│        due to A15 not being decoded in hardware   │
│                                                    │
│  MC6800 expects vectors at $FFF8-$FFFF, which     │
│  transparently access physical $7FF8-$7FFF         │
└────────────────────────────────────────────────────┘
```

## Memory Regions

### RAM (Shadow Memory)

**Address Range**: `$0000 - $13FF` (5120 bytes)
**Storage**: RP2350 internal RAM
**Access**: Read/Write
**Speed**: Single cycle (~6.7ns)
**Persistence**: Volatile (lost on power-off)

**Characteristics**:
- Fast access (no bus cycles required internally)
- Cycle-accurate bus timing maintained externally
- Cleared to $00 on reset
- Can be resized via USB command

**Example Configuration**:
```
config ram 0000 2000    # 8KB RAM
config ram 0000 0400    # 1KB RAM
```

### RAM Mirroring

**Address Range**: `$1000 - $10FF` mirrors `$0000 - $00FF`
**Purpose**: Williams System 7 compatibility
**Implementation**: Software mapping

**Why Mirroring?**:
Williams pinball machines use this addressing quirk. The first 256 bytes of RAM are accessible at two locations:
- Primary: `$0000 - $00FF`
- Mirror: `$1000 - $10FF`

Writes to either location update the same physical RAM.

### CMOS RAM (Battery-Backed)

**Address Range**: `$0100 - $01FF` (256 bytes)
**Storage**: RP2350 flash (persists across power cycles)
**Access**: Read/Write (auto-save)
**Speed**: Read = single cycle, Write = deferred flash

**Flash Storage**:
- Physical location: Flash offset `0x108000` (1MB + 32KB)
- Sector size: 4KB (only 256 bytes used)
- Wear leveling: Deferred write (30s idle)

**Auto-Save Behavior**:
```
Write to $0100-$01FF
  ↓
Set dirty flag + timestamp
  ↓
Wait 30 seconds idle
  ↓
Write to flash (if still dirty)
```

**Manual Save Triggers**:
- `cmos save` command
- `halt` command
- `reset` command
- 30 seconds after last write

**Typical Uses**:
- Game settings
- High scores
- Audit data
- Configuration bytes
- Replay scores

**Example Operations**:
```bash
# Read CMOS
read 0100 100

# Write to CMOS
write 0100 42 55 AA FF

# View CMOS contents
cmos dump

# Force save to flash
cmos save
```

**Flash Wear Analysis**:
- Typical game: 100 CMOS writes → 1 flash erase
- Flash endurance: 10,000 - 100,000 cycles
- At 100 games/day: 100+ days
- At 20 games/day: 13+ years

### ROM (Flash)

**Address Range**: `$5000 - $7FFF` (12288 bytes)
**Storage**: RP2350 flash (via XIP - eXecute In Place)
**Access**: Read-only
**Speed**: XIP cache (~100ns worst case)
**Persistence**: Non-volatile

**Flash Storage**:
- Physical location: Flash offset `0x100000` (1MB)
- Maximum size: 32KB
- Current default: 12KB

**Loading ROM**:
```bash
# Via USB CDC
load
[paste Intel HEX file]

# ROM auto-detected by address range
# Addresses $5000-$7FFF → ROM
# Addresses $0100-$01FF → CMOS
```

**Write Protection**:
Writes to ROM addresses are ignored but consume one bus cycle for accuracy.

**Why $5000-$7FFF?**:
The hardware does not decode A15, so:
- Physical ROM: `$5000 - $7FFF`
- Aliased at: `$D000 - $FFFF`
- MC6800 code typically uses `$D000` and higher
- Transparent mapping via software

### Interrupt Vectors

**Physical Location**: `$7FF8 - $7FFF` (8 bytes)
**Logical Location**: `$FFF8 - $FFFF` (MC6800 expectation)
**Storage**: Part of ROM (flash)

| Address (Logic) | Address (Physical) | Vector | Purpose |
|-----------------|-------------------|--------|---------|
| $FFF8-$FFF9 | $7FF8-$7FF9 | IRQ | Interrupt Request |
| $FFFA-$FFFB | $7FFA-$7FFB | SWI | Software Interrupt |
| $FFFC-$FFFD | $7FFC-$7FFD | NMI | Non-Maskable Interrupt |
| $FFFE-$FFFF | $7FFE-$7FFF | RESET | Reset/Power-On |

**Vector Format** (Big-Endian):
```
High byte first, then low byte
Example: $FFF8 = $51, $FFF9 = $80 → IRQ handler at $5180
```

**On Reset**:
1. Emulator reads $FFFE-$FFFF (physical $7FFE-$7FFF)
2. Loads PC with vector value
3. Begins execution

### Unmapped Space (Physical Bus)

**Address Range**: `$1400 - $4FFF` and others not in ROM/RAM
**Access**: Routes to physical bus
**Speed**: Hardware-timed to E clock
**Purpose**: External peripherals

**Behavior**:
Any address not in ROM or RAM is routed to the physical GPIO bus:
1. Assert address on GPIO pins
2. Assert VMA and R/W
3. Wait for E clock high
4. Read/write data bus
5. De-assert VMA

**Typical Peripherals**:
- **PIAs** (6821): Parallel I/O
- **ACIAs** (6850): Serial I/O
- **PTM** (6840): Timer
- **Custom logic**: Displays, sound, etc.

**Example: PIA at $2100**:
```
Address decode:
  A13=1, A12=0, A11=0, A10=0, A9=0
  → $2000-$27FF range
  → Chip select activates PIA
```

## Memory Configuration

### Default Settings

The emulator initializes with Williams System 7-compatible defaults:

```c
ROM:  $5000-$7FFF (12KB)
RAM:  $0000-$13FF (5KB)
CMOS: $0100-$01FF (256 bytes)
```

### Changing Configuration

Via USB commands:

```bash
# Show current configuration
config

# Output:
# Memory Configuration:
#   ROM: $5000-$7FFF (12288 bytes, 12KB)
#   RAM: $0000-$13FF (5120 bytes, 5KB)
#   RAM mirroring: $0000-$00FF <-> $1000-$10FF
#   CMOS RAM: $0100-$01FF (persistent in flash)
#   Unmapped addresses route to physical bus

# Change ROM base and size
config rom E000 2000   # 8KB ROM at $E000-$FFFF

# Change RAM base and size
config ram 0000 0800   # 2KB RAM at $0000-$07FF
```

### Configuration Limits

| Parameter | Minimum | Maximum | Default |
|-----------|---------|---------|---------|
| ROM Base | $0000 | $FFFF | $5000 |
| ROM Size | 1 byte | 32KB | 12KB |
| RAM Base | $0000 | $FFFF | $0000 |
| RAM Size | 1 byte | 8KB | 5KB |
| CMOS Base | Fixed | Fixed | $0100 |
| CMOS Size | Fixed | Fixed | 256 bytes |

## A15 Address Translation

### Problem

The current hardware does not decode address line A15, meaning:
- A15 is ignored
- Upper 32KB mirrors lower 32KB
- Addresses $8000-$FFFF appear as $0000-$7FFF

### Software Solution

The emulator masks A15 in memory type detection:

```c
uint16_t physical_addr = address & 0x7FFF;  // Mask A15
```

**Implications**:
1. **ROM must be in lower 32KB** ($0000-$7FFF)
2. **Vectors at $FFF8 access $7FF8** (transparent to software)
3. **Code can use $D000+ addresses** (they map to $5000+)

### Address Examples

| Logical | Physical | Result |
|---------|----------|--------|
| $0000 | $0000 | RAM |
| $0100 | $0100 | CMOS |
| $5000 | $5000 | ROM |
| $7FF8 | $7FF8 | Vector (IRQ) |
| $8000 | $0000 | RAM (mirrored) |
| $D000 | $5000 | ROM (aliased) |
| $FFF8 | $7FF8 | Vector (IRQ, aliased) |
| $FFFF | $7FFF | ROM (aliased) |

## Memory Access Timing

### Read Cycle (RAM/ROM)

```
Cycle: [----E Low----][----E High----]
       |              |              |
Step:  1. Address    2. Data valid  3. Latch
       stable        on E high      on E low

Time:  0ns           558ns          1117ns
```

1. **E Clock Low**:
   - Drive address bus
   - Assert VMA
   - R/W = 1 (read)

2. **E Clock High**:
   - Data valid from source
   - Sample data bus

3. **E Clock Low**:
   - Latch data
   - De-assert VMA
   - Increment cycle counter

**Total**: 1 E clock cycle (1.117µs @ 894.886 kHz)

### Write Cycle (RAM/Peripherals)

```
Cycle: [----E Low----][----E High----]
       |              |              |
Step:  1. Address+   2. Peripheral  3. De-assert
       Data stable   latches

Time:  0ns           558ns          1117ns
```

1. **E Clock Low**:
   - Drive address bus
   - Drive data bus
   - Assert VMA
   - R/W = 0 (write)

2. **E Clock High**:
   - Peripheral latches data

3. **E Clock Low**:
   - De-assert VMA
   - Return data bus to input
   - Increment cycle counter

**Total**: 1 E clock cycle (1.117µs)

### CMOS Write (Deferred)

```
Write to CMOS:
  CPU write → RAM shadow (1 cycle)
            ↓
            Set dirty flag + timestamp
            ↓
  Continue execution (no stall)
            ↓
  [30 seconds idle]
            ↓
  Flash erase + program (background)
```

**Performance**:
- No execution stall
- Write appears instantaneous
- Flash operation happens later
- Safe if power lost before save (previous value retained)

## Special Memory Features

### Stack

**Location**: User-defined via SP register
**Common**: `$01FE` (grows downward)
**Size**: Limited by RAM size

**Stack Operations**:
- `PUSH`: SP = SP - 1, write to [SP]
- `PULL`: Read from [SP], SP = SP + 1
- `JSR`: Push PC (2 bytes), 5 cycles
- `RTS`: Pull PC (2 bytes), 5 cycles

**Interrupt Stack Frame** (12 bytes):
```
[SP-0]: CCR
[SP-1]: B
[SP-2]: A
[SP-3]: X (low)
[SP-4]: X (high)
[SP-5]: PC (low)
[SP-6]: PC (high)
```

### Zero Page

**Address**: `$0000 - $00FF` (256 bytes)
**Advantage**: Direct addressing (3 cycles vs 4 for extended)
**Common Use**: Variables, pointers, temporaries

**Direct Addressing Example**:
```assembly
LDAA $10    ; Load A from $0010 (3 cycles)
LDAA $1010  ; Load A from $1010 (4 cycles)
```

### Memory-Mapped I/O

PIAs and other peripherals appear as memory locations:

**6821 PIA Registers**:
```
Base + 0: Port A Data / DDR
Base + 1: Port A Control
Base + 2: Port B Data / DDR
Base + 3: Port B Control
```

**Example: Reading Port A**:
```assembly
        LDA  $2100    ; Read Port A
        STA  $0010    ; Store in RAM
```

The emulator routes this to physical bus automatically.

## Memory Testing

### RAM Test

```bash
# Write pattern
write 0000 AA 55 AA 55 AA 55 AA 55
write 0008 12 34 56 78 9A BC DE F0

# Read back
read 0000 10

# Expected output:
# 0000: AA 55 AA 55 AA 55 AA 55 12 34 56 78 9A BC DE F0
```

### ROM Test

```bash
# Read ROM contents
read 5000 100

# Check vectors
read 7FF8 8

# Expected (example):
# 7FF8: 51 80 51 90 51 A0 50 00
#       ^IRQ  ^SWI  ^NMI  ^RESET
```

### CMOS Test

```bash
# Write test pattern
write 0100 DE AD BE EF

# Verify immediately
read 0100 4
# Output: DE AD BE EF

# Force save
cmos save

# Power cycle emulator

# Verify persistence
read 0100 4
# Output: DE AD BE EF (should match)
```

### Mirroring Test

```bash
# Write to primary location
write 0050 42

# Read from mirror
read 1050 1
# Output: 42 (should match)

# Write to mirror
write 1050 99

# Read from primary
read 0050 1
# Output: 99 (should match)
```

## Memory Diagnostics

### View Full Memory Map

```bash
config

# Output shows all configured regions
```

### Dump Specific Regions

```bash
# RAM dump (first 256 bytes)
read 0000 100

# CMOS dump (formatted)
cmos dump

# ROM dump (first 256 bytes)
read 5000 100

# Vectors
read 7FF8 8
```

### Monitor Memory Access

Enable SPI debug output (GPIO 18-19) and connect logic analyzer:
- Captures all memory accesses
- Shows address, data, R/W
- Useful for debugging peripherals

## Memory Layout Examples

### Minimal System (1KB RAM)

```
$0000-$03FF: RAM (1KB)
$5000-$7FFF: ROM (12KB)
Everything else: Unmapped (physical bus)
```

### Williams System 7

```
$0000-$13FF: RAM (5KB)
$0100-$01FF:   └─ CMOS (256 bytes, persistent)
$1000-$10FF:   └─ Mirror ($0000-$00FF)
$2100-$217F: PIA region (3× 6821 PIAs)
$5000-$7FFF: ROM (12KB, game code)
$7FF8-$7FFF:   └─ Vectors
```

### Custom Configuration

```bash
# Large RAM
config ram 0000 2000  # 8KB

# Small ROM
config rom 7000 1000  # 4KB at $7000-$7FFF

# Result:
# $0000-$1FFF: RAM
# $2000-$6FFF: Unmapped (PIAs, etc.)
# $7000-$7FFF: ROM
```

## See Also

- [Architecture](Architecture.md) - Overall system design
- [Hardware Connection](Hardware-Connection.md) - Physical connections
- [USB Commands](USB-Commands.md) - Configuration commands
- [Getting Started](Getting-Started.md) - Quick start guide
