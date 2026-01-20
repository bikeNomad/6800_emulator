# Memory Fingerprinting and Auto-Configuration

## Overview

The MC6800 Emulator includes advanced memory fingerprinting and auto-configuration capabilities that automatically detect and configure the target system's memory layout. This eliminates the need for manual memory configuration in most cases, allowing the emulator to adapt to different pinball machine architectures automatically.

**Key Features:**

- **Automatic Detection**: Scans the target system's address space to identify memory types
- **Architecture Recognition**: Recognizes specific pinball machine architectures (Williams System 7, System 11, etc.)
- **Memory Map Generation**: Builds an optimized memory access map for fast emulation
- **ROM Preservation**: Copies ROM contents to flash for persistent storage
- **Address Aliasing**: Handles systems with incomplete address decoding

## Memory Fingerprinting Process

### Scan Operation

The `scan_memory` USB command initiates a comprehensive scan of the target system's address space:

```bash
> scan_memory
Starting memory scan...
Scanning 256 pages...
Scan complete
Architecture: Williams System 7
```

**What happens during scan:**

1. **Page-by-Page Analysis**: Tests each 256-byte page in the 64KB address space
2. **Memory Type Detection**: Determines if each page contains RAM, ROM, CMOS, PIA, or is empty
3. **Architecture Recognition**: Identifies the target system type based on detected patterns
4. **Memory Map Building**: Creates an optimized lookup table for memory access
5. **ROM Preservation**: Copies detected ROM contents to persistent flash storage

### Memory Type Detection

The fingerprinting system can identify several types of memory and peripherals:

#### RAM Detection

- Writes test data to the address range
- Reads back and verifies the data matches
- Handles different RAM types (regular RAM, CMOS with special patterns)

#### ROM Detection

- Assumes any address not identified as RAM/PIA is ROM
- ROM is typically read-only memory containing program code

#### CMOS Detection

Two types of CMOS RAM are detected:

**Williams System 3-7 CMOS** (high nybble = 0xF):

```
Address: $0100-$01FF
Pattern:  x0 x1 x2 x3 x4 x5 x6 x7 x8 x9 xA xB xC xD xE xF
Where:    F0 F1 F2 F3 F4 F5 F6 F7 F8 F9 FA FB FC FD FE FF
```

**Bally CMOS** (low nybble = 0xF):

```
Address: Various locations
Pattern:  0x 1x 2x 3x 4x 5x 6x 7x 8x 9x Ax Bx Cx Dx Ex Fx
Where:    0F 1F 2F 3F 4F 5F 6F 7F 8F 9F AF BF CF DF EF FF
```

#### PIA Detection

Detects 6820/6821 Peripheral Interface Adapter chips by:

- Testing control register access patterns
- Verifying DDR (Data Direction Register) functionality
- Checking for proper register behavior

#### Empty Space Detection

Identifies unmapped or empty address ranges by checking for consistent 0xFF or 0x00 patterns.

### Architecture Recognition

Based on detected memory patterns, the system recognizes specific architectures:

#### Williams System 7

**Signature:**

- CMOS at pages $01, $05, $09, $0D (4-bit high nybble)
- RAM mirroring ($0000-$00FF ↔ $1000-$10FF)
- 15 address bits decoded (A15 ignored)

**Memory Layout:**

```
$0000-$13FF: RAM (5KB)
$0100-$01FF: CMOS (persistent)
$1000-$10FF: RAM mirror
$2100-$217F: PIA region
$4000-$7FFF: ROM (16KB)
```

#### Williams System 11

**Signature:**

- Contiguous RAM at start of address space
- Full 16-bit address decoding
- No CMOS pattern (uses battery-backed RAM)

**Memory Layout:**

```
$0000-$1FFF: RAM (8KB contiguous)
$2100-$217F: PIA region
$4000-$FFFF: ROM (48KB)
```

#### Early Bally/Stern

**Signature:**

- 13-bit address decoding (8KB unique address space, aliased 8 times)
- Bally zero page at $0000-$00FF (special RAM pattern, stays unmapped for PIA access)
- Bally CMOS at $0200-$03FF (4-bit low nybble = 0xF pattern, stays unmapped)
- RAM at $0100-$01FF
- ROM typically at $1000-$1FFF

**Memory Layout:**

