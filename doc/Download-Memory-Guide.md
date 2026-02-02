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

# Memory Download Guide

## Overview

The MC6800 Emulator provides two methods for downloading (exporting) memory contents:

1. **USB Command**: `download <addr> <len>` - Interactive command-line access
2. **Python Tool**: `download_memory.py` - Automated scripting and batch operations

Both methods export memory in standard Intel HEX format, which can be:
- Reloaded into the emulator with the `load` command
- Burned to EPROMs with standard programmers
- Analyzed with disassemblers and hex editors
- Archived for backup purposes

## Quick Start

### Using USB Command (Interactive)

```bash
# Connect to emulator
screen /dev/tty.usbmodem14201

# Download 12KB ROM starting at $5000
> download 5000 3000

# Output appears as Intel HEX records
:10500000864E7E400086FF97109710971097109755
:105010007E50001097109710971097109710971085
[... more records ...]
:00000001FF
OK: Downloaded 12288 bytes
```

### Using Python Tool (Scripted)

```bash
# Download same region to file
./tools/download_memory.py /dev/tty.usbmodem14201 rom_backup.hex 5000:3000

# Output:
✓ Connected to /dev/tty.usbmodem14201
Downloading 1 memory range(s):
  Downloading $5000:$3000 (12288 bytes)... ✓
✓ Saved 768 records to rom_backup.hex
  File size: 25344 bytes

Download Summary:
  Ranges downloaded: 1
  Bytes requested:   12288 ($3000)
  Bytes received:    12288 ($3000)
  Intel HEX records: 768

Memory Ranges:
  $5000-$7FFF  (12288 bytes)

✓ Download complete!
```

## USB Command Method

### Basic Usage

**Syntax:**
```
download <addr_hex> <len_hex>
```

**Parameters:**
- `addr_hex`: Starting address (0000-FFFF)
- `len_hex`: Number of bytes (1-10000)

### Examples

**Download ROM:**
```
> download 5000 4000
Downloading $4000 bytes from $5000 as Intel HEX:
[Intel HEX records...]
:00000001FF
OK: Downloaded 16384 bytes
```

**Download RAM:**
```
> download 0000 1400
Downloading $1400 bytes from $0000 as Intel HEX:
[Intel HEX records...]
:00000001FF
OK: Downloaded 5120 bytes
```

**Download CMOS:**
```
> download 0100 100
Downloading $0100 bytes from $0100 as Intel HEX:
[Intel HEX records...]
:00000001FF
OK: Downloaded 256 bytes
```

**Download Reset Vectors:**
```
> download FFF8 8
Downloading $0008 bytes from $FFF8 as Intel HEX:
:08FFF800000000000000000077
:00000001FF
OK: Downloaded 8 bytes
```

### Capturing Output

**Using screen (macOS/Linux):**
```bash
# Start screen with logging
screen -L /dev/tty.usbmodem14201

# Download memory
> download 5000 3000

# Press Ctrl+A, then H to toggle logging
# File saved as screenlog.0

# Rename the log
mv screenlog.0 rom_backup.hex
```

**Using minicom:**
```bash
# Start with logging
minicom -D /dev/tty.usbmodem14201 -C rom_backup.log

# Download memory
> download 5000 3000

# Exit minicom - log contains output
# Extract just the HEX records from the log
```

**Using script redirection:**
```bash
# For simple automation
(echo "download 5000 3000"; sleep 2) | screen /dev/tty.usbmodem14201 > output.txt

# Extract Intel HEX records
grep "^:" output.txt > rom_backup.hex
```

### Limitations

- Manual capture requires text selection/copy-paste
- No automatic file saving
- Must manually filter out command echo and status messages
- Better suited for quick checks than batch operations

## Python Tool Method

### Installation

```bash
# Install dependency
pip install pyserial

# Make script executable (if needed)
chmod +x tools/download_memory.py
```

### Basic Usage

**Syntax:**
```bash
./download_memory.py <serial_port> <output_file> <addr1:len1> [addr2:len2] [...]
```

**Range Format:**
- `addr:len` where both are hexadecimal (no prefix)
- Example: `5000:3000` = 12KB at $5000

### Single Range Examples

**Download Williams System 7 ROM:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 system7_rom.hex 5800:2000
```

**Download RAM region:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 ram_contents.hex 0000:1400
```

**Download CMOS:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 cmos_backup.hex 0100:100
```

**Download vectors:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 vectors.hex FFF8:8
```

### Multiple Range Examples

**Download RAM + ROM:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 full_backup.hex \
    0000:1400 \    # 5KB RAM
    5000:3000      # 12KB ROM
```

**Download System 7 in segments:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 system7_all.hex \
    5800:800 \     # IC26 (2KB)
    6000:800 \     # IC14 (2KB)
    6800:800 \     # IC20 (2KB)
    7000:800       # IC17 (2KB)
```

