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

# Getting Started with the MC6800 Emulator

## Quick Start (5 Minutes)

### What You Need

- NED_SYS7 board
- USB cable (data-capable)
- Computer with terminal software
- MC6800 program in Intel HEX format (optional)

### Step 1: Flash the Firmware

1. **Download** the latest `mc6800_emulator.uf2` file
2. **Connect USB** cable
3. **Press and hold BOOT** button on NED_SYS7 board
4. **Press RESET** button on board, relase both buttons
5. **Drag** `.uf2` file to `RP2350` drive
6. **Wait** for board to reboot (drive disappears)

The emulator is now running!

### Step 2: Connect via Serial Terminal

**macOS/Linux**:

```bash
screen /dev/tty.usbmodem14201
```

**Windows**:

- Open PuTTY or TeraTerm
- Select COM port (check Device Manager)
- Connect (any baud rate)

**Arduino IDE**:

- Tools → Port → Select USB device
- Tools → Serial Monitor

### Step 3: Verify Connection

Type `help` and press Enter:

```
> help
MC6800 Emulator Commands:
  load                      - Load Intel HEX (auto-detects ROM/CMOS)
  config                    - Show memory configuration
  ...
```

✅ **Success!** You're connected.

### Step 4: Check Configuration

```
> config
Memory initialized: ROM=$4000-$7FFF RAM=$0000-$13FF
  ROM aliasing: A15 not decoded, $4000-$7FFF aliases at $C000-$FFFF
  Vectors at $FFF8-$FFFF access physical $7FF8-$7FFF
  RAM mirroring: $0000-$00FF mirrored at $1000-$10FF
  CMOS RAM: $0100-$01FF (persistent in flash)
  Unmapped addresses route to physical bus
```

### Step 5: Load a Test Program

**Simple test program** (blinks accumulator):

```
> load
Ready to receive Intel HEX data. Paste file now...
:1040000086424E7E400020
:02FFE00040008A
:00000001FF
Detected ROM data (address $4000)
Loaded 18 bytes from HEX data
Finalizing ROM load...
Flash programming complete
Flash verification OK
OK: EPROM loaded successfully
```

**What this does**:

```assembly
4000: LDAA #$42    ; Load $42 into A
4002: JMP  $4000   ; Loop forever
```

### Step 6: Run the Program

```
> reset
OK: CMOS saved, CPU reset

> run
OK: CPU started
```

The emulator is now executing MC6800 code!

### Step 7: Inspect Execution

```
> halt
OK: CPU halted, CMOS saved

> status
CPU Status:
  PC: $5002
  A:  $42      ← Success! Accumulator = $42
  B:  $00
  X:  $0000
  SP: $0000
  CCR: $C0 [--NZ--]
  Running: NO
  Halted: YES
  Instructions: 5678
```

🎉 **Congratulations!** You've successfully run MC6800 code.

## What's Next?