```
$0000-$00FF: Zero page (unmapped - bus access for PIAs)
$0100-$01FF: RAM (256 bytes)
$0200-$03FF: CMOS (unmapped - write-through to bus)
$0400-$0FFF: PIAs and other peripherals (unmapped)
$1000-$1FFF: ROM (4KB)
```

**Important Notes:**

- The Bally zero page ($0000-$00FF) is intentionally left unmapped so that memory accesses in this range go to the physical bus, allowing the PIAs to be accessed
- Bally CMOS also stays unmapped for write-through to the target system
- With 13-bit decoding, the 8KB address space ($0000-$1FFF) is mirrored 8 times throughout the 64KB space

#### Address Decoding Analysis

The system also analyzes how many address lines are actually decoded:

```c
int count_decoded_address_bits(void) {
    // Check for repeated sequences that indicate non-decoded lines
    // Tests for aliasing patterns in the scanned memory map
}
```

## Memory Map Architecture

### Memory Map Structure

The emulator uses a 256-entry lookup table (one per 256-byte page):

```c
uint32_t memory_map[256];  // 256 pages × 256 bytes = 64KB
```

Each entry encodes:

- **Bits 31-8**: Shadow RAM/ROM base address (24 bits)
- **Bit 2**: Write-through flag (for CMOS)
- **Bit 1**: Writable flag (1=RAM, 0=ROM)
- **Bit 0**: Mapped flag (0=mapped, 1=unmapped)

**Entry Formats:**

```c
ENTRY_MAPPED_ROM    = 0b000  // ROM: read from shadow
ENTRY_UNMAPPED_BUS  = 0b001  // Bus: read/write via GPIO
ENTRY_MAPPED_RAM    = 0b010  // RAM: read/write shadow
ENTRY_MAPPED_CMOS   = 0b110  // CMOS: read shadow, write-through
```

### Memory Access Flow

```
CPU Access Request
        ↓
Lookup memory_map[page]
        ↓
Mapped? → Bus Access
    ↓
Shadow Address Calculation
        ↓
Direct RAM Access (1 cycle)
```

### Address Translation

For systems with incomplete address decoding:

```c
uint16_t physical_addr = address & 0x7FFF;  // Mask A15
```

**Williams System 7 Example:**

```
Logical Address → Physical Access
$0000-$7FFF     → $0000-$7FFF (direct)
$8000-$FFFF     → $0000-$7FFF (aliased)
```

### Shadow Memory

Two types of shadow memory provide fast access:

#### RAM Shadow

```c
uint8_t ram_shadow[MAX_RAM_SIZE];  // Fast RAM copy
```

- Mirrors target system RAM
- Single-cycle access during emulation
- Synchronized with target via bus operations

#### ROM Shadow

```c
uint8_t rom_shadow[MAX_ROM_SIZE];  // Flash-backed copy
```

- Contains program code for execution
- Loaded from flash on startup
- Never written back (ROM is read-only)

## Configuration Persistence

### Flash Storage Layout

Memory configuration is stored in RP2350 flash:

```
Flash Offset 0x100000:

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

### Auto-Load on Startup

On emulator boot:

1. Load memory config from flash
2. Validate configuration integrity
3. Load memory map table
4. Initialize ROM shadow from flash
5. Copy RAM contents from target system

### Configuration Validation

Loaded configurations are validated:

- Size limits respected
- Address ranges valid
- Checksum verification
- Fallback to defaults on corruption

## ROM Preservation

### ROM Copying Process

After fingerprinting, detected ROM is copied to flash:

```bash
> scan_memory
...
Copied 12 ROM pages from bus
ROM contents saved to flash
```

**Process:**

1. Identify ROM pages during scan
2. Read ROM contents from bus (256-byte blocks)
3. Buffer in RAM temporarily
4. Program to flash storage
5. Verify programming success
6. Load into ROM shadow for execution

### Flash Wear Management

To minimize flash erase cycles:

- Sector-aligned operations
- Verification before programming
- Interrupt-safe operations

## Memory Configuration Commands

### Manual Configuration

Override auto-detected settings:

```bash
# Configure ROM region
config rom 4000 4000    # 16KB ROM at $4000

# Configure RAM region
config ram 0000 1400    # 5KB RAM at $0000
```

### Memory Map Inspection

View current configuration:

```bash
# Show memory summary
config

