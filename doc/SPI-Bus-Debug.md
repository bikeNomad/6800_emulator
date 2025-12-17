# SPI Bus Debug Traces for External Bus Accesses

## Overview

This document describes the SPI debug output for external (unmapped) bus accesses in the MC6800 emulator. In addition to the existing PC/CCR SPI output for instruction execution, the emulator now outputs debug traces for all external bus accesses.

## Implementation

### SPI Packet Format

Each external bus access generates a 4-byte (2 x 16-bit words) SPI packet:

**Word 0 (Address):**
- Byte 1: Address high byte (bits 15-8)
- Byte 0: Address low byte (bits 7-0)

**Word 1 (R/W flag + Data):**
- Byte 3: R/W flag
  - `0x01` = Read operation
  - `0x00` = Write operation
- Byte 2: Data byte
  - For writes: the data written to the address
  - For reads: the data read from the address

### Examples

**Write of 0xAA to address 0x2100:**
- Word 0: `0x2100` (address)
- Word 1: `0x00AA` (write flag + data)
- SPI output: `21 00 / 00 AA`

**Read of 0x55 from address 0x2100:**
- Word 0: `0x2100` (address)
- Word 1: `0x0155` (read flag + data)
- SPI output: `21 00 / 01 55`

## What Gets Traced

The SPI bus debug traces are generated for:

1. **Unmapped/External Addresses**: Any address not in ROM or RAM
   - PIA registers (typically 0x2100-0x2103, 0x2400-0x2403, etc.)
   - Other peripheral devices
   - Unmapped address space

2. **Both Read and Write Operations**: All external bus transactions are logged

## What Does NOT Get Traced

- **ROM accesses**: These are served from the ROM shadow copy in RAM
- **RAM accesses**: These are served from the RAM shadow copy
- **Non-running CPU accesses**: Diagnostic/initialization accesses when CPU is halted

## Code Changes

### Files Modified

1. **src/debug_spi.h**
   - Added `debug_spi_log_bus()` function declaration

2. **src/debug_spi.c**
   - Implemented `debug_spi_log_bus(uint16_t address, bool is_read, uint8_t data)`
   - Formats and sends 4-byte SPI packet for bus accesses

3. **src/memory.c**
   - Added `#include "debug_spi.h"`
   - Added call to `debug_spi_log_bus()` in `memory_read()` for unmapped reads
   - Added call to `debug_spi_log_bus()` in `memory_write()` for unmapped writes

### Function Signature

```c
void debug_spi_log_bus(uint16_t address, bool is_read, uint8_t data);
```

**Parameters:**
- `address`: The 16-bit bus address being accessed
- `is_read`: `true` for read operations, `false` for write operations
- `data`: The data byte (written or read)

## Enable/Disable

Bus debug traces respect the same enable/disable flag as instruction traces:
- Enable: USB command `debug on`
- Disable: USB command `debug off` (default)
- Query: `debug_spi_is_enabled()`

## Use Cases

1. **PIA Debugging**: Monitor all PIA register accesses to understand peripheral behavior
2. **Timing Analysis**: Track when external devices are accessed
3. **Protocol Analysis**: Observe communication patterns with external hardware
4. **Hardware Troubleshooting**: Identify unexpected or missing bus accesses

## Integration with Existing SPI Debug

The bus access traces are interleaved with the existing PC/CCR instruction traces:
- Instruction executed → PC/CCR packet (Word0: PC, Word1: CCR)
- External bus access → Address/Data packet (Word0: Address, Word1: R/W+Data)

This provides a complete execution trace showing both instruction flow and I/O operations.

## Performance Considerations

- SPI output only occurs when debug is enabled (`debug on`)
- When disabled, the function returns immediately with minimal overhead
- Only affects unmapped addresses (external bus), not ROM/RAM accesses
- Time-critical paths remain unaffected when debug is disabled

## Future Enhancements

Possible future additions:
- Separate enable/disable flags for instruction vs bus traces
- Filtering by address range
- Additional metadata in the SPI packet (cycle count, bus timing, etc.)
- Different SPI packet types/formats for different trace events