- [Load Your Own Program](#loading-your-own-program)
- [Connect Hardware](#connecting-hardware)
- [Debug Your Code](#debugging)
- [Understand the Architecture](#learning-more)

---

## Loading Your Own Program

### Creating Intel HEX Files

#### From Assembly Source

**Using AS6800** (free assembler):

```bash
# Install as6800
sudo apt-get install as6800  # Linux
brew install as6800          # macOS

# Assemble your code
as6800 -l mycode.asm -o mycode.hex

# mycode.asm example:
#         ORG   $4000
# START:  LDAA  #$42
#         STAA  $10
# LOOP:   BRA   LOOP
#         ORG   $7FFE
#         FDB   START   ; Reset vector
```

**Using VASM** (versatile assembler):

```bash
# Get vasm from http://sun.hasenbraten.de/vasm/

# Assemble
vasm6800_std -Fihex mycode.asm -o mycode.hex
```

#### From Binary

**Using srec_cat** (SRecord tools):

```bash
# Install
sudo apt-get install srecord

# Convert binary to hex
srec_cat mycode.bin -binary -offset 0x4000 -o mycode.hex -intel
```

**Using Python**:

```python
from intelhex import IntelHex

ih = IntelHex()
with open('mycode.bin', 'rb') as f:
    data = f.read()

# Load at $4000
ih.frombytes(data, offset=0x4000)

# Add reset vector at $7FFE pointing to $4000
ih[0x7FFE] = 0x40
ih[0x7FFF] = 0x00

ih.write_hex_file('mycode.hex')
```

### Loading the Program

1. **Connect** to emulator via USB
2. **Issue load command**: `load`
3. **Paste HEX file** contents (Ctrl+V)
4. **Wait** for automatic detection and flash programming
5. **Verify**: `OK: EPROM loaded successfully`

```
> load
Ready to receive Intel HEX data. Paste file now...
[Paste entire HEX file here]
Detected ROM data (address $5000)
Loaded 1234 bytes from HEX data
Finalizing ROM load...
Writing 12288 bytes to flash at offset 0x00100000...
Flash programming complete
Flash verification OK
OK: EPROM loaded successfully
```

### Testing Your Program

```bash
# Reset CPU to load vectors
> reset
OK: CMOS saved, CPU reset

# Start execution
> run
OK: CPU started

# Let it run for a while...
# [Press Enter to get prompt back]

# Stop and inspect
> halt
OK: CPU halted, CMOS saved

> status
CPU Status:
  PC: $5123
  A:  $42
  B:  $55
  X:  $0100
  SP: $01FE
  CCR: $C0 [--NZ--]
  Instructions: 45678

# Read memory to check results
> read 0010 10
0010: 42 55 AA FF 00 00 00 00 00 00 00 00 00 00 00 00
```

---

## Connecting Hardware

### Safety First

⚠️ **Never connect 5V directly to RP2350 GPIO pins!**

Use level shifters:

- 74LVC245 (recommended)
- TXS0108E (slower but auto-direction)
- Resistor dividers (inputs only)

### Minimal Setup (PIA Only)

**What You Need**:

- 6821 PIA chip
- 74LVC245 level shifters (2×)
- Breadboard and wires
- 5V power supply (separate from Pico)

**Connections**:

```
Data Bus:
  Pico GPIO 0-7  ←→  74LVC245  ←→  PIA D0-D7

Address Bus:
  Pico GPIO 8-9  →   74LVC245  →   PIA RS0-RS1

Control:
  Pico GPIO 21   →   PIA /CS (via decode)
  Pico GPIO 22   →   PIA E
  Pico GPIO 23   →   PIA R/W

Power:
  Pico 3.3V → 74LVC245 VccB
  5V supply → 74LVC245 VccA, PIA VCC
  Common GND
```

**Test Code**:

```assembly
        ORG   $5000
START:  LDAA  #$FF      ; All outputs
        STAA  $2101     ; PIA A control
        LDAA  #$04      ; Set DDR mode
        STAA  $2101

LOOP:   LDAA  #$AA      ; Pattern
        STAA  $2100     ; Write to Port A
        JSR   DELAY
        LDAA  #$55
        STAA  $2100
        JSR   DELAY
        BRA   LOOP

DELAY:  LDX   #$1000
DLY1:   DEX
        BNE   DLY1
        RTS

        ORG   $7FFE
        FDB   START
```

**Load and Run**:

```bash
> load
[paste HEX]

> reset
> run
```

**Expected**: Port A alternates between $AA and $55 (connect LEDs to see)

### Full System Connection

See [Hardware Connection Guide](Hardware-Connection.md) for complete details on:

- MC6800 CPU replacement
- Williams System 7 pinball
- Full 64KB address space (NED_SYS7 board)

---

## Debugging

### Basic Debugging Workflow

1. **Load program** with `load`
2. **Reset** with `reset`
3. **Run** with `run`
4. **Halt** with `halt`
5. **Check status** with `status`
6. **Inspect memory** with `read`
7. **Fix and reload**

### Common Issues

#### Program Doesn't Start

**Check reset vector**:

```
> read 7FFE 2
7FFE: 50 00
```

This should point to your start address ($5000).

**Check PC after reset**:

```
> reset
> status
CPU Status:
  PC: $5000  ← Should match your start address
```

**If wrong**: Your HEX file doesn't include vectors.

#### Program Crashes Immediately

**Check first instruction**:

```
> read 5000 10
5000: 86 42 97 10 7E 50 00 FF FF FF FF FF FF FF FF FF
      ^LDAA #42
           ^STAA $10
                ^JMP $5000
```

**Check stack**:

```
> status
  SP: $01FE  ← Should be initialized
```

**If SP=$0000**: Program may be doing JSR/RTS without setting SP.

**Fix**: Add stack initialization:

```assembly
START:  LDS   #$01FE    ; Set stack pointer
        ; rest of program
```

#### Infinite Loop

**Symptoms**:

- `halt` → `status` shows same PC repeatedly
- Instruction count increases but nothing happens

**Check code**:

```
> halt
> status
  PC: $5123

> read 5123 4
5123: 7E 51 23 FF
      ^JMP $5123    ← Infinite loop!
```

**Fix**: Check your loop conditions and branches.

#### Wrong Memory Values

**Test reads and writes**:

```
> write 0010 42
> read 0010 1
0010: 42  ← Should match
```

**If different**: Memory configuration problem.

**Check config**:

```
> config
  RAM: $0000-$13FF  ← Verify range includes $0010
```

### Advanced Debugging

#### Enable Interrupt Debug

Edit `CMakeLists.txt`:

```cmake
target_compile_definitions(mc6800_emulator PRIVATE
    DEBUG_INTERRUPTS=1  # Enable
)
```

Rebuild and reflash. Now interrupts print debug info:

```
*** RESET ***
Reset vector: $5000
*** IRQ at PC=$5234 ***
IRQ vector: $5180
```

#### Logic Analyzer

Connect to debug SPI (GPIO 18-19):

- Captures every instruction
- Shows registers, memory access
- Timing analysis

#### UART Debug Output

Connect to UART (GPIO 16-17, 115200 baud):

- System messages
- Error messages
- Boot information

---

## Example Programs

### Example 1: Hello World (Memory)

Writes "HELLO" to memory locations:

```assembly
        ORG   $5000

START:  LDX   #DATA     ; Point to data
        LDY   #$0100    ; Destination (not available on 6800!)
        ; Use indexed instead:

        LDAA  0,X       ; Load 'H'
        STAA  $0100
        LDAA  1,X
        STAA  $0101
        LDAA  2,X
        STAA  $0102
        LDAA  3,X
        STAA  $0103
        LDAA  4,X
        STAA  $0104

LOOP:   BRA   LOOP      ; Halt

DATA:   FCB   'H','E','L','L','O'

        ORG   $7FFE
        FDB   START
```

**Test**:

```
> load
[paste hex]
> reset
> run
> halt
> read 0100 5
0100: 48 45 4C 4C 4F   ; "HELLO" in ASCII
```

### Example 2: Counter

Counts up in memory:

```assembly
        ORG   $5000

START:  LDS   #$01FE    ; Initialize stack
        LDAA  #$00      ; Start at 0

LOOP:   STAA  $0100     ; Store count
        INCA            ; Increment
        BNE   LOOP      ; Loop until overflow

        BRA   START     ; Restart

        ORG   $7FFE
        FDB   START
```

**Test**:

```
> load
[paste hex]
> reset
> run

[Wait 1 second]

> halt
> read 0100 1
0100: 87   ← Some value

> run
[Wait 1 second]
> halt
> read 0100 1
0100: FF   ← Higher value (wraps to 0)
```

### Example 3: Interrupt Handler

Responds to IRQ:

```assembly
        ORG   $5000

START:  LDS   #$01FE    ; Initialize stack
        CLI             ; Enable interrupts

MAIN:   LDAA  $0100     ; Read CMOS
        INCA
        STAA  $0100     ; Increment
        BRA   MAIN

IRQ_HANDLER:
        LDAA  #$FF
        STAA  $0101     ; Mark interrupt occurred
        RTI

        ORG   $7FF8
        FDB   IRQ_HANDLER
        ORG   $7FFE
        FDB   START
```

**Test** (requires hardware IRQ):

```
> load
[paste hex]
> reset
> run

[Trigger IRQ on GPIO 27]

> halt
> read 0101 1
0101: FF   ← Interrupt handler ran!
```

### Example 4: CMOS Settings

Persistent configuration:

```assembly
        ORG   $5000

START:  LDS   #$01FE

        ; Read CMOS config
        LDAA  $0100     ; Version byte
        CMPA  #$42      ; Check if initialized
        BEQ   RUN       ; Already set up

INIT:   ; First run - initialize CMOS
        LDAA  #$42
        STAA  $0100     ; Version
        LDAA  #$05
        STAA  $0101     ; Setting 1
        LDAA  #$AA
        STAA  $0102     ; Setting 2

RUN:    ; Normal operation
        LDAA  $0101     ; Load setting
        ; Use setting...

LOOP:   BRA   LOOP

        ORG   $7FFE
        FDB   START
```

**Test**:

```
> load
[paste hex]
> reset
> run
> halt
> cmos dump
0100: 42 05 AA ...   ← Initialized

[Power cycle]

> status
  PC: $5000
> run
> halt
> cmos dump
0100: 42 05 AA ...   ← Persisted!
```

---

## Building from Source

### Prerequisites

```bash
# macOS
brew install cmake gcc-arm-embedded

# Ubuntu/Debian
sudo apt-get install cmake gcc-arm-none-eabi

# Set SDK path
export PICO_SDK_PATH=/path/to/pico-sdk
```

### Build Steps

```bash
# Clone repository
git clone https://github.com/bikeNomad/6800_emulator.git
cd 6800_emulator
make 

# Output: images/BOARD_NED_SYS7.uf2
```

---

## Learning More

### MC6800 Resources

**Datasheets**:

- [MC6800 CPU Datasheet](https://www.nxp.com/docs/en/data-sheet/MC6800.pdf)
- [6821 PIA Datasheet](https://www.jameco.com/Jameco/Products/ProdDS/43596.pdf)

**Books**:

- "MC6800 Microprocessor Programming Manual" (Motorola)
- "6800 Assembly Language Programming" (Lance Leventhal)

**Online**:

- [6800.org](http://www.6800.org)

### Emulator Documentation

- [Architecture](Architecture.md) - System design and internals
- [Memory Map](Memory-Map.md) - Complete memory layout
- [Memory Fingerprinting](Memory-Fingerprinting.md) - Auto-configuration system
- [Hardware Connection](Hardware-Connection.md) - Physical connections
- [USB Commands](USB-Commands.md) - Full command reference

### Example Projects

**Williams System 7**:

```bash
# Get ROMs from IPDB.org or similar (Black Knight 1980 as an example)
# Concatenate them into a single file
cat IC26.716 IC14.716 IC20.716 IC17.532 > black_knight.bin
# Convert to Intel Hex format, setting the start address
arm-none-eabi-objcopy -I binary -O ihex --change-address 0xd800 black_knight.bin black_knight.hex
# Load and run
> load
[paste black_knight.hex]
> reset
> run
```

**Custom Hardware**:

- See [Hardware Connection Guide](Hardware-Connection.md)
- Examples for PIAs, displays, switches

---

## Troubleshooting

### USB Not Recognized

1. Check cable (must be data cable)
2. Try different USB port
3. Re-enter bootloader: Hold BOOTSEL + connect
4. Check Device Manager (Windows) or `lsusb` (Linux)

### Load Command Hangs

1. Ensure HEX file has EOF record (`:00000001FF`)
2. Check HEX file format (Intel HEX)
3. Try smaller file first
4. Terminal buffer issues: paste slowly

### Program Not Running

1. Check reset vector: `read FFFE 2`
2. Verify ROM loaded: `read 5800 10`
3. Check PC after reset: `reset` then `status`

### Flash Write Failed

1. Power cycle Pico
2. Re-enter bootloader
3. Reflash firmware
4. Check for hardware issues

### Strange Behavior

1. Reset: `reset`
2. Check config: `config`
3. Clear CMOS: `write 0100 FF FF FF ... FF`
4. Power cycle

---

## Next Steps

### Beginner Path

1. ✅ Flash firmware
2. ✅ Connect and verify
3. ✅ Load test program
4. ✅ Read this guide
5. → Write simple programs
6. → Learn MC6800 assembly
7. → Connect first peripheral (PIA)

### Intermediate Path

1. ✅ Working standalone emulator
2. → Create larger programs
3. → Use CMOS for configuration
4. → Connect multiple PIAs
5. → Interface with displays/switches
6. → Debug with logic analyzer

### Advanced Path

1. ✅ Understanding architecture
2. → Replace CPU in vintage system
3. → Cycle-accurate timing verification
4. → Custom peripheral development
5. → Contribute to emulator code

---

## Support and Community

### Getting Help

1. **Check documentation** (start here!)
2. **Search issues** on GitHub
3. **Ask in discussions**
4. **File bug report** (with details!)

### Reporting Issues

Include:

- Board type (NED_SYS7, etc.)
- Firmware version
- HEX file (if relevant)
- Terminal output
- Expected vs actual behavior

### Contributing

See `CONTRIBUTING.md` (if available) or:

1. Fork repository
2. Create feature branch
3. Make changes
4. Test thoroughly
5. Submit pull request

---

## Quick Reference

### Essential Commands

```bash
help                    # Show all commands
config                  # Memory configuration
load                    # Load HEX file
reset                   # Reset CPU
run                     # Start execution
halt                    # Stop execution
status                  # CPU state
read <addr> <len>       # Read memory
write <addr> <data>     # Write memory
cmos dump               # View CMOS
```

### Typical Session

```bash
# 1. Connect
screen /dev/tty.usbmodem14201

# 2. Load program
load
[paste HEX]

# 3. Run
reset
run

# 4. Check
halt
status
read 0100 10

# 5. Iterate
[fix code, rebuild HEX]
load
[paste new HEX]
reset
run
```

### Pin Quick Reference (NED_SYS7)

```
GPIO 0-7:   Data bus (D0-D7)
GPIO 8-23:  Address bus (A0-A15)
GPIO 24:    E clock (output, 894.886 kHz)
GPIO 25:    VMA
GPIO 26:    R/W
GPIO 27:    /IRQ (input)
GPIO 28:    /NMI (input)
GPIO 29:    /RESET (input)
```

---

## Congratulations

You now have a working MC6800 emulator. Start writing code and have fun!

For more details, see:

- [Architecture](Architecture.md)
- [Memory Map](Memory-Map.md)
- [Hardware Connection](Hardware-Connection.md)
- [USB Commands](USB-Commands.md)

Happy emulating! 🎮
