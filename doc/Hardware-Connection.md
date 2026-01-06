# Hardware Connection Guide

## Overview

This guide covers the physical connections required to interface the MC6800 Emulator with target systems. The emulator can operate in three modes:

1. **Standalone Mode**: No external connections, ROM/RAM in flash
2. **Peripheral Mode**: Connect to PIAs, ACIAs, and other peripherals
3. **System Replacement Mode**: Drop-in replacement for MC6800 CPU

## Board Configuration

### NED_SYS7 Board (BOARD_NED_SYS7)

**Specifications**:

- Based on RP2350 with 48 GPIO pins
- 16 address lines (full bus)
- Complete 64KB address space
- Designed for Williams System 7 pinball machines

**Features**:

- A0-A15 connect to GPIO 8-23 (standard contiguous mapping)
- Full system replacement capability
- Complete memory addressing
- Suitable for CPU drop-in replacement

## Pin Assignments

### Data Bus (All Boards)

| Signal | Direction | GPIO | Notes |
|--------|-----------|------|-------|
| D0 | Bidirectional | 0 | Data bit 0 (LSB) |
| D1 | Bidirectional | 1 | Data bit 1 |
| D2 | Bidirectional | 2 | Data bit 2 |
| D3 | Bidirectional | 3 | Data bit 3 |
| D4 | Bidirectional | 4 | Data bit 4 |
| D5 | Bidirectional | 5 | Data bit 5 |
| D6 | Bidirectional | 6 | Data bit 6 |
| D7 | Bidirectional | 7 | Data bit 7 (MSB) |

**Connection Notes**:

- Use 330Ω series resistors for protection
- Tie to ground via 10KΩ pull-down if unused
- 5V-tolerant with appropriate level shifters

### Address Bus

| Signal | Direction | GPIO | Notes |
|--------|-----------|------|-------|
| A0-A15 | Output | 8-23 | Full address bus |

**Contiguous mapping**: Complete 64KB address space

- A0 → GPIO 8, A1 → GPIO 9, ..., A15 → GPIO 23
- Standard sequential mapping

### Control Signals

| Signal | Direction | GPIO | Notes |
|--------|-----------|------|-------|
| VMA | Output | 25 | Valid Memory Address |
| E | Output | 24 | E Clock (894.886 kHz) |
| R/W | Output | 26 | Read/Write (1=Read, 0=Write) |
| /IRQ | Input | 27 | Interrupt Request (active low) |
| /NMI | Input | 28 | Non-Maskable Interrupt (active low) |
| /RESET | Input | 29 | Reset (active low) |

### Debug Interfaces

| Signal | Direction | GPIO | Notes |
|--------|-----------|------|-------|
| SPI_CS | Output | 33 | Debug SPI chip select |
| SPI_SCK | Output | 34 | Debug SPI clock |
| SPI_MOSI | Output | 35 | Debug SPI data out |
| SPI_MISO | Input | 36 | Debug SPI data in |
| UART_TX | Output | 40 | Debug UART output |
| UART_RX | Input | 41 | Debug UART input (unused) |
| LED_ROM | Output | 37 | ROM access indicator (active low, green) |
| LED_RAM | Output | 38 | RAM access indicator (active low, red) |
| LED_UNMAPPED | Output | 39 | Unmapped access indicator (active low, yellow) |
| PSRAM_CS | Output | 47 | PSRAM chip select |

**Note**: UART uses GPIO 40-41 on UART1 (address bus uses 8-23, UART0 pins occupied)

**LED Indicators**:

- Active low outputs (LED lights when GPIO is low)
- GREEN: Illuminates during ROM access cycles
- RED: Illuminates during RAM/CMOS access cycles
- YELLOW: Illuminates during unmapped/external bus access cycles

**PSRAM Interface**:

- Shared with QSPI bus on RP2350
- Used for extended memory in future implementations

### USB Interface

- **USB CDC**: Built-in USB connection for programming and control
- **Speed**: Full-speed USB 2.0 (12 Mbps)
- **Usage**: Command interface, HEX loading, configuration

## Level Shifting

### Voltage Considerations

The RP2350 is a 3.3V device, while most MC6800 systems use 5V logic.

