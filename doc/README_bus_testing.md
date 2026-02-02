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

# MC6800 Bus Testing Module

This module provides Python functions to read and write data via the hardware bus interface of the MC6800 emulator, allowing you to test ROMs, PIAs, and other hardware components connected to the bus.

## Features

- **Bus Read/Write**: Direct access to the hardware bus with cycle-accurate timing
- **Block Operations**: Efficient bulk data transfer (up to 1024 bytes)
- **ROM Testing**: Built-in ROM verification against expected data
- **Memory Dumping**: Hex dump functionality for bus inspection
- **Auto-detection**: Automatic discovery of the emulator's USB serial port
- **Error Handling**: Comprehensive error checking and validation

## Installation

No installation required. The module runs directly from the project root directory.

## Usage

### Basic Usage

```python
import bus_test

# Initialize connection (auto-detects port)
bus_test.init()

# Read a byte from address 0x0000
data = bus_test.read(0x0000)
print(f"Read: 0x{data:02X}")

# Write a byte to address 0x0000
bus_test.write(0x0000, 0xAA)

# Clean up
bus_test.cleanup()
```

### Context Manager (Recommended)

```python
import bus_test

with bus_test.BusTester() as bus:
    # Read operations
    data = bus.read_byte(0x0000)

    # Write operations
    bus.write_byte(0x0000, 0x55)

    # Block operations
    block_data = bus.read_block(0x0000, 16)
    bus.write_block(0x0010, [0x00, 0x11, 0x22, 0x33])

    # Get bus information
    info = bus.get_bus_info()
    print(f"Max address: 0x{info['Max Address']:04X}")
```

### ROM Testing

```python
import bus_test

# Expected ROM data (replace with your actual ROM content)
expected_rom = [
    0x86, 0x01, 0xB7, 0x10, 0x00,  # Example data
    # ... more bytes
]

with bus_test.BusTester() as bus:
    # Test ROM at address 0xE000
    if bus.test_rom(0xE000, expected_rom):
        print("ROM test PASSED")
    else:
        print("ROM test FAILED")
```

### Memory Dumping

```python
import bus_test

with bus_test.BusTester() as bus:
    # Dump 256 bytes starting at address 0x0000
    dump = bus.dump_memory(0x0000, 256, width=16)
    print(dump)
```

## API Reference

### BusTester Class

#### Constructor
```python
BusTester(port=None, baudrate=115200, timeout=1.0)
```

- `port`: Serial port name (auto-detected if None)
- `baudrate`: Serial baud rate (default: 115200)
- `timeout`: Command timeout in seconds (default: 1.0)

#### Methods

##### `read_byte(address)` -> int
Read a single byte from the specified address.

##### `write_byte(address, data)`
Write a single byte to the specified address.

##### `read_block(address, length)` -> List[int]
Read a block of bytes from the specified address range.

##### `write_block(address, data)`
Write a block of bytes to the specified address range.
- `data` can be a list of integers or bytes object

##### `get_bus_info()` -> dict
Get information about the bus configuration.

##### `test_rom(address, expected_data)` -> bool
Test a ROM by comparing actual vs expected data.

##### `dump_memory(address, length, width=16)` -> str
Create a formatted hex dump of memory contents.

### Convenience Functions

##### `init(port=None)`
Initialize the default bus tester instance.

##### `cleanup()`
Clean up the default bus tester instance.

##### `read(address)` -> int
Read using the default tester.

##### `write(address, data)`
Write using the default tester.

##### `read_block(address, length)` -> List[int]
Read block using the default tester.

##### `write_block(address, data)`
Write block using the default tester.

## Hardware Interface

The module communicates with the MC6800 emulator via USB CDC serial commands:

- `BUS_READ <address>` - Read byte from hardware bus
- `BUS_WRITE <address> <data>` - Write byte to hardware bus
- `BUS_READ_BLOCK <address> <length>` - Read block from hardware bus
- `BUS_WRITE_BLOCK <address> <data...>` - Write block to hardware bus
- `BUS_INFO` - Get bus configuration information

All operations are cycle-accurate and synchronized to the E-clock signal.

## Board Support

### Supported Boards

- **Pico 2 W**: 7 address lines (A0-A1, A10-A14), 128-byte address space
- **Ned's System 7**: 16 address lines (A0-A15), 64KB address space

The module automatically detects the board type and enforces appropriate address limits.

## Error Handling