**Download non-contiguous regions:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 scattered.hex \
    0100:100 \     # CMOS
    2000:1000 \    # Peripheral region
    5000:3000      # ROM
```

### Advanced Features

**Progress Reporting:**
```
Downloading 3 memory range(s):
  Downloading $0000:$1400 (5120 bytes)... ✓
  Downloading $5000:$3000 (12288 bytes)... ✓
  Downloading $0100:$0100 (256 bytes)... ✓
✓ Saved 1088 records to full_backup.hex
```

**Summary Statistics:**
```
Download Summary:
  Ranges downloaded: 3
  Bytes requested:   17664 ($4500)
  Bytes received:    17664 ($4500)
  Intel HEX records: 1088

Memory Ranges:
  $0000-$13FF  (5120 bytes)
  $0100-$01FF  (256 bytes)
  $5000-$7FFF  (12288 bytes)
```

**Error Detection:**
```bash
# Invalid address
./tools/download_memory.py /dev/tty.usbmodem14201 out.hex 20000:100
✗ Error parsing ranges: Address $20000 exceeds 64K address space

# Invalid length
./tools/download_memory.py /dev/tty.usbmodem14201 out.hex 5000:20000
✗ Error parsing ranges: Length $20000 must be 1-65536 bytes

# Range exceeds space
./tools/download_memory.py /dev/tty.usbmodem14201 out.hex FF00:200
✗ Error parsing ranges: Range $FF00:$0200 exceeds address space
```

**Overlap Warning:**
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 out.hex 5000:2000 6000:1000
✗ Warning: Ranges $5000:$2000 and $6000:$1000 overlap
```

### Platform-Specific Usage

**macOS:**
```bash
# Find port
ls /dev/tty.usbmodem*

# Download
./tools/download_memory.py /dev/tty.usbmodem14201 backup.hex 5000:3000
```

**Linux:**
```bash
# Find port
ls /dev/ttyACM*

# May need permissions
sudo usermod -a -G dialout $USER
# Log out and back in

# Download
./tools/download_memory.py /dev/ttyACM0 backup.hex 5000:3000
```

**Windows:**
```bash
# Check Device Manager for COM port

# Download
python tools/download_memory.py COM3 backup.hex 5000:3000
```

## Use Cases

### 1. Regular Backups

**Daily ROM backup:**
```bash
#!/bin/bash
DATE=$(date +%Y%m%d)
./tools/download_memory.py /dev/tty.usbmodem14201 \
    backups/rom_$DATE.hex 5000:3000
```

**Weekly full backup:**
```bash
#!/bin/bash
DATE=$(date +%Y%m%d)
./tools/download_memory.py /dev/tty.usbmodem14201 \
    backups/full_$DATE.hex 0000:10000
```

### 2. Version Control

**Before making changes:**
```bash
# Save current state
./tools/download_memory.py /dev/tty.usbmodem14201 \
    versions/v1_original.hex 5000:3000

# Load modified code
./tools/load_hex.py /dev/tty.usbmodem14201 modified.hex

# Test...

# Revert if needed
./tools/load_hex.py /dev/tty.usbmodem14201 \
    versions/v1_original.hex
```

### 3. Development Workflow

**Save, modify, test, restore:**
```bash
# 1. Backup current ROM
./tools/download_memory.py /dev/tty.usbmodem14201 \
    old_rom.hex 5000:1000

# 2. Develop and assemble
vim mycode.asm
as6800 -l mycode.asm -o mycode.hex

# 3. Upload
./tools/load_hex.py /dev/tty.usbmodem14201 mycode.hex

# 4. Test (via USB or web interface)
screen /dev/tty.usbmodem14201
> reset
> run
> status

# 5. Download result for analysis
./tools/download_memory.py /dev/tty.usbmodem14201 \
    result.hex 5000:1000

# 6. If problems, restore
./tools/load_hex.py /dev/tty.usbmodem14201 old_rom.hex
```

### 4. ROM Extraction

**Extract from running system:**
```bash
# Download entire ROM space
./tools/download_memory.py /dev/tty.usbmodem14201 \
    extracted_rom.hex 4000:4000

# Disassemble
da68 extracted_rom.hex > disassembly.asm

# Analyze
less disassembly.asm
```

### 5. Verification

**Verify upload integrity:**
```bash
# Upload ROM
./tools/load_hex.py /dev/tty.usbmodem14201 original.hex

# Download back
./tools/download_memory.py /dev/tty.usbmodem14201 \
    verify.hex 5000:3000

# Compare
diff original.hex verify.hex
echo $?  # Should be 0 if identical
```

### 6. CMOS Configuration Backup

