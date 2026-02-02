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

# Memory Download Quick Reference

## USB Command

```
download <addr> <len>
```

**Examples**:
```
download 5000 3000     # Download 12KB ROM
download 0000 1400     # Download 5KB RAM
download 0100 100      # Download 256B CMOS
download FFF8 8        # Download vectors
```

**Output**: Intel HEX records to console

## Python Tool

```bash
./tools/download_memory.py <port> <file> <addr:len> [addr:len...]
```

**Examples**:
```bash
# Single range
./tools/download_memory.py /dev/tty.usbmodem14201 rom.hex 5000:3000

# Multiple ranges
./tools/download_memory.py /dev/tty.usbmodem14201 full.hex \
    0000:1400 5000:3000 0100:100

# macOS
./tools/download_memory.py /dev/tty.usbmodem14201 backup.hex 5000:3000

# Linux
./tools/download_memory.py /dev/ttyACM0 backup.hex 5000:3000

# Windows
python tools/download_memory.py COM3 backup.hex 5000:3000
```

**Output**: Intel HEX file

## Common Memory Ranges

| Region | Address | Size | Purpose |
|--------|---------|------|---------|
| RAM | 0000-13FF | 5KB | System 7 RAM |
| CMOS | 0100-01FF | 256B | Persistent settings |
| ROM | 5000-7FFF | 12KB | Common ROM region |
| ROM | 4000-7FFF | 16KB | Full ROM space |
| Vectors | FFF8-FFFF | 8B | Reset/IRQ/NMI vectors |

## Williams System 7 IC Layout

| IC | Address | Size |
|----|---------|------|
| IC26 | 5800 | 2KB |
| IC14 | 6000 | 2KB |
| IC20 | 6800 | 2KB |
| IC17 | 7000 | 2KB |

**Download all**:
```bash
./tools/download_memory.py /dev/tty.usbmodem14201 system7.hex \
    5800:800 6000:800 6800:800 7000:800
```

## Workflows

### Quick Backup
```bash
./tools/download_memory.py $PORT backup.hex 5000:3000
```

### Development Cycle
```bash
# 1. Backup
./tools/download_memory.py $PORT old.hex 5000:1000

# 2. Upload new code
./tools/load_hex.py $PORT new.hex

# 3. Test
screen $PORT
> reset
> run

# 4. Restore if needed
./tools/load_hex.py $PORT old.hex
```

### Full Backup
```bash
DATE=$(date +%Y%m%d)
./tools/download_memory.py $PORT backup_$DATE.hex 0000:10000
```

### Verification
```bash
# Upload
./tools/load_hex.py $PORT original.hex

# Download and compare
./tools/download_memory.py $PORT verify.hex 5000:3000
diff original.hex verify.hex
```

## Error Messages

| Error | Meaning | Solution |
|-------|---------|----------|
| `Address out of range` | addr > FFFF | Use valid address |
| `Length must be 1-65536` | Invalid length | Check length |
| `Block exceeds address space` | addr+len > 10000 | Reduce length |
| `Error opening serial port` | Port not found | Check port name |
| `Timeout waiting for response` | No response | Reset emulator |

## Intel HEX Format

```
:LLAAAATTDD...DDCC
```

- `:` = Start
- `LL` = Byte count (hex)
- `AAAA` = Address (hex)
- `TT` = Type (00=data, 01=EOF)
- `DD...DD` = Data bytes
- `CC` = Checksum

**Example**:
```
:10500000864E7E400086FF97109710971097109755
:00000001FF
```

## Port Names

**macOS**: `/dev/tty.usbmodem*`
```bash
ls /dev/tty.usbmodem*
```

**Linux**: `/dev/ttyACM*`
```bash
ls /dev/ttyACM*
sudo usermod -a -G dialout $USER  # If permission denied
```

**Windows**: `COM3`, `COM4`, etc.
- Check Device Manager

## Dependencies

**Python tool**:
```bash
pip install pyserial
```

## Help

**USB command**:
```
> help
```

**Python tool**:
```bash
./tools/download_memory.py
```

**Documentation**:
- `doc/Download-Memory-Guide.md` - Full guide
- `doc/USB-Commands.md` - All commands
- `tools/README.md` - All tools