The module raises `BusError` exceptions for communication errors:

```python
import bus_test

try:
    with bus_test.BusTester() as bus:
        data = bus.read_byte(0xFFFF)  # May be out of range
except bus_test.BusError as e:
    print(f"Bus error: {e}")
```

## Examples

### Testing a 6821 PIA

```python
import bus_test

# PIA typically at addresses 0x0000-0x0003
PIA_BASE = 0x0000

with bus_test.BusTester() as bus:
    # Test PIA port A (write 0x55, read back)
    bus.write_byte(PIA_BASE + 0, 0x55)  # Port A data
    bus.write_byte(PIA_BASE + 2, 0x00)  # Port A DDR (output)
    bus.write_byte(PIA_BASE + 3, 0x00)  # Port A control

    read_back = bus.read_byte(PIA_BASE + 0)
    print(f"PIA test: wrote 0x55, read 0x{read_back:02X}")
```

### ROM Verification

```python
import bus_test

# Load expected ROM data from file
def load_expected_rom(filename):
    with open(filename, 'rb') as f:
        return list(f.read())

expected = load_expected_rom('expected_rom.bin')

with bus_test.BusTester() as bus:
    if bus.test_rom(0xE000, expected):
        print("✓ ROM verification successful")
    else:
        print("✗ ROM verification failed")
        # Get actual data for comparison
        actual = bus.read_block(0xE000, len(expected))
        for i, (exp, act) in enumerate(zip(expected, actual)):
            if exp != act:
                print(f"Mismatch at 0x{i:04X}: expected 0x{exp:02X}, got 0x{act:02X}")
```

### Automated Testing

```python
import bus_test
import time

def run_bus_tests():
    """Run comprehensive bus tests"""
    tests_passed = 0
    total_tests = 0

    with bus_test.BusTester() as bus:
        # Test 1: Basic read/write
        total_tests += 1
        bus.write_byte(0x0000, 0xAA)
        if bus.read_byte(0x0000) == 0xAA:
            tests_passed += 1
            print("✓ Basic R/W test passed")
        else:
            print("✗ Basic R/W test failed")

        # Test 2: Block operations
        total_tests += 1
        test_data = list(range(256))
        bus.write_block(0x0100, test_data)
        read_data = bus.read_block(0x0100, len(test_data))
        if read_data == test_data:
            tests_passed += 1
            print("✓ Block operation test passed")
        else:
            print("✗ Block operation test failed")

        # Test 3: Address boundary check
        total_tests += 1
        try:
            bus.read_byte(bus.get_bus_info()['Max Address'] + 1)
            print("✗ Address boundary test failed (should have raised error)")
        except bus_test.BusError:
            tests_passed += 1
            print("✓ Address boundary test passed")

    print(f"\nResults: {tests_passed}/{total_tests} tests passed")
    return tests_passed == total_tests

if __name__ == "__main__":
    success = run_bus_tests()
    exit(0 if success else 1)
```

## Running Tests

The module includes a test suite that can be run to verify functionality:

```bash
cd /path/to/6800_emulator
python3 tests/test_rom.py
```

This will run various tests including basic bus operations, PIA testing, memory dumping, and block operations.

## Troubleshooting

### Connection Issues

1. **Auto-detection fails**: Specify port manually
   ```python
   bus = bus_test.BusTester(port='/dev/ttyACM0')
   ```

2. **Permission denied**: Ensure user has access to serial ports
   ```bash
   sudo usermod -a -G dialout $USER
   ```

3. **Device not found**: Check USB connection and that emulator is running

### Timeout Errors

- Increase timeout for slow operations
- Check that emulator is responding to commands
- Verify USB connection stability

### Address Errors

- Check board type and address limits with `bus.get_bus_info()`
- Ensure addresses are within valid range for your board configuration

## Technical Details

- **Timing**: All bus operations are synchronized to the E-clock for cycle accuracy
- **Threading**: Safe for single-threaded use; not thread-safe
- **Memory**: Minimal memory footprint, suitable for resource-constrained systems
- **Compatibility**: Works with both Pico 2 and Ned's System 7 board configurations

## Contributing

To extend the bus testing functionality:

1. Add new methods to the `BusTester` class
2. Implement corresponding USB commands in the emulator's USB CDC handler
3. Update this documentation
4. Add tests to the test suite

## License

This module is part of the MC6800 emulator project. See project LICENSE for details.