The NED_SYS7 board uses 74LVC245 octal level shifters to shift 5V to 3.3V for the data and address bus,
and TXU0104 quad level shifters for the control signals.

**Power Supply**:

- VccA (A side): 5V (MC6800 system)
- VccB (B side): 3.3V (RP2350)
- GND: Common ground

## Connection Scenarios

### Scenario 1: Standalone Operation (No External Hardware)

**Use Case**: Development, testing, ROM debugging

**Connections Required**:

- USB cable only
- Optional: UART for debug output

**Configuration**:

```c
// All memory in emulator
ROM:  0x5000-0x7FFF (flash)
RAM:  0x0000-0x13FF (shadow)
CMOS: 0x0100-0x01FF (flash)
```

**Steps**:

1. Connect USB cable
2. Load ROM via `load` command
3. Issue `reset` command
4. Issue `run` command

### Scenario 2: PIA Connection (Peripheral Mode)

**Use Case**: Control PIAs, read switches, drive displays

**Required Connections**:

- Data bus (D0-D7)
- Address bus (A0-A15)
- VMA, R/W
- E clock

**Example: 6821 PIA at $2100**

```
PIA 6821                    NED_SYS7
Pin 26-33 (D0-D7)  ←→  Level Shifter  ←→  GPIO 0-7
Pin 9-17 (PA0-PA7)  →  Your peripherals
Pin 18-25 (PB0-PB7) →  Your peripherals

Pin 36 (RS0)       ←   GPIO 8 (A0)
Pin 35 (RS1)       ←   GPIO 9 (A1)
Pin 24 (CS2)       ←   GPIO 21 (A13, via decode)
Pin 23 (/CS1)      ←   Address decode logic
Pin 22 (CS0)       ←   VCC

Pin 21 (R/W)       ←   GPIO 26 (R/W)
Pin 25 (E)         ←   GPIO 24 (E)
Pin 34 (/RESET)    ←   GPIO 29 (/RESET)
```

**Address Decode Logic**:

```
CS2  = A13
/CS1 = !(A12 & A11 & A10 & A9)  [Inverted for $2100]
CS0  = VCC (always enabled)
```

**Software Configuration**:

```
# Via USB command
config rom 5000 3000
config ram 0000 1400

# PIA will respond at $2100-$2103 on physical bus
```

### Scenario 3: System Replacement (Full CPU Mode)

**Use Case**: Replace failed MC6800 CPU in existing system

**Supported Boards**: BOARD_NED_SYS7 only (requires full 16-bit address bus)

**Required Connections**:

- All signals (data, address, control)
- Direct connection to CPU socket or edge connector

**Steps**:

1. **Remove original MC6800**
2. **Identify socket pinout** (40-pin DIP)
3. **Connect all signals**:
   - D0-D7 (pins 26-33)
   - A0-A15 (pins 9-20, 22-25)
   - VMA (pin 5)
   - R/W (pin 34)
   - E (pin 37)
   - /IRQ (pin 4)
   - /NMI (pin 6)
   - /RESET (pin 40)

4. **Power connections**:
   - Pin 1: VSS (GND)
   - Pin 8: VCC (5V) - **DO NOT CONNECT TO RP2350**
   - RP2350: Separate 3.3V USB power

5. **Level shifters required** for all signals

**Pinout Reference - MC6808 40-Pin DIP**:

```
         ┌──────────────┐
    VSS ─│1          40│─ /RESET
  /HALT ─│2          39│─ EXTAL
     MR ─│3          38│─ XTAL
   /IRQ ─│4          37│─ E
    VMA ─│5          36│─ RE
   /NMI ─│6          35│─ VCCST
     BA ─│7          34│─ R/W
    VCC ─│8          33│─ D0
     A0 ─│9          32│─ D1
     A1 ─│10         31│─ D2
     A2 ─│11         30│─ D3
     A3 ─│12         29│─ D4
     A4 ─│13         28│─ D5
     A5 ─│14         27│─ D6
     A6 ─│15         26│─ D7
     A7 ─│16         25│─ A15
     A8 ─│17         24│─ A14
     A9 ─│18         23│─ A13
    A10 ─│19         22│─ A12
    A11 ─│20         21│─ VSS
         └──────────────┘
```

