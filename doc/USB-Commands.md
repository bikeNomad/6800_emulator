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
| `config` | none | Show memory configuration |
| `config rom` | base size | Configure ROM region |
| `config ram` | base size | Configure RAM region |
| `cmos save` | none | Save CMOS to flash |
| `cmos dump` | none | Display CMOS contents |
| `read` | addr len | Read memory |
| `write` | addr data... | Write memory |
| `status` | none | Display CPU status |
| `run` | none | Start CPU execution |
| `halt` | none | Stop CPU execution |
| `reset` | none | Reset CPU |
| `cycletest` | none | Test instruction cycles |
| `bootloader` | none | Enter bootloader mode |

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
  load                      - Load Intel HEX (auto-detects ROM/CMOS)
  config                    - Show memory configuration
  config rom <b> <s>        - Configure ROM region
  config ram <b> <s>        - Configure RAM region
  cmos save                 - Manually save CMOS to flash
  cmos dump                 - Display CMOS RAM contents
  read <addr> <len>         - Read memory
  write <addr> <data>       - Write memory
  status                    - Display CPU status
  run                       - Start CPU execution
  halt                      - Stop CPU execution (auto-saves CMOS)
  reset                     - Reset CPU (auto-saves CMOS)
  cycletest                 - Test instruction cycle counts
  bootloader                - Enter bootloader mode
  help                      - Show this help
```

### load

Load Intel HEX file (ROM or CMOS). The emulator automatically detects the data type based on addresses in the HEX file.

**Syntax**:
```
load
[paste Intel HEX data]
[EOF record :00000001FF automatically detected]
```

**Address Detection**:
- Addresses `$5000-$7FFF` → ROM (flash at 0x100000)
- Addresses `$0100-$01FF` → CMOS (flash at 0x108000)

**Example**:
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
- CMOS region ($0100-$01FF) is always persistent

**Common Configurations**:
```
config ram 0000 1400    # 5KB at $0000 (default)
config ram 0000 2000    # 8KB at $0000 (maximum)
config ram 0000 0400    # 1KB at $0000 (minimal)
config ram 8000 1000    # 4KB at $8000 (non-standard)
```

### cmos save

Manually save CMOS RAM to flash. Normally not needed as CMOS auto-saves on halt/reset or after 30 seconds idle.

**Syntax**:
```
cmos save
```

**Example**:
```
> cmos save
Saving CMOS RAM to flash...
Flash programming complete
Flash verification OK
CMOS saved successfully
OK: CMOS saved to flash
```

**When to Use**:
- Before power-off (extra safety)
- After critical configuration changes
- Testing CMOS persistence
- Manual backup

### cmos dump

Display complete CMOS RAM contents in hexadecimal.

**Syntax**:
```
cmos dump
```

**Example**:
```
> cmos dump
CMOS RAM ($0100-$01FF):
0100: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0110: DE AD BE EF 00 00 00 00 00 00 00 00 00 00 00 00
0120: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0130: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0140: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0150: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0160: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0170: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0180: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
0190: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01A0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01B0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01C0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01D0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01E0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
01F0: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

**Format**:
- 16 bytes per line
- Address shown at start of each line
- Values in hexadecimal

### read

Read memory contents in hexadecimal.

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
- Writes to CMOS ($0100-$01FF) trigger auto-save
- Can write multiple bytes in one command
- Values wrap at byte boundary (e.g., 100 → 00)

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
CPU Status:
  PC: $5000
  A:  $42
  B:  $55
  X:  $0100
  SP: $01FE
  CCR: $C0 [--NZ--]
  Running: YES
  Halted: NO
  Instructions: 12345
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
1. Save CMOS to flash
2. Clear all registers (A=0, B=0, X=0)
3. Set SP to 0x0000
4. Set CCR to 0xC4 (I flag set)
5. Read reset vector from $FFFE-$FFFF
6. Load PC with vector value
7. Set CPU to halted state

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

### cycletest

Test cycle-accurate execution of all implemented instructions.

**Syntax**:
```
cycletest
```

**Example** (partial output):
```
> cycletest
Running cycle count test...

========================================
MC6800 Instruction Cycle Count Test
========================================

Format: $XX MNEMONIC : N cycles

$01 NOP      : 2 cycles
$02 NOP      : 2 cycles
$04 NOP      : 2 cycles
$05 NOP      : 2 cycles
$06 TAP      : 2 cycles
$07 TPA      : 2 cycles
$08 INX      : 4 cycles
$09 DEX      : 4 cycles
$0A CLV      : 2 cycles
$0B SEV      : 2 cycles
$0C CLC      : 2 cycles
$0D SEC      : 2 cycles
$0E CLI      : 2 cycles
$0F SEI      : 2 cycles
$10 SBA      : 2 cycles
...
$86 LDAA#    : 2 cycles
$96 LDAA     : 3 cycles
$A6 LDAA,X   : 5 cycles
$B6 LDAA     : 4 cycles
...

========================================
Test Complete
========================================

Cycle test complete.
```

**Purpose**:
- Verify cycle-accurate emulation
- Test all implemented instructions
- Compare against MC6800 datasheet
- Useful for debugging timing issues

### bootloader

Enter RP2350 bootloader mode for firmware updates.

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
3. Pico appears as "RPI-RP2" mass storage device
4. Drag `.uf2` file to update firmware

**To Exit Bootloader**:
- Power cycle the Pico
- Or flash new firmware (auto-resets)

**Warning**: This command is irreversible without power cycle!

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
# Configure memory
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

# Check CMOS
cmos dump

# Resume
run
```

### CMOS Test Script

```bash
# Write test pattern
write 0100 DE AD BE EF

# Verify write
read 0100 4

# Save to flash
cmos save

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

### Quick Reset

```
reset;run
```
(Not currently supported - use separate commands)

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
# Dump CMOS to file
> cmos dump > cmos_backup.txt

# To restore (create HEX):
# :10010000[data]
# :00000001FF
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
