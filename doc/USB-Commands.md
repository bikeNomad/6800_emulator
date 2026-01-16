# USB Commands Reference

## Overview

The MC6800 Emulator provides a comprehensive command-line interface via USB CDC (Communication Device Class). This allows you to control the emulator, load programs, inspect memory, and configure the system without requiring specialized software.

**💡 Prefer a graphical interface?** Check out the [Web Interface](Web-Interface.md) for a browser-based control panel with visual status display, one-click ROM uploads, and auto-refresh capabilities. The web interface uses these same USB commands under the hood.

**This document** covers the raw USB CDC command protocol for:

- Terminal/command-line users
- Scripting and automation
- Understanding the communication protocol
- Advanced debugging and testing

## Connection

**💡 Tip**: The [Web Interface](Web-Interface.md) (`web-interface/emulator-control.html`) provides an easier way to connect using your browser's built-in serial support - no terminal software required!

### Serial Terminal Settings

- **Baud Rate**: Any (USB CDC ignores baud rate)
- **Data Bits**: 8
- **Parity**: None
- **Stop Bits**: 1
- **Flow Control**: None
- **Line Ending**: CR or LF (auto-detected)

### Connecting

**macOS/Linux**:

```bash
# Find device
ls /dev/tty.usb*

# Connect with screen
screen /dev/tty.usbmodem14201

# Or use minicom
minicom -D /dev/tty.usbmodem14201

# Or tio
tio /dev/tty.usbmodem14201
```

**Windows**:

```
# Use PuTTY, TeraTerm, or Arduino Serial Monitor
# Port: COM3, COM4, etc. (check Device Manager)
```

**Arduino IDE**:

1. Tools → Port → Select USB Serial Device
2. Tools → Serial Monitor
3. Set line ending to "Newline"

## Command Summary

| Command | Parameters | Description |
|---------|------------|-------------|
| `help` | none | Show command list |
| `load` | (interactive) | Load Intel HEX file |
| `end` | none | Exit HEX load mode |
| `config` | none | Show memory configuration |
| `config rom` | base size | Configure ROM region |
| `config ram` | base size | Configure RAM region |
| `checksum` | addr len | Calculate checksum of memory range |
| `read` | addr len | Read memory |
| `write` | addr data... | Write memory |
| `status` | none | Display CPU status |
| `run` | none | Start CPU execution |
| `halt` | none | Stop CPU execution |
| `reset` | none | Reset CPU |
| `debug on` | none | Enable SPI debug output |
| `debug off` | none | Disable SPI debug output |
| `break` | addr | Set breakpoint |
| `break clear` | [addr] | Clear breakpoint(s) |
| `break list` | none | List breakpoints |
| `reg pc` | value | Set program counter |
| `reg a` | value | Set accumulator A |
| `reg b` | value | Set accumulator B |
| `reg x` | value | Set index register X |
| `reg sp` | value | Set stack pointer |
| `reg ccr` | value | Set condition code register |
| `bus_read` | addr | Read byte from hardware bus |
| `bus_write` | addr data | Write byte to hardware bus |
| `readb` | addr len | Read block from hardware bus |
| `writeb` | addr data... | Write block to hardware bus |
| `bus_info` | none | Show bus configuration |
| `map show` | none | Show ROM mapping state |
| `map clear` | none | Clear all ROM mapping |
| `map program` | addr | Manually map ROM page |
| `copy_roms` | none | Copy ROM data from bus to persistent storage |
| `scan_memory` | none | Auto-detect and configure memory map |
| `verify_memory` | none | Verify memory configuration |
| `count print` | none | Print instruction execution counts and cycles |
| `count reset` | none | Reset instruction execution counts |
| `count on` | none | Enable instruction counting |
| `count off` | none | Disable instruction counting |
| `bootloader` | none | Enter UF2 bootloader |
| `boot` | none | Enter UF2 bootloader (alias) |

## Command Details

### help

Show list of available commands.

**Syntax**:

```
help
```

**Example**:

