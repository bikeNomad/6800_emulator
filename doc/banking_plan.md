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

# Banked ROM Support Implementation Plan

## Overview

This document outlines the comprehensive plan to add banked ROM support to the MC6800/MC6809 emulator, enabling it to run later Williams pinball games that use multiple ROM banks (up to 1MB total ROM space) mapped into the $5000-$7FFF address range.

## Problem Statement

Later Williams pinball games (particularly WPC - Williams Pinball Control) use banked ROM memory where:
- Total ROM size can reach 1MB across multiple chips
- Only 12KB ($5000-$7FFF) is visible to the MC6809 at any time
- Bank selection occurs through writes to PIA registers or ASIC addresses
- Current emulator doesn't support any banking - only single 32KB ROM

## Solution Architecture

### Multi-Strategy Banking System

Four different banking strategies to accommodate various memory and performance requirements:

#### BANK_STRATEGY_NONE (Default)
- No banking support - current behavior
- ROM reads are direct from RAM shadow copy (fastest)
- No additional RAM usage
- Compatible with current System 11 games

#### BANK_STRATEGY_DIRECT_XIP
- Bank switching reads directly from flash using XIP
- No additional RAM usage beyond current
- Slower access (100ns vs 6ns) but works for debugging
- Suitable for memory-constrained systems

#### BANK_STRATEGY_CACHE_ONE
- Cache one additional bank in RAM alongside main ROM
- 4KB additional RAM usage
- Fast switching for 2-bank scenarios
- Good for System 11A games

#### BANK_STRATEGY_CACHE_LRU
- LRU cache of 4 banks (16KB additional RAM)
- Intelligent caching minimizes flash access
- Balancing performance and memory usage
- Recommended for general gaming

#### BANK_STRATEGY_PRELOAD
- Load all banks at startup (up to 256KB RAM for 64 banks)
- Instantaneous switching, maximum performance
- Highest memory usage but fastest gameplay
- Best for full-speed emulation of complex games

### Strategy Selection Framework

Users can select strategy via USB command:
```bash
# Enable banked ROM with different strategies
> bank enable wpc xip      # WPC ASIC detection, direct flash access
> bank enable system11 lru # System 11 PIA detection, LRU cache
> bank strategy cache_one   # Change strategy on-the-fly
> bank disable              # Return to single-bank mode
```

## Implementation Details

### Memory Layout

```
RP2350 RAM (520KB):
┌───────────────────┐
│ Program (~100KB)  │
├───────────────────┤
│ ROM Fast Shadow   │ 32KB (current rom_shadow)
│ • Bank 0 always   │    Main code/always resident
│ • Current mapped  │    Currently active bank
├───────────────────┤
│ Bank Cache Area   │ Varies by strategy:
│ • NONE: 0KB       │    No banking
│ • XIP: 0KB        │    Direct flash reads
│ • CACHE_ONE: 4KB  │    One additional bank
│ • LRU: 16KB       │    4-bank LRU cache
│ • PRELOAD: 256KB  │    All banks loaded
├───────────────────┤
│ System RAM (5KB)  │ $0000-$13FF
│ CMOS RAM (256B)   │ $0100-$01FF (persistent)
└───────────────────┘
```

### Flash Storage Organization

```
RP2350 Flash (2MB):
┌──────────────────────┐
│ Program Code (~100KB) │ 0x000000
├──────────────────────┤
│ Banking Config (4KB)  │ 0x100000 - Persistent banking settings
│ • System type         │  • Bank select addresses/masks
│ • Strategy            │  • Bank size/offset mappings
│ • Auto-detection data │  • Performance statistics
│ • Known configurations│
├──────────────────────┤
│ Bank Directory (4KB)  │ 0x101000 - Bank info/metadata
├──────────────────────┤
│ Bank 0 Data           │ Variable size per bank
│ Bank 1 Data           │
│ Bank 2 Data           │
│ ...                   │
├──────────────────────┤
│ CMOS Save Area (4KB)  │ 0x108000 - Game settings, high scores
└──────────────────────┘
```

