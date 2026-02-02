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

# MC6800 Emulator Web Control Interface

A web-based interface for controlling the MC6800 emulator via WebSerial API. This single-file HTML application runs entirely in your browser and communicates with the emulator over USB.

## Features

- **CPU Control**: Run, halt, and reset the emulator
- **Real-time Status**: Auto-refreshing CPU state display (PC, registers, flags, speed)
- **ROM Loading**: Upload Intel HEX or binary ROM files
- **IC Auto-detection**: Automatically detects System 7 IC numbers from filenames
- **Terminal**: Full command-line access for advanced users
- **No Installation**: Single HTML file, no dependencies or build process

## Browser Support

**Supported:**

- ✅ Google Chrome 89+
- ✅ Microsoft Edge 89+
- ✅ Opera 75+

**Not Supported:**

- ❌ Firefox (no WebSerial API support)
- ❌ Safari (no WebSerial API support)

## Requirements

- Supported browser (Chrome, Edge, or Opera)
- MC6800 emulator connected via USB
- File must be served over HTTPS or opened from `localhost` (WebSerial security requirement)

## Usage

### 1. Opening the Interface

**Option A: Local file (file://)**

```bash
# Simply open the HTML file in Chrome/Edge
open emulator-control.html
```

**Option B: Local web server (<http://localhost>)**

```bash
# Python 3
cd web-interface
python3 -m http.server 8000

# Then open: http://localhost:8000/emulator-control.html
```

### 2. Connecting to the Emulator

1. Click **"Connect to Emulator"**
2. Select the emulator's USB serial port from the browser dialog
3. The status indicator will change to "Connected" (green)

### 3. CPU Control

- **Run**: Start CPU execution
- **Halt**: Stop CPU
- **Reset**: Reset CPU to initial state

### 4. Monitoring Status

The status panel automatically refreshes every 1 second, showing:

- **PC**: Program Counter
- **A, B**: Accumulators
- **X**: Index Register
- **SP**: Stack Pointer
- **CCR**: Condition Code Register with flags [HINZVC]
- **Running**: YES/NO
- **Instructions**: Total instructions executed
- **Speed**: Execution speed relative to real-time (e.g., "4.21x")

### 5. Loading ROMs

#### Intel HEX Files (.hex)

1. Click **"Choose File"** and select your .hex file
2. Click **"Upload ROM"**
3. Wait for "Upload successful!" message

#### Binary Files (.bin)

**With IC number in filename:**

```
IC26.bin   → Automatically loads to $5800
IC14.bin   → Automatically loads to $6000
IC20.bin   → Automatically loads to $6800
IC17.bin   → Automatically loads to $7000
```

1. Click **"Choose File"** and select your .bin file
2. If IC number detected, click **"Upload ROM"**
3. Otherwise, enter start address (hex, without $) and click **"Upload ROM"**

**System 7 IC Mappings:**

- IC26 → $5800
- IC14 → $6000
- IC20 → $6800
- IC17 → $7000

### 6. Using the Terminal

The terminal provides full access to all emulator commands:

```
> help           # Show all available commands
> status         # Display CPU status
> run            # Start execution
> halt           # Stop execution
> reset          # Reset CPU
> config         # Show memory configuration
> read 5800 20   # Read 32 bytes from $5800
> debug on       # Enable SPI debug output
> debug off      # Disable SPI debug output
```

**Terminal Features:**

- Type commands and press Enter
- Use ↑/↓ arrow keys for command history
- All responses displayed in real-time
- Supports all USB CDC commands

## How It Works

### WebSerial Communication

The interface uses the WebSerial API to communicate with the emulator's USB CDC interface:

1. **Connection**: Opens a serial port connection to the emulator
2. **Commands**: Sends text commands terminated with `\r\n`
3. **Responses**: Receives and parses text responses (OK/ERROR/multi-line)
4. **Binary to HEX**: Converts binary ROM files to Intel HEX format before upload

### Binary to Intel HEX Conversion

Binary files are automatically converted to Intel HEX format:

1. File split into 16-byte records
2. Each record includes:
   - Byte count
   - Address
   - Record type (0x00 = data)
   - Data bytes
   - Checksum (two's complement)
3. EOF record added (`:00000001FF`)
4. Sent via `load` command to emulator

### Status Parsing

The status display extracts information from the emulator's `status` command output using regular expressions:

```
CPU Status:
  PC: $E800
  A:  $00
  B:  $00
  X:  $0000
  SP: $0000
  CCR: $D0 [-I----]
  Running: NO
  Halted: YES
  Instructions: 0
  Cycle Count: 0
  PIO Cycles: 0
  Speed: 0.00x real-time
```

## Troubleshooting

### "WebSerial API not supported"

- Use Chrome, Edge, or Opera browser
- Update to latest version

### "Connection failed"

- Check emulator is connected via USB
- Verify emulator firmware is running
- Try disconnecting and reconnecting USB cable

### "Upload timeout"

- File may be too large
- Connection may be unstable
- Try uploading again

### Status not updating

- Check connection is active
- Look for error messages in terminal
- Reconnect to emulator

### Binary file address errors

- Ensure filename includes IC number (e.g., "IC26.bin")
- Or manually enter start address in hex (without $ prefix)
- Verify address is correct for your ROM

## Technical Details

### File Structure

- **Single HTML file**: ~1000 lines
- **No dependencies**: Pure HTML/CSS/JavaScript
- **Classes**:
  - `EmulatorConnection`: WebSerial connection manager
  - `ROMLoader`: ROM file handling and conversion
  - `StatusParser`: Status text parsing
  - `UI`: Main UI controller and event handling

### Command Protocol

- **Format**: Text commands with `\r\n` termination
- **Responses**: `OK` or `ERROR` prefixes, multi-line for status/read/dump
- **HEX Loading**: `load` → HEX data → `end` sequence

### Performance

- Auto-refresh: 1 second interval
- Command timeout: 5 seconds for ROM upload
- Terminal buffer: Last 50 lines kept for parsing
- Connection: Asynchronous, non-blocking

## License

Part of the MC6800 Emulator project.

## Support

For issues or questions:

1. Check the terminal for error messages
2. Verify browser console for JavaScript errors
3. Review the USB CDC command documentation in `src/usb_cdc.c`