**Key MC6808 Features**:

- **RE (Ready Enable)**: Enables automatic wait state generation for slow memories (pin 36)
- **MR (Memory Ready)**: Input for synchronizing with external memory (pin 3)
- **BA (Bus Available)**: Indicates DMA bus available (pin 7, outputs low during DMA)
- **VCCST (Standby Power)**: Standby power supply pin (pin 35, not used in emulation)
- **XTAL/EXTAL**: Crystal oscillator connections (pins 38-39, leave unconnected)

**Emulator Notes**:

- /HALT (pin 2): Controlled via USB command (normally active low input)
- MR (pin 3): Tied low (no external wait state control needed)
- RE (pin 36): Tied high (enable all wait states)
- /IRQ (pin 4), /NMI (pin 6): Software controlled interrupts
- Crystal pins XTAL/EXTAL: Leave unconnected in emulation

### Scenario 4: Williams System 7 Pinball

**Use Case**: Replacement CPU for Williams pinball machines

**Recommended Board**: BOARD_NED_SYS7

**System Details**:

- CPU: MC6808 @ 894.886 kHz
- PIAs: Three 6821s at $2100-$21FF
- Memory: 5KB RAM, 12KB EPROM

**Connections (BOARD_NED_SYS7)**:

```
IC1 Connector (CPU Board)
Pin  Signal      RP2350 GPIO    Notes
1    GND         GND             Common ground
2    -           -               (not used)
3    -           -               (not used)
4    /IRQ        27              Interrupt request (active low)
5    VMA         24              Valid memory address
6    /NMI        28              Non-maskable interrupt (active low)
7    -           -               (not used)
8    -           -               (not used)
9    A0          8               Address bus (full 16-bit)
10   A1          9
11   A2          10
12   A3          11
13   A4          12
14   A5          13
15   A6          14
16   A7          15
17   A8          16
18   A9          17
19   A10         18
20   A11         19
21   GND         GND             Ground (duplicate)
22   A12         20
23   A13         21
24   A14         22
25   A15         23
26   D7          7
27   D6          6
28   D5          5
29   D4          4
30   D3          3
31   D2          2
32   D1          1
33   D0          0
34   R/W         26              Read/Write
35   -           -               (not used)
36   -           -               (not used)
37   E           25              E clock (894.886 kHz)
38   -           -               (not used)
39   -           -               (not used)
40   /RESET      29              Reset (active low)
```

**Configuration**:

```bash
# Build for NED_SYS7 board (if building from source)
# cmake -DBOARD_TYPE=BOARD_NED_SYS7 ..
# make

# Load System 7 ROM
load
[paste system7.hex]

# Check configuration
config

# Run
reset
run
```

**Expected Output**:

```
Memory initialized: ROM=$5000-$7FFF RAM=$0000-$13FF
  ROM aliasing: A15 not decoded, $5000-$7FFF aliases at $D000-$FFFF
  Vectors at $FFF8-$FFFF access physical $7FF8-$7FFF
  RAM mirroring: $0000-$00FF mirrored at $1000-$10FF
  CMOS RAM: $0100-$01FF (persistent in flash)
  Unmapped addresses route to physical bus

Reset vector: $5000
CPU started
```

## Troubleshooting

### No Communication

**Symptom**: USB device not recognized

**Solutions**:

1. Check USB cable (data, not charge-only)
2. Press BOOTSEL + RESET to enter bootloader
3. Drag .uf2 file to RPI-RP2 drive
4. Verify COM port in Device Manager (Windows) or `ls /dev/tty*` (macOS/Linux)

### Data Bus Errors

**Symptom**: Incorrect data reads, random behavior

**Solutions**:

1. Check level shifter connections
2. Verify 3.3V and 5V power rails
3. Test with multimeter (logic high/low levels)
4. Add 330Ω series resistors for protection
5. Check ground connections

### Timing Issues

**Symptom**: Peripherals not responding, intermittent faults

**Solutions**:

1. Verify E clock frequency (should be 894.886 kHz)
2. Check E clock with oscilloscope
3. Ensure level shifters are fast enough (< 10ns propagation)
4. Add 100nF bypass caps near level shifters
5. Keep wires short (< 6 inches)