### Banking Configuration Persistence

**Auto-Save Logic**: Banking settings are saved automatically when changed:

```c
// Struct for persistent storage
typedef struct {
    uint32_t magic;              // Validation marker (BANK_CFG_V1)
    bank_strategy_t strategy;    // Current strategy
    bank_system_type_t system;   // System type (SYSTEM11, WPC, etc.)
    uint16_t bank_select_addr;   // Address to monitor
    uint8_t bank_select_mask;    // Bits to extract
    uint16_t bank_size;          // Size of each bank
    uint16_t total_banks;        // Total banks configured
    uint32_t checksum;           // For corruption detection

    // Descriptive data
    char system_name[32];        // "Williams System 11"
    char strategy_name[16];      // "LRU", "CACHE_ONE", etc.
    uint32_t usage_timestamp;    // Last used timestamp

    // Auto-detection history (for preferred configs)
    detection_result_t last_detection;
} saved_bank_config_t;
```

**Save Triggers**:
- `bank enable` command executed
- `bank strategy` changed
- Auto-detection completes successfully
- `bank save` command (manual)

**Restore Process** (on emulator startup):

```c
void bank_config_restore(void) {
    saved_bank_config_t *stored = (saved_bank_config_t *)FLASH_BANK_CONFIG_ADDR;

    if (stored->magic == BANK_MAGIC && checksum_valid(stored)) {
        printf("Restored banking config: %s / %s (%s @ $%04X)\n",
               stored->system_name, stored->strategy_name,
               system_type_to_addr(stored->system),
               stored->bank_select_addr);

        if (stored->strategy == BANK_STRATEGY_PRELOAD) {
            printf("Reloading %d bank(s) from flash...\n", stored->total_banks);
            // Preload banks from flash to RAM
        }

        printf("Banking configuration restored - use 'bank disable' to override\n");
        banking_enabled = true;  // Resume previous state
    }
}
```

**Integration with CMOS Save**:
- Banking config shares save timing with CMOS data
- Unified save operation: `Automatic CMOS/bank save every 30 seconds idle + on system events`
- Both saved to protected flash sectors

### Bank Manager Architecture

```c
typedef struct {
    bank_strategy_t strategy;
    uint8_t current_bank;
    uint16_t bank_size;          // 2048, 4096, 8192 bytes
    uint16_t total_banks;        // Inferred from ROM size
    bool banking_enabled;

    // Detection support
    bank_system_type_t system;   // SYSTEM11, WPC, CUSTOM
    uint16_t bank_select_addr;   // Address to monitor
    uint8_t bank_select_mask;    // Bits to extract from writes

    // Strategy-specific data
    union {
        struct {
            uint8_t *cache;      // Bank data cache
            uint8_t cache_bank;  // Which bank is cached
        } cache_one;

        struct {
            uint8_t *cache;      // Multi-bank cache
            uint8_t bank_slots[4]; // Which banks in which slots
            uint32_t timestamps[4]; // LRU timestamps
        } lru;

        struct {
            uint8_t **banks;     // Array of pointers to all bank data
        } preload;
    } data;
} bank_manager_t;
```

### Auto-Detection Engine

**ROM-Based Heuristics:**

Auto-detection analyzes loaded ROM data for banking patterns:

```c
typedef struct {
    uint16_t detection_score;    // Confidence level (0-100)
    bank_system_type_t system;
    uint16_t bank_select_addr;
    uint8_t bank_select_mask;
    uint16_t num_banks_detected;
    uint16_t bank_size_estimate;
} detection_result_t;

// Detection Process:
1. Run on ROM load via 'load' command
2. Scan ROM for banking patterns
3. Analyze interrupt vectors for bank switch code
4. Check for system-specific signatures
5. Return confidence-ranked suggestions
```

**Pattern-Based Detection:**

- **PIA Signature Detection:** Scan for code patterns like `BCLR 4, $2101` (System 11)
- **Bank Switch Code:** Look for consecutive writes to PIA/ASIC interfaces
- **ROM Boundary Checks:** Verify ROM banks are properly sized and aligned

