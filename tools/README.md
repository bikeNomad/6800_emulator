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

# MC6800 Emulator Tools

This directory contains Python utilities for working with the MC6800 emulator firmware.

## Communication Tools

### download_memory.py

Download memory ranges from the emulator to an Intel HEX file via USB CDC.

**Features:**
- Download single or multiple memory ranges
- Saves to standard Intel HEX format
- Progress reporting and error handling
- Range validation and overlap detection
- Summary statistics

**Usage:**
```bash
./download_memory.py <serial_port> <output_file> <addr1:len1> [addr2:len2] [...]
```

**Examples:**

Download ROM region (12KB from $5000):
```bash
./download_memory.py /dev/tty.usbmodem14201 rom_backup.hex 5000:3000
```

Download multiple regions (RAM + ROM):
```bash
./download_memory.py /dev/ttyACM0 full_backup.hex 0000:1400 5000:3000
```

Download entire 64K address space:
```bash
./download_memory.py /dev/tty.usbmodem14201 full_dump.hex 0000:10000
```

Download CMOS region (256 bytes):
```bash
./download_memory.py /dev/tty.usbmodem14201 cmos.hex 0100:100
```

**Output Format:**

Standard Intel HEX format with:
- Data records (16 bytes per record)
- Proper checksums
- EOF record
- Compatible with assemblers, EPROM programmers, and the `load` command

**Range Format:**

Ranges are specified as `addr:len` where both values are hexadecimal:
- `5000:3000` = 12KB starting at $5000
- `0000:1400` = 5KB starting at $0000
- `0100:100` = 256 bytes starting at $0100

**Error Handling:**

The tool validates:
- Serial port connectivity
- Address range bounds (0-FFFF)
- Length limits (1-10000)
- Range overlaps (warning)
- Emulator responses

**Exit Codes:**
- `0` = Success
- `1` = Error (invalid arguments, connection failure, download failure)

### load_hex.py

Load Intel HEX files to the emulator via USB CDC.

**Usage:**
```bash
./load_hex.py <serial_port> <hex_file>
```

**Examples:**
```bash
./load_hex.py /dev/tty.usbmodem14201 game.hex
./load_hex.py /dev/ttyACM0 pinball.hex
```

**Features:**
- Progress reporting during upload
- Automatic chunking to avoid buffer overflow
- Response validation
- Error detection

**Dependencies:**
- Python 3.6+
- pyserial (`pip install pyserial`)

## Conversion Tools

### rom2hex.py

Convert binary ROM files to Intel HEX format.

**Usage:**
```bash
./rom2hex.py <start_address> <rom_file1> [rom_file2] [...] > output.hex
```

**Examples:**

Convert single ROM:
```bash
./rom2hex.py 5000 game.bin > game.hex
```

Concatenate multiple ROMs:
```bash
./rom2hex.py D000 ic26.bin ic14.bin ic20.bin ic17.bin > system7.hex
```

Williams System 7 typical layout:
```bash
# IC26 at $5800 (2KB)
./rom2hex.py 5800 ic26.bin > ic26.hex

# IC14 at $6000 (2KB)
./rom2hex.py 6000 ic14.bin > ic14.hex

# IC20 at $6800 (2KB)
./rom2hex.py 6800 ic20.bin > ic20.hex

# IC17 at $7000 (2KB)
./rom2hex.py 7000 ic17.bin > ic17.hex

# Or all at once starting at $5800
./rom2hex.py 5800 ic26.bin ic14.bin ic20.bin ic17.bin > system7_full.hex
```

**Features:**
- Supports multiple input files (concatenated)
- Automatic duplicate detection (e.g., mirrored ROMs)
- 16 bytes per line (standard format)
- Progress output to stderr, HEX to stdout

### make_test_hex.py

Generate test Intel HEX files for development and testing.

**Usage:**
```bash
./make_test_hex.py
```

Generates test patterns for various scenarios.

## Verification Tools

### verify_implementation.py

Verify emulator instruction implementation against MC6800 specification.

**Usage:**
```bash
./verify_implementation.py
```

Checks that all MC6800 instructions are implemented with correct:
- Opcodes
- Cycle counts
- Addressing modes

### verify_opcodes.py

Verify opcode definitions match MC6800 datasheet.

**Usage:**
```bash
./verify_opcodes.py
```

### verify_cycles.py

Verify instruction cycle counts against specification.

**Usage:**
```bash
./verify_cycles.py
```

### double_check_opcodes.py

Cross-reference opcode table with implementation.

**Usage:**
```bash
./double_check_opcodes.py
```

## Checksum Tools

### checksum16.py

Calculate 16-bit checksums for ROM verification.