```
> help
MC6800 Emulator Commands:
  load                      - Load Intel HEX
  end                       - End Intel HEX
  config                    - Show memory configuration
  config rom <b> <s>        - Configure ROM region
  config ram <b> <s>        - Configure RAM region
  checksum <addr> <len>     - Calculate checksum of memory range
  read <addr> <len>         - Read memory
  write <addr> <data>       - Write memory
  status                    - Display CPU status
  run                       - Start CPU execution
  halt                      - Stop CPU execution
  reset                     - Reset CPU
  count print               - Print instruction execution counts
  count reset               - Reset instruction execution counts
  count on                  - Enable instruction counting
  count off                 - Disable instruction counting
  debug on/off              - Enable/disable SPI debug output
  break <addr>              - Set breakpoint at address
  break clear               - Clear all breakpoints
  break clear <addr>        - Clear specific breakpoint
  break list                - List all breakpoints
  reg pc <val>              - Set program counter
  reg a <val>               - Set accumulator A
  reg b <val>               - Set accumulator B
  reg x <val>               - Set index register X
  reg sp <val>              - Set stack pointer
  reg ccr <val>             - Set condition code register
  bus_read <addr>           - Read byte from hardware bus
  bus_write <addr> <data>   - Write byte to hardware bus
  readb <addr> <len>        - Read block from hardware bus
  writeb <addr> <data...>   - Write block to hardware bus
  bus_info                  - Show bus configuration
  map show                  - Show ROM mapping state
  map clear                 - Clear all ROM mapping
  map program <addr>        - Manually map ROM page
  copy_roms                 - Copy ROM data from bus to persistent storage
  scan_memory               - Auto-detect and configure memory map
  verify_memory             - Verify memory configuration
  bootloader                - Enter bootloader mode
  boot                      - Enter bootloader mode (alias)
  help                      - Show this help
```

### load

Load Intel HEX file (ROM or CMOS). The emulator automatically detects the data type based on addresses in the HEX file.

(Note that echo is turned off during the load)

**Syntax**:

```
load
[paste Intel HEX data]
[EOF record :00000001FF automatically detected]
```

**Address Detection**:

- Addresses `$5000-$7FFF` (or `$D000-$FFFF`) → ROM
- Addresses `$0100-$01FF` (or `$8100-$81FF`) → CMOS

**Example**:

Note that echo is turned off during the load;
the hex data is shown here for clarity.

```
> load
Ready to receive Intel HEX data. Paste file now...
:1050000086424EFFFFFFFF7E500097107E502E20D2
:1050100097107E502E2097107E502E20AA012645C0
:105020003D0E000186FF975397543D2758A75458C7
:02FFE00050008A
:00000001FF
Detected ROM data (address $5000)
Loaded 516 bytes from HEX data
Finalizing ROM load...
Writing 12288 bytes to flash at offset 0x00100000...
Flash programming complete
Flash verification OK
OK: EPROM loaded successfully
```

**Notes**:

- Paste entire HEX file at once (most terminals support this)
- EOF record (`:00000001FF`) ends loading automatically
- Checksums are verified
- Flash is automatically programmed and verified

**Common Errors**:

```
ERROR: Failed to load EPROM
```

- Check HEX file format
- Verify addresses are in valid range
- Ensure sufficient flash space

### end

Exit HEX load mode and process any accumulated data.

**Syntax**:

```
end
```

**Example**:

```
> load
Ready to receive Intel HEX data. Paste file now...
:1050000086424EFFFFFFFF7E500097107E502E20D2
:00000001FF
end
Processing HEX data...
OK: EPROM loaded successfully
```

**Notes**:

- Used to manually exit HEX load mode when EOF record is not present
- Automatically called when Intel HEX EOF record (`:00000001FF`) is detected
- Processes any data accumulated during HEX load mode

### config

Display current memory configuration.

**Syntax**:

```
config
```

**Example**:

```
> config
Memory Configuration:
  ROM: $5000-$7FFF (12288 bytes, 12KB)
  RAM: $0000-$13FF (5120 bytes, 5KB)
  RAM mirroring: $0000-$00FF <-> $1000-$10FF
  CMOS RAM: $0100-$01FF (persistent in flash)
  Unmapped addresses route to physical bus
  Debug SPI: OFF
```

### config rom