**Vector Analysis:**

```c
// Scan vectors for bank switching hints
uint16_t reset_vector = (rom_shadow[0x7FFE] << 8) | rom_shadow[0x7FFF];
uint16_t swi_vector = (rom_shadow[0x7FFA] << 8) | rom_shadow[0x7FFB];
uint16_t nmi_vector = (rom_shadow[0x7FFC] << 8) | rom_shadow[0x7FFD];

// Analyze vector locations for banking artifacts
// System 11 vectors typically in lower ROM banks
// WPC vectors may span multiple banks
```

**Signature Matching:**

```c
const rom_signature_t system11_signatures[] = {
    {
        .signature = {0x0F, 0x21, 0x01, 0x00},  // SEI, AIM/ORA $2101
        .mask = {0xFF, 0xFF, 0xFF, 0xFF},
        .confidence = 90,
        .system = SYSTEM11,
        .bank_addr = 0x2100,
        .bank_mask = 0x10
    }
};
```

**Run-Time Behavioral Detection:**

During execution, monitor for banking signatures:

1. **Address Range Monitoring:** Track writes to peripheral registers
2. **Pattern Recognition:** Identify repeated bank selection patterns
3. **Correlation Analysis:** Match code execution patterns with expected banking

```c
// Monitor execution in 'bank detect' mode
> bank autodetect
Autodetected: System 11 PIA banking at $2100 (confidence: 95%)
ROM banks detected: 3 (2KB each)
Switch pattern: CRA bit 4 controls bank 0/1

Would you like to enable? y/n
```

**Auto-Detection Results:**

```
Detection Results (ROM: game.hex):
─────────────────────────────────────────
1. System 11 / 90% confidence
   • Bank address: $2100 (PIA CRA)
   • Bank mask: $10 (bit 4)
   • Banks detected: 2 × 4KB
   • Strategy: CACHE_ONE

2. WPC ASIC / 45% confidence
   • Bank address: $1F80
   • Bank mask: $0F (4 bits)
   • Banks detected: 4 × 8KB

3. Unknown / Manual config
   • Requires custom parameters

Select option 1-3 or 'manual' for custom setup
```

### Williams System Configurations

**System 11 (Early Games)**
- Bank selection: PIA at $2100-$2103
- Control: CRA bit 4 (0x10) selects bank 0/1
- Decode address: $2000-$27FF triggers PIA
- 2 banks typical (2KB-4KB each)

**System 11A**
- Multiple bits in CRA/CRB control banking
- Supports up to 4 banks via bit combinations
- Deeper decode ($2000-$21FF vs $2000-$27FF)

**System 11B/11C & WPC (ASIC)**
- Direct register write to specific address ($1F80 typical)
- Bank number written directly (0-7, 0-15)
- Fast switching, no decode logic complexity

**Custom Systems**
- User-definable via USB commands
1. ✅ Implement CACHE_ONE strategy
2. ✅ Add LRU cache management
3. ✅ DMA-assisted bank copying
4. ✅ Cache statistics and optimization
5. ✅ Hash-table lookup optimization

### Phase 3: Williams Integration (3 days)
1. ✅ System-11 family configurations
2. ✅ WPC ASIC support
3. ✅ Enhanced HEX loader
4. ✅ Configuration validation
5. ✅ Test with Williams ROMs

### Phase 4: Polish & Documentation (2 days)
1. ✅ Performance monitoring commands
2. ✅ Web interface integration
3. ✅ Documentation updates
4. ✅ Error handling and diagnostics

**Total: 14 days** (2-3 weeks implementation)

## Benefits

### For Williams Gaming
- **System 11 support**: Backward compatible with early games
- **WPC compatibility**: Later games with large ROM banks
- **Performance options**: Choose speed vs memory trade-off
- **Transparent operation**: Games work without code changes

### For Development
- **Flexible strategies**: Adapt to different ROM layouts
- **USB control**: Configure per game requirements
- **Debug support**: Statistics and monitoring
- **Memory efficient**: Only cache what's needed

