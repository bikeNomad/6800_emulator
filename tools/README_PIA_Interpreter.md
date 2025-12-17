# PIA Interpreter Tool

## Overview

The `interpret_pia.py` tool interprets MC6821 PIA (Peripheral Interface Adapter) reads and writes from SPI log data. It provides detailed annotations showing what's happening with each PIA port, tracking data direction register (DDR) changes, output writes, input reads, and detecting mismatches between written and read-back values.

## Usage

### Basic Usage

```bash
python3 tools/interpret_pia.py <input_file> [output_file]
```

- `input_file`: The output file from `extract_spi_data.py` (e.g., `from_reset_spi.txt`)
- `output_file`: Optional output filename (defaults to `<input>_pia.txt`)

### Example Workflow

1. Extract SPI data from CSV:
   ```bash
   python3 tools/extract_spi_data.py from_reset.csv
   ```
   This creates `from_reset_spi.txt`

2. Interpret PIA accesses:
   ```bash
   python3 tools/interpret_pia.py from_reset_spi.txt
   ```
   This creates `from_reset_pia.txt` with annotations

### Quick Test with Default Files

If you have `from_reset_spi.txt` in the current directory:
```bash
python3 tools/interpret_pia.py
```

## MC6821 PIA Architecture

Each PIA has two I/O ports (A and B), with the following registers per port:

### Register Map (per PIA at base address)
- `+0`: Port A Data/DDR (selected by CRA bit 2)
- `+1`: Control Register A (CRA)
- `+2`: Port B Data/DDR (selected by CRB bit 2)
- `+3`: Control Register B (CRB)

### Data Direction Register (DDR)
- Each bit controls one pin: `0` = Input, `1` = Output
- Must be accessed when Control Register bit 2 = 0

### Control Register (CRA/CRB)
- **Bit 2**: DDR access control (0 = DDR accessible, 1 = Data register accessible)
- Bits 7-0: Interrupt flags and control (see MC6821 datasheet)

### PIA Address Ranges
PIAs are located at multiples of 0x100 in the range 0x2000-0x2FFF:
- 0x2100: PIA 1
- 0x2200: PIA 2
- 0x2400: PIA 3
- 0x2800: PIA 4

## Output Format

### Annotated Lines

Each PIA access is annotated with details:

```
2100 W 7F            # PIA 2100 Port A: Write 7F (01111111) [all outputs]
2101 W 3E            # PIA 2100 Port A: Set CRA = 3E (00111110) [DATA now accessible]
2102 R FF            # PIA 2100 Port B: Read FF (11111111) [outputs=C0, inputs=3F]
```

Non-PIA accesses are left unchanged:
```
E809 D0
E80B D0
```

### Summary Output

At the end of processing, a summary is printed showing the final state of all PIAs:

```
================================================================================
PIA STATE SUMMARY
================================================================================

PIA at 2100:
  Port A:
    Control:      3E (00111110)
    DDR:          7F (01111111) [IOOOOOOO]
    Last written: 7F (01111111)
    Last read:    00 (00000000)
  Port B:
    Control:      3E (00111110)
    DDR:          C0 (11000000) [OOIIIIII]
    Last written: 3F (00111111)
    Last read:    FF (11111111)
```

### I/O Direction Format

The DDR is shown in binary with a direction indicator:
- `[OOOOOOOO]`: All outputs
- `[IIIIIIII]`: All inputs
- `[IOOOOOOO]`: MSB is input, rest are outputs
- `[OOIIIIII]`: Top 2 bits are outputs, rest are inputs

## Key Features

### 1. DDR Tracking
Tracks when the DDR is set and shows the input/output configuration:
```
2100 W 7F            # PIA 2100 Port A: Set DDR = 7F (01111111) [IOOOOOOO]
```

### 2. Output Writes
Shows writes to output pins and indicates which bits are affected:
```
2100 W 7F            # PIA 2100 Port A: Write 7F (01111111) [all outputs]
2102 W 00            # PIA 2100 Port B: Write 00 (00000000) [outputs=00, DDR=C0]
```

### 3. Input Reads
Shows reads from input pins:
```
2100 R 00            # PIA 2100 Port A: Read 00 (00000000) [all inputs]
```

### 4. Mismatch Detection
**Detects when output values don't match what was written:**
```
2202 R FF            # PIA 2200 Port B: Read FF (11111111) [all outputs - MISMATCH! wrote 00, read FF]
2402 R FF            # PIA 2400 Port B: Read FF (11111111) [all outputs - MISMATCH! wrote 04, read FF]
2102 R FF            # PIA 2100 Port B: Read FF (11111111) [outputs=C0, inputs=3F, OUTPUT MISMATCH! wrote 00]
```

This is useful for debugging:
- Hardware issues (pull-ups/pull-downs overriding outputs)
- Unconnected pins floating high
- Bus contention
- External devices driving output pins

### 5. Control Register Monitoring
Tracks control register changes, especially DDR/DATA access mode:
```
2101 W 00            # PIA 2100 Port A: Set CRA = 00 (00000000) [DDR now accessible]
2101 W 3E            # PIA 2100 Port A: Set CRA = 3E (00111110) [DATA now accessible]
```

## Finding Specific Issues

### Find all output mismatches:
```bash
grep -i "MISMATCH" from_reset_pia.txt
```

### Find all DDR changes:
```bash
grep "Set DDR" from_reset_pia.txt
```

### Track a specific PIA:
```bash
grep "PIA 2100" from_reset_pia.txt
```

### Track a specific port:
```bash
grep "PIA 2100 Port A" from_reset_pia.txt
```

## Typical PIA Initialization Sequence

The tool will show typical initialization patterns:

1. **Set control register to access DDR** (CRA/CRB bit 2 = 0):
   ```
   2101 W 00            # PIA 2100 Port A: Set CRA = 00 (00000000) [DDR now accessible]
   ```

2. **Configure data direction**:
   ```
   2100 W 7F            # PIA 2100 Port A: Set DDR = 7F (01111111) [IOOOOOOO]
   ```

3. **Set control register to access data** (CRA/CRB bit 2 = 1):
   ```
   2101 W 3E            # PIA 2100 Port A: Set CRA = 3E (00111110) [DATA now accessible]
   ```

4. **Initialize output values**:
   ```
   2100 W 00            # PIA 2100 Port A: Write 00 (00000000) [outputs=00, DDR=7F]
   ```

## Implementation Details

The interpreter maintains stateful models of each PIA, tracking:
- Current DDR configuration for each port
- Current control register values
- Last values written to outputs
- Last values read from inputs

This allows it to correctly interpret whether a data register access is a read/write to the DDR or the actual port data, based on the current control register state.

## See Also

- `extract_spi_data.py`: Extracts SPI data from CSV files
- `doc/MC6821P.pdf`: MC6821 PIA datasheet
- `doc/SPI-Bus-Debug.md`: SPI debugging documentation