**Usage:**
```bash
./checksum16.py <rom_file>
```

### checksum32.py

Calculate 32-bit checksums for ROM verification.

**Usage:**
```bash
./checksum32.py <rom_file>
```

## Debug Tools

### extract_spi_data.py

Extract and decode SPI debug output from logic analyzer captures.

**Usage:**
```bash
./extract_spi_data.py <capture_file>
```

Decodes the emulator's SPI debug stream showing:
- Program counter values
- R/W signals
- Data bus values
- Instruction execution trace

## Common Workflows

### Backup Current ROM

```bash
# Download ROM region
./download_memory.py /dev/tty.usbmodem14201 rom_backup.hex 5000:3000

# Verify checksum
./checksum16.py rom_backup.hex
```

### Load and Test ROM

```bash
# Convert binary to HEX
./rom2hex.py 5000 game.bin > game.hex

# Upload to emulator
./load_hex.py /dev/tty.usbmodem14201 game.hex

# If needed, download and verify
./download_memory.py /dev/tty.usbmodem14201 verify.hex 5000:3000
diff game.hex verify.hex
```

### Full System Backup

```bash
# Download all regions
./download_memory.py /dev/tty.usbmodem14201 full_backup.hex \
    0000:1400 \  # RAM
    5000:3000    # ROM

# Compress for archival
gzip full_backup.hex
```

### Development Cycle

```bash
# 1. Backup current ROM
./download_memory.py /dev/tty.usbmodem14201 old_rom.hex 5000:1000

# 2. Assemble new code
as6800 -l mycode.asm -o mycode.hex

# 3. Upload to emulator
./load_hex.py /dev/tty.usbmodem14201 mycode.hex

# 4. Test via USB commands
screen /dev/tty.usbmodem14201
> reset
> run
> status

# 5. If problems, restore backup
./load_hex.py /dev/tty.usbmodem14201 old_rom.hex
```

## Dependencies

All tools require Python 3.6 or later.

### Required Packages

Install with pip:
```bash
pip install pyserial
```

Or using the requirements file (if available):
```bash
pip install -r requirements.txt
```

### Platform-Specific Notes

**macOS:**
- Serial ports typically appear as `/dev/tty.usbmodem*`
- Use `ls /dev/tty.usb*` to find the correct port

**Linux:**
- Serial ports typically appear as `/dev/ttyACM*`
- May need to add user to `dialout` group: `sudo usermod -a -G dialout $USER`
- Use `ls /dev/ttyACM*` to find the correct port

**Windows:**
- Serial ports appear as `COM3`, `COM4`, etc.
- Check Device Manager for the correct port
- Path format: `COM3` (no `/dev/` prefix)

## Data Formats

### Intel HEX Format

All tools use standard Intel HEX format:
```
:LLAAAATTDD...DDCC
```

Where:
- `LL` = Byte count (hex)
- `AAAA` = Address (hex)
- `TT` = Record type (00=data, 01=EOF)
- `DD...DD` = Data bytes
- `CC` = Checksum (two's complement)

Example:
```
:10500000864E7E400086FF97109710971097109755
:00000001FF
```

### Range Specifications

Memory ranges use the format `addr:len` where both are hexadecimal:

- No `$` or `0x` prefix
- Both address and length in hex
- Example: `5000:3000` means "12KB starting at $5000"

## Error Handling

### Serial Port Issues

If you get "Permission denied" or "Device not found":

**Linux:**
```bash
# Check port exists
ls /dev/ttyACM*

# Add user to dialout group
sudo usermod -a -G dialout $USER

# Log out and back in for group to take effect
```

**macOS:**
```bash
# Check port exists
ls /dev/tty.usbmodem*

# No special permissions needed on macOS
```

### Connection Timeouts

If downloads timeout:
- Check emulator is powered and running
- Try resetting the emulator (press RESET button)
- Verify USB cable is data-capable (not charge-only)
- Try a different USB port
- Close other programs using the serial port

### Checksum Errors

If downloaded file has wrong checksum:
- Memory may be corrupted
- Bus interface issue (for unmapped regions)
- Try downloading again
- Verify with `read` command first

## Contributing

When adding new tools:
1. Follow the existing code style
2. Include docstring with usage examples
3. Add error handling and validation
4. Update this README
5. Make script executable (`chmod +x`)
6. Test on multiple platforms if possible

## Reference Files

- `opcodes_and_cycles.tsv` - MC6800 instruction reference table
- Contains opcode mappings, cycle counts, and addressing modes
- Used by verification tools

## Support

For issues or questions:
- Check the main project documentation
- Review USB Commands reference (`doc/USB-Commands.md`)
- File issues on the project GitHub repository