**Save CMOS settings:**
```bash
# Save before power cycle
./tools/download_memory.py /dev/tty.usbmodem14201 \
    cmos_settings.hex 0100:100

# After power cycle, restore if needed
./tools/load_hex.py /dev/tty.usbmodem14201 cmos_settings.hex
```

### 7. Archival

**Create dated archive:**
```bash
#!/bin/bash
# Full system snapshot
DATE=$(date +%Y%m%d_%H%M%S)
ARCHIVE_DIR="archives/$DATE"
mkdir -p "$ARCHIVE_DIR"

# Download all regions
./tools/download_memory.py /dev/tty.usbmodem14201 \
    "$ARCHIVE_DIR/ram.hex" 0000:1400

./tools/download_memory.py /dev/tty.usbmodem14201 \
    "$ARCHIVE_DIR/rom.hex" 5000:3000

./tools/download_memory.py /dev/tty.usbmodem14201 \
    "$ARCHIVE_DIR/cmos.hex" 0100:100

# Create archive
cd archives
tar czf "${DATE}.tar.gz" "$DATE"
rm -rf "$DATE"

echo "Archive created: archives/${DATE}.tar.gz"
```

## Comparison: Command vs Tool

| Feature | USB Command | Python Tool |
|---------|-------------|-------------|
| **Setup** | None (built-in) | Requires Python + pyserial |
| **Usage** | Interactive | Command-line/scripting |
| **Output** | Console (manual capture) | Automatic file saving |
| **Multiple Ranges** | Manual (one at a time) | Automatic (single file) |
| **Progress** | Line-by-line | Per-range with summary |
| **Error Handling** | Basic | Comprehensive |
| **Automation** | Difficult | Easy |
| **Speed** | Same | Same |
| **Best For** | Quick checks, testing | Backups, automation, production |

## Tips and Best Practices

### File Naming

Use descriptive names with metadata:
```bash
# Include date
rom_backup_20260202.hex

# Include address range
rom_5000_3000.hex

# Include description
system7_black_knight_ic26.hex

# Include version
game_v1.2_rom.hex
```

### Verification

Always verify important downloads:
```bash
# Download
./tools/download_memory.py /dev/tty.usbmodem14201 \
    important.hex 5000:3000

# Upload back to temporary location (if supported)
# Or reload and verify

# Check file size
ls -lh important.hex

# View first few records
head important.hex

# Check EOF record
tail -1 important.hex
# Should be: :00000001FF
```

### Automation

Create shell scripts for common tasks:
```bash
#!/bin/bash
# backup_system.sh

PORT="/dev/tty.usbmodem14201"
DATE=$(date +%Y%m%d)
BACKUP_DIR="backups"

mkdir -p "$BACKUP_DIR"

echo "Backing up system..."

# RAM
./tools/download_memory.py "$PORT" \
    "$BACKUP_DIR/ram_$DATE.hex" 0000:1400

# ROM
./tools/download_memory.py "$PORT" \
    "$BACKUP_DIR/rom_$DATE.hex" 5000:3000

# CMOS
./tools/download_memory.py "$PORT" \
    "$BACKUP_DIR/cmos_$DATE.hex" 0100:100

echo "Backup complete: $BACKUP_DIR/*_$DATE.hex"
```

### Performance

For large downloads:
- 16KB takes ~1-2 seconds
- 64KB takes ~4-8 seconds
- Network/overhead is minimal
- USB CDC speed is the limiting factor

## Troubleshooting

### Connection Issues

**Problem:** "Error opening serial port"
```bash
# Check port exists
ls /dev/tty.usb* /dev/ttyACM*

# Check permissions (Linux)
ls -l /dev/ttyACM0
sudo usermod -a -G dialout $USER

# Try different port
./tools/download_memory.py /dev/ttyACM1 out.hex 5000:1000
```

### Timeout Issues

**Problem:** "Timeout waiting for response"
```bash
# Check emulator is running
# Press RESET button on board

# Try again with longer timeout
# (Edit script timeout parameter if needed)

# Check USB cable
# Try different cable or port
```

### Invalid Data

**Problem:** Downloaded file has wrong content
```bash
# Verify with read command first
screen /dev/tty.usbmodem14201
> read 5000 10

# Compare with download
./tools/download_memory.py /dev/tty.usbmodem14201 \
    test.hex 5000:10

# Check for bus issues if accessing unmapped regions
```

### File Issues

**Problem:** "Permission denied" writing file
```bash
# Check directory permissions
ls -ld .

# Use absolute path
./tools/download_memory.py /dev/tty.usbmodem14201 \
    /tmp/backup.hex 5000:3000

# Check disk space
df -h .
```

## See Also

- [USB Commands Reference](USB-Commands.md) - Complete command documentation
- [Getting Started Guide](Getting-Started.md) - Initial setup
- [Web Interface Guide](Web-Interface.md) - Browser-based control
- [Tools README](../tools/README.md) - All utility scripts