### For Extensibility
- **Modular design**: Easy to add new platforms
- **Configurable detection**: Custom address/bit manipulation
- **Strategy plug-ins**: New caching strategies possible

## USB Command Interface

### Bank Management Commands
```bash
# Enable banking with system detection
> bank enable system11        # Auto-detect System 11 PIA banking
> bank enable wpc             # Auto-detect WPC ASIC banking
> bank enable custom 1F80 0F  # Custom address $1F80, mask 0x0F

# Control strategies
> bank strategy xip           # Direct flash reads (slowest)
> bank strategy cache_one     # Single bank cache
> bank strategy lru           # LRU multi-bank cache
> bank strategy preload       # Load all banks

# Status and control
> bank status                 # Show current configuration
> bank select 3               # Force bank switch to #3
> bank info                   # Show ROM bank sizes and layout
> bank disable                # Return to non-banked mode
> bank statistics             # Cache hit/miss ratios
```

### Example Usage Session

```bash
# Load WPC ROM with banking enabled
> load
[paste enhanced HEX with !BANK directives]

# Enable banking
> bank enable wpc
Banking enabled: WPC ASIC at $1F80, LRU strategy

# Status check
> bank status
Banking: ENABLED
Strategy: LRU (caches 4 banks)
Current bank: 0
Address monitor: $1F80 (ASIC direct)
ROM banks: 8 × 8KB = 64KB total
Cache: 3/4 slots used, hit ratio 98.7%

# Change strategy if needed
> bank strategy preload
Strategy changed to PRELOAD
Loading all 8 banks... Done (256KB RAM used)

# Start game
> reset
> run
[out-of-memory-errors on cache miss eliminated]
```

## Compatibility and Testing

### Backward Compatibility
- **No banking enabled**: Behavior identical to current
- **Single bank**: Functions as before
- **Configuration optional**: Default off, opt-in only

### Test Cases

**System 11 Test:**
- Load game, enable `bank enable system11`
- PIA writes to $2101 should switch banks
- Verify ROM access at $5000 shows different data

**WPC Test:**
- Load multi-bank WPC ROM
- ASIC writes to $1F80 select banks
- Fast switching between 64KB ROM pages

**Performance Test:**
- Measure bank switch timing under load
- Cache hit/miss ratios during gameplay
- Memory usage vs performance trade-offs

### Error Handling
- **Invalid bank numbers**: Log warning, mask to valid range
- **Flash read failures**: Fall back to default bank
- **Cache overflow**: Evict LRU, log statistics
- **Misconfiguration**: Allow disable/reconfigure without restart

## Technical Implementation Notes

### DMA for Flash Access
For faster bank copying, use RP2350 DMA controller:

```c
void dma_bank_copy(uint32_t flash_addr, uint8_t *ram_addr, uint32_t size) {
    dma_instance->READ_ADDR_TRIG = flash_addr;
    dma_instance->WRITE_ADDR_TRIG = (uint32_t)ram_addr;
    dma_instance->TRANS_COUNT_TRIG = size;
    // CPU can process other tasks during copy
}
```

### Address Translation Cache
For LRU strategies, maintain virtual-to-physical mapping:

```c
uint16_t get_physical_offset(uint16_t logical_addr, uint8_t bank) {
    // Fast lookup: virtual_addr → (bank + physical_offset)
    return get_bank_offset(bank) + (logical_addr - mem_config.rom_base);
}
```

### Statistics Gathering
Monitor system performance:

```c
typedef struct {
    uint32_t bank_switches;      // Total switches
    uint32_t cache_hits;         // LRU/cache hits
    uint32_t cache_misses;       // Cache misses requiring load
    uint32_t flash_reads;        // Direct XIP reads
    uint32_t current_hit_ratio;  // Percentage
} bank_stats_t;
```

This provides comprehensive monitoring for optimizing performance and memory usage.

## Conclusion

The proposed banked ROM implementation provides a flexible, high-performance solution for Williams MC6809 games while maintaining full backward compatibility. The multi-strategy approach accommodates different memory and performance requirements, making it suitable for both development debugging and production gaming scenarios.