Configure ROM base address and size.

**Syntax**:

```
config rom <base_hex> <size_hex>
```

**Parameters**:

- `base_hex`: Base address in hexadecimal (without $ or 0x)
- `size_hex`: Size in hexadecimal bytes

**Example**:

```
> config rom E000 2000
ROM configured: $E000-$FFFF (8192 bytes)
OK: ROM configured at $E000, size $2000
```

**Limits**:

- Maximum ROM size: 32KB (0x8000)
- Base + size must not exceed 0xFFFF
- Changes take effect immediately

**Common Configurations**:

```
config rom 5000 3000    # 12KB at $5000 (default)
config rom E000 2000    # 8KB at $E000
config rom F000 1000    # 4KB at $F000
config rom 8000 8000    # 32KB at $8000 (max)
```

### config ram

Configure RAM base address and size.

**Syntax**:

```
config ram <base_hex> <size_hex>
```

**Parameters**:

- `base_hex`: Base address in hexadecimal
- `size_hex`: Size in hexadecimal bytes

**Example**:

```
> config ram 0000 0800
RAM configured: $0000-$07FF (2048 bytes)
OK: RAM configured at $0000, size $0800
```

**Limits**:

- Maximum RAM size: 8KB (0x2000)
- RAM is cleared when reconfigured
- CMOS region ($0100-$01FF) is always persistent (copy on write uses target's CMOS RAM for persistence)

**Common Configurations**:

```
config ram 0000 1400    # 5KB at $0000 (default)
config ram 0000 2000    # 8KB at $0000 (maximum)
config ram 0000 0400    # 1KB at $0000 (minimal)
config ram 8000 1000    # 4KB at $8000 (non-standard)
```

### checksum

Calculate checksum of a memory range. Computes the sum of all bytes in the specified range and returns the result modulo 64k (0-65535).

**Syntax**:

```
checksum <addr_hex> <len_hex>
```

**Parameters**:

- `addr_hex`: Starting address in hexadecimal
- `len_hex`: Number of bytes to include in checksum (1-65535)

**Example**:

```
> checksum 5000 100
Checksum of $0100 bytes from $5000: $1234

> checksum 0100 4
Checksum of $0004 bytes from $0100: $01DE
```

**Notes**:

- Checksum is calculated as: `sum = 0; for each byte: sum += byte; checksum = sum & 0xFFFF`
- Useful for verifying data integrity or detecting memory corruption
- Maximum range: 65535 bytes per command
- Reads from mapped memory (ROM/RAM) or physical bus for unmapped addresses

**Algorithm**:

```c
uint32_t sum = 0;
for (uint32_t i = 0; i < length; i++) {
    uint8_t value = memory_read_fast(address + i);
    sum += value;
}
uint16_t checksum = sum & 0xFFFF;  // Modulo 64k
```

### read

Read memory contents in hexadecimal. This will display
mapped ROM/RAM from the MCU's RAM, and unmapped areas
will be read directly from the bus.

**Syntax**:

```
read <addr_hex> <len_hex>
```

**Parameters**:

- `addr_hex`: Starting address in hexadecimal
- `len_hex`: Number of bytes to read in hexadecimal

**Example**:

```
> read 5000 40
Reading $0040 bytes from $5000:
5000: 86 42 4E FF FF FF FF 7E 50 00 97 10 7E 50 2E 20
5010: 97 10 7E 50 2E 20 97 10 7E 50 2E 20 AA 01 26 45
5020: 3D 0E 00 01 86 FF 97 53 97 54 3D 27 58 A7 54 58
5030: 0E 00 01 86 FF 97 53 97 54 3D 27 58 A7 54 58 0E
```

**Format**:

- 16 bytes per line
- Address shown at start of each line
- Values in hexadecimal

**Maximum Length**: 256 bytes per command

**Common Uses**:

```
read 0000 100    # Read first 256 bytes of RAM
read 5000 100    # Read first 256 bytes of ROM
read 01FE 2      # Read stack pointer value
read 7FF8 8      # Read interrupt vectors
```

### write

Write data to memory.

**Syntax**:

```
write <addr_hex> <byte1_hex> [byte2_hex] ...
```

**Parameters**:

- `addr_hex`: Starting address in hexadecimal
- `byteN_hex`: Data bytes in hexadecimal (space-separated)

**Example**:

```
> write 0100 DE AD BE EF
OK

> write 0010 42
OK

> write 1000 00 01 02 03 04 05 06 07 08 09 0A 0B 0C 0D 0E 0F
OK
```

**Notes**:

- Writes to ROM are ignored (no error)
- Writes to CMOS ($0100-$01FF) are written through to the target CMOS RAM.
- Can write multiple bytes in one command
- Values are truncated to 8 bits

**Common Uses**:

```
write 0100 42 55 AA FF    # Write config to CMOS
write 0010 00             # Clear a RAM location
write 2100 FF             # Write to PIA (if connected)
```

### status

Display current CPU state.

**Syntax**:

```
status
```

**Example**:

```
> status
Emulator State: RUNNING
CPU Status:
  PC: $F000 (SEI (INH))
  A:  $42
  B:  $55
  X:  $0100
  SP: $01FE
  CCR: $C0 [--NZ--]
  Running: YES
  Halted: NO
  Instructions: 12345
  Cycle Count: 98765
  PIO Cycles: 100000
  Overage: 0
  Underage: 1235
  Speed ratio: 1.75x
  Speed: 0.9999x real-time
  QSPI Bus: 133 MHz (divisor: 2)
  E Clock is stopped
```

**CCR Flags**:

- `H`: Half-carry
- `I`: Interrupt mask
- `N`: Negative
- `Z`: Zero
- `V`: Overflow
- `C`: Carry

**Running States**:

- **Running: YES, Halted: NO**: Normal execution
- **Running: NO, Halted: YES**: Stopped (use `run` to resume)
- **Running: NO, Halted: NO**: Reset but not started

### run

Start CPU execution.

**Syntax**:

```
run
```

**Example**:

```
> run
OK: CPU started
```

**Notes**:

- CPU must be halted before running
- If CPU was reset, starts from reset vector
- If CPU was halted, resumes from current PC
- No output while running (use `status` after `halt`)

**Typical Sequence**:

```
> reset
OK: CMOS saved, CPU reset

> run
OK: CPU started

[CPU executes...]

> halt
OK: CPU halted, CMOS saved

> status
CPU Status:
  PC: $5234
  ...
```

### halt

Stop CPU execution.

**Syntax**:

```
halt
```

**Example**:

```
> halt
OK: CPU halted, CMOS saved
```

**Notes**:

- Automatically saves CMOS to flash
- CPU state preserved (PC, registers)
- Use `run` to resume execution
- Use `status` to inspect state

### reset

Reset CPU to initial state.

**Syntax**:

```
reset
```

**Example**:

```
> reset
OK: CMOS saved, CPU reset
```

**Reset Actions**:

1. Clear all registers (A=0, B=0, X=0)
2. Set SP to 0x0000
3. Set CCR to 0xC4 (I flag set)
4. Read reset vector from $FFFE-$FFFF
5. Load PC with vector value
6. Set CPU to halted state

**After Reset**:

```
> status
CPU Status:
  PC: $5000      ← From reset vector
  A:  $00        ← Cleared
  B:  $00        ← Cleared
  X:  $0000      ← Cleared
  SP: $0000      ← Cleared
  CCR: $C4 [-I---C]  ← Interrupt masked
  Running: NO
  Halted: YES
```

**Note**: CPU is halted after reset. Use `run` to start execution.

### debug on

Enable SPI debug output for debugging bus transactions.

**Syntax**:

```
debug on
```

**Example**:

```
> debug on
OK: Debug SPI enabled
```

**Notes**:

- Enables detailed SPI output showing bus transactions
- Useful for debugging hardware interface issues
- May impact performance when enabled

### debug off

Disable SPI debug output.

**Syntax**:

```
debug off
```

**Example**:

```
> debug off
OK: Debug SPI disabled
```

**Notes**:

- Disables SPI debug output to improve performance
- Debug output is disabled by default

### break

Set a breakpoint at the specified address.

**Syntax**:

```
break <addr_hex>
```

**Parameters**:

- `addr_hex`: Address in hexadecimal where breakpoint should be set

**Example**:

```
> break 5000
OK: Breakpoint set at $5000

> break F000
ERROR: Failed to set breakpoint at $F000 (max 8 breakpoints)
```

**Notes**:

- Maximum of 8 breakpoints supported
- CPU will halt when PC reaches the breakpoint address
- Use `break list` to see all active breakpoints

### break clear

Clear breakpoint(s).

**Syntax**:

```
break clear [<addr_hex>]
```

**Parameters**:

- `addr_hex` (optional): Specific address to clear. If omitted, clears all breakpoints.

**Examples**:

```
> break clear 5000
OK: Breakpoint at $5000 cleared

> break clear
OK: All breakpoints cleared
```

### break list

List all currently set breakpoints.

**Syntax**:

```
break list
```

**Example**:

```
> break list
Breakpoints:
  $5000
  $6000
  $7000
```

**Notes**:

- Shows all active breakpoints
- Displays "No breakpoints set" if none are active

### reg pc

Set the program counter register.

**Syntax**:

```
reg pc <value_hex>
```

**Parameters**:

- `value_hex`: New value for PC in hexadecimal

**Example**:

```
> reg pc 5000
OK: PC set to $5000
```

**Notes**:

- Changes take effect immediately
- Use with caution when CPU is running

### reg a

Set accumulator A register.

**Syntax**:

```
reg a <value_hex>
```

**Parameters**:

- `value_hex`: New value for accumulator A (8-bit, 0x00-0xFF)

**Example**:

```
> reg a 42
OK: A set to $42
```

### reg b

Set accumulator B register.

**Syntax**:

```
reg b <value_hex>
```

**Parameters**:

- `value_hex`: New value for accumulator B (8-bit, 0x00-0xFF)

**Example**:

```
> reg b 55
OK: B set to $55
```

### reg x

Set index register X.

**Syntax**:

```
reg x <value_hex>
```

**Parameters**:

- `value_hex`: New value for index register X (16-bit)

**Example**:

```
> reg x 0100
OK: X set to $0100
```

### reg sp

Set stack pointer register.

**Syntax**:

```
reg sp <value_hex>
```

**Parameters**:

- `value_hex`: New value for stack pointer (16-bit)

**Example**:

```
> reg sp 01FE
OK: SP set to $01FE
```

**Notes**:

- Be careful when changing SP as it affects stack operations
- Typical stack range is $0100-$01FF for CMOS systems

### reg ccr

Set condition code register.

**Syntax**:

```
reg ccr <value_hex>
```

**Parameters**:

- `value_hex`: New value for condition code register (8-bit)

**Example**:

```
> reg ccr C0
OK: CCR set to $C0
```

**CCR Flags**:

- Bit 7: Always 1 (fixed)
- Bit 6: Always 0 (fixed)
- Bit 5: Half-carry (H)
- Bit 4: Interrupt mask (I)
- Bit 3: Negative (N)
- Bit 2: Zero (Z)
- Bit 1: Overflow (V)
- Bit 0: Carry (C)

### bootloader

Enter RP2350 bootloader mode for firmware updates.
This will make a USB MSC drive available named `RP2350`.
Just drop a `.uf2` file in that drive to update the firmware.

**Syntax**:

```
bootloader
```

**Example**:

```
> bootloader
Entering bootloader mode...
[Connection lost]
```

**What Happens**:

1. Emulator enters BOOTSEL mode
2. USB disconnects
3. Board appears as "RP2350" mass storage device
4. Drag `.uf2` file to update firmware

**To Exit Bootloader**:

- Power cycle the board
- Or press the board's `RESET` button
- Or flash new firmware (auto-resets)

**Warning**: This command is irreversible without a power cycle or RESET!

### boot

Enter RP2350 bootloader mode (alias for `bootloader`).

**Syntax**:

```
boot
```

**Example**:

```
> boot
Entering bootloader mode...
[Connection lost]
```

**Notes**:

- Same functionality as `bootloader` command
- Provided as a shorter alternative

### bus_read

Read byte from hardware bus.

**Syntax**:

```
bus_read <addr_hex>
```

**Parameters**:

- `addr_hex`: Address in hexadecimal

**Example**:

```
> bus_read 5000
86
```

**Notes**:

- Reads directly from hardware bus (bypasses mapped memory)
- Useful for debugging hardware interface
- E clock is automatically started if needed

### bus_write

Write byte to hardware bus.

**Syntax**:

```
bus_write <addr_hex> <data_hex>
```

**Parameters**:

- `addr_hex`: Address in hexadecimal
- `data_hex`: Data byte in hexadecimal

**Example**:

```
> bus_write 0100 42
OK
```

**Notes**:

- Writes directly to hardware bus (bypasses mapped memory)
- Useful for debugging hardware interface
- E clock is automatically started if needed

### readb

Read block from hardware bus.

**Syntax**:

```
readb <addr_hex> <len_hex>
```

**Parameters**:

- `addr_hex`: Starting address in hexadecimal
- `len_hex`: Number of bytes to read in hexadecimal

**Example**:

```
> readb 5000 40
Reading $0040 bytes from $5000:
5000: 86 42 4E FF FF FF FF 7E 50 00 97 10 7E 50 2E 20
5010: 97 10 7E 50 2E 20 97 10 7E 50 2E 20 AA 01 26 45
5020: 3D 0E 00 01 86 FF 97 53 97 54 3D 27 58 A7 54 58
5030: 0E 00 01 86 FF 97 53 97 54 3D 27 58 A7 54 58 0E
```

**Notes**:

- Reads directly from hardware bus (bypasses mapped memory)
- Useful for debugging hardware interface
- E clock is automatically started if needed
- Maximum length: 1024 bytes per command

### writeb

Write block to hardware bus.

**Syntax**:

```
writeb <addr_hex> <data_hex...>
```

**Parameters**:

- `addr_hex`: Starting address in hexadecimal
- `data_hex...`: Data bytes in hexadecimal (space-separated)

**Example**:

```
> writeb 0100 DE AD BE EF
OK
```

**Notes**:

- Writes directly to hardware bus (bypasses mapped memory)
- Useful for debugging hardware interface
- E clock is automatically started if needed
- Can write multiple bytes in one command
- Values are truncated to 8 bits

### scan_memory

Auto-detect and configure memory map by scanning the target system's bus.

**Syntax**:

```
scan_memory
```

**Example**:

```
> scan_memory
Starting memory scan...
Scanning 256 pages...
Scan complete
Architecture: Williams System 7

Scan Results:
  $0000-$13FF: RAM
  $5000-$7FFF: ROM
  $0100-$01FF: CMOS

Memory Configuration:
  ROM: $5000-$7FFF (12288 bytes)
  RAM: $0000-$13FF (5120 bytes)
  CMOS: $0100-$01FF (256 bytes)

Copied 12 ROM pages from bus
ROM contents saved to flash
Memory scan and configuration complete
```

**What it does**:

1. **Scans all 256 pages** (256-byte blocks) of the 64KB address space
2. **Detects memory types** by testing each page:
   - **ROM**: Read-only memory with consistent data
   - **RAM**: Read/write memory that passes write tests
   - **CMOS**: Special RAM with high nybble always 0xF (Williams System 7)
   - **PIA**: 6820/6821 Peripheral Interface Adapter chips
   - **Empty**: All 0xFF or all 0x00
3. **Recognizes architecture** based on detected patterns:
   - Williams System 7: CMOS at specific addresses, RAM mirroring
   - Williams System 11: Contiguous RAM at start
4. **Builds memory map** with appropriate mappings and aliases
5. **Copies ROM contents** from bus to persistent flash storage
6. **Saves configuration** to flash for use on next boot

**Notes**:

- Automatically halts emulator during scan if running
- Requires target system to be connected and powered
- May take 10-30 seconds depending on target system
- Configuration is saved and used automatically on reboot
- Useful for initial setup or when changing target systems

### verify_memory

Verify current memory configuration matches hardware.

**Syntax**:

```
verify_memory
```

**Example**:

```
> verify_memory
Verifying memory configuration...
OK: Memory configuration matches hardware

> verify_memory
ERROR: RAM mismatch - run 'scan_memory' to update
```

**What it does**:

1. **Checks saved configuration** against current hardware
2. **Tests RAM regions** by writing/reading test patterns
3. **Validates ROM regions** (should not be writable)
4. **Reports discrepancies** if hardware doesn't match saved map

**Possible Results**:

- `OK: Memory configuration matches hardware` - All good
- `ERROR: RAM mismatch - run 'scan_memory' to update` - RAM not found where expected
- `WARNING: ROM/RAM differs from saved - run 'scan_memory' to update` - Minor differences detected
- `ERROR: No saved memory map - run 'scan_memory' first` - No configuration saved

**Notes**:

- Useful for diagnosing hardware changes or connection issues
- Non-destructive (restores any test data written)
- Run after changing target systems or if emulation seems unstable

## Command Line Editing

### Supported Keys

| Key | Action |
|-----|--------|
| Backspace | Delete previous character |
| Delete | Delete character at cursor |
| Enter | Execute command |
| Ctrl+C | Clear current line |

### Command History

Not currently implemented. Each command must be typed fresh.

## Script Examples

### Initial Setup Script

```bash
# Configure memory (not necessary if these defaults are OK)
config rom 5000 3000
config ram 0000 1400

# Load ROM
load
[paste HEX file]

# Reset and run
reset
run
```

### Debug Script

```bash
# Halt CPU
halt

# Check status
status

# Inspect memory
read 0000 100    # RAM
read 5000 100    # ROM
read 01FE 2      # Stack

# Check CMOS (first 256 bytes)
read 0100 100

# Resume
run
```

### CMOS Test Script

```bash
# Write test pattern
write 0100 DE AD BE EF

# Verify write
read 0100 4

# Simulate power cycle (reset)
reset

# Verify persistence
read 0100 4
```

### ROM Reload Script

```bash
# Halt if running
halt

# Load new ROM
load
[paste HEX file]

# Reset to new code
reset

# Start execution
run
```

## Error Messages

### Common Errors

| Error | Cause | Solution |
|-------|-------|----------|
| `ERROR: Unknown command` | Invalid command | Type `help` for list |
| `ERROR: Failed to load EPROM` | Bad HEX format | Check HEX file |
| `ERROR: Usage: ...` | Wrong parameters | Check command syntax |
| `ERROR: Failed to save CMOS` | Flash error | Retry or check hardware |
| `HEX address $XXXX outside ROM/CMOS range` | Address out of bounds | Check memory config |

### Debug Strategies

1. **Command Not Working**:

   ```
   > help           # Verify command syntax
   > config         # Check current configuration
   > status         # Check CPU state
   ```

2. **Memory Access Issues**:

   ```
   > config         # Verify memory map
   > read [addr] 1  # Test single byte read
   > write [addr] 42
   > read [addr] 1  # Verify write
   ```

3. **Program Not Running**:

   ```
   > halt           # Stop execution
   > read 7FF8 8    # Check vectors
   > status         # Check PC value
   > reset          # Reload from vectors
   > run
   ```

## Tips and Tricks

### Memory Dump to File

**macOS/Linux**:

```bash
screen -L /dev/tty.usbmodem14201
[Type commands]
[Exit: Ctrl+A, K]
# Output saved to screenlog.0
```

### Automated Testing

```bash
# Create test.txt with commands:
load
:10500000864200...
:00000001FF
reset
run
halt
status

# Send via serial:
cat test.txt > /dev/tty.usbmodem14201
```

### CMOS Backup

```bash
# Use read command to dump CMOS to console
> read 0100 100

# To backup CMOS data, copy the output manually
# and recreate as Intel HEX format if needed for restoration
```

### Quick Memory Check

```bash
# Write known pattern
write 0100 AA 55 AA 55

# Read back
read 0100 4

# Should match: AA 55 AA 55
```

## See Also

- [Getting Started](Getting-Started.md) - Quick start guide
- [Memory Map](Memory-Map.md) - Memory layout
- [Architecture](Architecture.md) - System design
- [Hardware Connection](Hardware-Connection.md) - Physical setup