### Address Decode Problems

**Symptom**: PIA responds at wrong addresses

**Solutions**:

1. Verify address line connections
2. Check CS logic with logic analyzer
3. Test address decode with `read` command
4. Use `config` command to check memory map

### Interrupt Not Working

**Symptom**: /IRQ or /NMI not triggering

**Solutions**:

1. Check pull-up resistors (10KΩ to 3.3V)
2. Verify active-low logic
3. Test with USB command: `write 2104 80` (PIA interrupt)
4. Enable debug output: `DEBUG_INTERRUPTS=1` in CMakeLists.txt

## Safety Precautions

### Power Supply

⚠️ **NEVER connect 5V directly to RP2350 GPIO pins**

**Safe practices**:

- Use level shifters for all 5V signals
- Power RP2350 from USB (3.3V regulated)
- Use separate power supply for MC6800 system
- Common ground only

### ESD Protection

- Use ESD wrist strap when handling boards
- Store in anti-static bags
- Touch grounded metal before handling
- Avoid carpeted areas

### Smoke Test

Before full connection:

1. Power RP2350 via USB only
2. Verify 3.3V on GPIO pins (multimeter)
3. Load test ROM and verify operation
4. Connect level shifters (no load)
5. Test with oscilloscope
6. Connect peripheral incrementally
7. Monitor for overheating

## Test Points

### Recommended Test Signals

1. **E Clock (GPIO 24)**
   - Frequency: 894.886 kHz
   - Duty cycle: 50%
   - Amplitude: 3.3V

2. **Data Bus (GPIO 0-7)**
   - Idle: High-Z (with pull-ups)
   - Active read: Input mode
   - Active write: Output mode, 0V or 3.3V

3. **VMA (GPIO 25)**
   - Asserted during bus cycles
   - De-asserted between cycles

4. **R/W (GPIO 26)**
   - High during reads
   - Low during writes

## Logic Analyzer Setup

### Recommended Analyzers

- Saleae Logic 8/16 (excellent software)
- DSLogic Plus (affordable, 16 channels)
- PulseView + cheap clones (budget option)

### Channel Assignment

```
CH0:  E (clock)
CH1:  VMA
CH2:  R/W
CH3:  D0
CH4:  D1
CH5:  D2
CH6:  D3
CH7:  D4
CH8:  D5
CH9:  D6
CH10: D7
CH11: A0
CH12: /IRQ
CH13: /NMI
CH14: /RESET
```

### Trigger Conditions

- **Instruction fetch**: VMA=1, R/W=1
- **Interrupt**: /IRQ falling edge
- **Reset**: /RESET falling edge
- **Write to PIA**: VMA=1, R/W=0, A13=1

## Connection Checklist

- [ ] USB cable connected
- [ ] Level shifters installed (if using 5V system)
- [ ] All power supplies verified (voltages correct)
- [ ] Ground connections solid
- [ ] Data bus connected (D0-D7)
- [ ] Address bus connected (A0-A1, A10-A14 minimum)
- [ ] Control signals connected (VMA, R/W, E)
- [ ] Interrupt inputs connected (/IRQ, /NMI, /RESET)
- [ ] Pull-up resistors on interrupt inputs
- [ ] Series resistors on outputs (330Ω recommended)
- [ ] Bypass capacitors near ICs (100nF)
- [ ] No 5V connected to RP2350 GPIOs directly
- [ ] E clock verified with oscilloscope
- [ ] ROM loaded via USB
- [ ] Initial test successful (standalone mode)

## Reference Documents

- [MC6800 Datasheet (Motorola)](https://www.nxp.com/docs/en/data-sheet/MC6800.pdf)
- [6821 PIA Datasheet](https://www.jameco.com/Jameco/Products/ProdDS/43596.pdf)
- [RP2350 Datasheet](https://datasheets.raspberrypi.com/rp2350/rp2350-datasheet.pdf)
- [Williams System 7 Schematics](https://www.ipdb.org/)

## See Also

- [Architecture](Architecture.md)
- [PIO Bus Cycles](PIO-Bus-Cycles.md)
- [Memory Map](Memory-Map.md)
- [USB Commands](USB-Commands.md)
- [Getting Started](Getting-Started.md)