# Show detailed mapping (respects decoded bits)
map show
# Output shows only unique address space, e.g.:
# Memory Map ($0000-$1FFF, 13 bits decoded):
#   $0000-$00FF: UNMAPPED
#   $0100-$01FF: RAM
#   ...

# Verify against hardware
verify_memory
```

### ROM Management

```bash
# Load new ROM via USB
load
[paste HEX data]

# Copy ROM from target
copy_roms

# Clear ROM mapping
map clear
```

## Architecture-Specific Features

### Williams System 7 Rules

```c
void apply_system7_rules(void) {
    // RAM mirroring
    memory_map[0x10] = memory_map[0x00];
    // A15 aliasing
    for(int i=0; i<128; i++) {
        memory_map[i+128] = memory_map[i];
    }
}
```

### System 11 Support

- Full 16-bit address decoding
- No aliasing applied
- Continuous ROM from $4000-$FFFF

## Performance Optimization

### Fast Memory Access

- Mapped memory: 1 RP2350 cycle (~6.7ns)
- Unmapped memory: Bus timing (hardware-timed)
- ROM access: RAM shadow (no flash wait states)

### Cycle-Accurate Timing

All memory operations consume correct E-clock cycles:

- RAM/ROM access: 1 cycle
- Bus access: 1 cycle (with E-clock sync)
- Peripheral I/O: Appropriate timing maintained

## Troubleshooting

### Common Issues

#### Scan Fails to Detect Memory

- Ensure target system is powered on
- Check bus connections
- Try manual configuration

#### Invalid Configuration

- Run `verify_memory` to check against hardware
- Re-scan with `scan_memory`
- Check for hardware changes

#### ROM Loading Issues

- Verify HEX file format
- Check address ranges
- Ensure sufficient flash space

### Debug Information

Enable detailed logging:

```cmake
# CMakeLists.txt
target_compile_definitions(mc6800_emulator PRIVATE
    DEBUG_MEMORY_FINGERPRINTING=1
)
```

## Technical Details

### Memory Map Entry Format

```c
typedef uint32_t memory_map_entry_t;

/* Entry bitfield:
 * 31:8  - Shadow base address (24 bits)
 *  7:3  - Reserved
 *  2    - Write-through flag
 *  1    - Writable flag
 *  0    - Mapped flag
 */
```

### Page Size Constants

```c
#define ENTRY_PAGE_SIZE     256    // Bytes per page
#define NUM_PAGES          256    // Total pages (64KB)
#define MAX_ROM_SIZE      32768   // 32KB ROM limit
#define MAX_RAM_SIZE       8192   // 8KB RAM limit
```

### Architecture Types

```c
typedef enum {
    ARCH_UNKNOWN,
    ARCH_EARLY_BALLY,
    ARCH_WILLIAMS_SYS3_6,
    ARCH_WILLIAMS_SYS7,
    ARCH_WILLIAMS_SYS9,
    ARCH_WILLIAMS_SYS11
} architecture_type_t;
```

## Integration with Emulation

### CPU Memory Access

```c
uint8_t memory_read_fast(uint16_t address) {
    uint8_t page = address >> 8;
    uint32_t entry = memory_map[page];

    if (entry & ENTRY_UNMAPPED) {
        return bus_read_cycle(address);
    }

    // Fast shadow access
    uint32_t shadow_addr = entry & ENTRY_ADDR_MASK;
    uint8_t offset = address & 0xFF;
    return *(uint8_t*)(shadow_addr + offset);
}
```

### Interrupt Handling

Memory fingerprinting runs independently of CPU emulation:

- Separate core execution
- No impact on cycle accuracy
- Bus operations properly timed

## Future Enhancements

### Planned Features

- **Extended Architecture Support**: Additional pinball systems
- **Dynamic Reconfiguration**: Runtime memory map changes
- **Memory Protection**: Access violation detection
- **Performance Profiling**: Memory access statistics
- **Advanced Diagnostics**: Detailed bus analysis

### Expansion Capabilities

- **Multi-System Support**: Automatic detection of different architectures
- **Custom Hardware**: Support for non-standard memory configurations
- **Network Configuration**: Remote memory map sharing
- **Firmware Updates**: Over-the-air configuration updates

## See Also

- [Architecture](Architecture.md) - Overall system design
- [Memory Map](Memory-Map.md) - Memory layout details
- [USB Commands](USB-Commands.md) - Configuration commands
- [Hardware Connection](Hardware-Connection.md) - Physical setup
