# MicroPython Modules for MC6800 Emulator

This directory contains MicroPython modules that run directly on the RP2350 board for testing and interacting with the MC6800 hardware bus interface.

## Files

### `bus_test.py` - MC6800 Bus Test Module

Provides Python functions to read and write via the hardware bus for testing ROMs, PIAs, and other peripherals connected to the MC6800 bus. This module runs directly on the RP2350 board under MicroPython, providing the same functionality as the host-side `tests/bus_test.py` but with direct hardware access.

#### Features

- **Direct Hardware Access**: Controls GPIO pins directly for cycle-accurate bus operations
- **Bus Read/Write Operations**: Read and write single bytes or blocks of data
- **ROM Testing**: Compare ROM contents against expected data
- **Memory Dumping**: Hex dump functionality for memory inspection
- **Interrupt Monitoring**: Read interrupt lines (IRQ, NMI, RESET)
- **Class-Based API**: `BusTester` class with instance methods

#### Usage

```python
import bus_test

# Initialize hardware
bus_test.init()

# Read a byte from address 0x0000
data = bus_test.read(0x0000)

# Write a byte to address 0x0000
bus_test.write(0x0000, 0xAA)

# Read a block of 256 bytes starting at address 0x8000
data_block = bus_test.read_block(0x8000, 256)

# Write a block of data
bus_test.write_block(0x8000, [0x01, 0x02, 0x03, 0x04])

# Test a ROM against expected data
rom_ok = bus_test.test_rom(0xC000, expected_data)

# Get hex dump of memory
dump = bus_test.dump_memory(0x0000, 64)
print(dump)

# Clean up
bus_test.cleanup()
```

#### Class-Based Usage

```python
import bus_test

# Create and initialize tester instance
tester = bus_test.BusTester()
tester.init()

# Use instance methods
data = tester.read_byte(0x0000)
tester.write_byte(0x0000, 0xAA)

# Get bus information
info = tester.get_bus_info()
print("Board:", info['board'])
print("Address space:", info['address_space'])
```

#### API Reference

##### Module-Level Functions

- `set_board_type(board_type)` - Set board type (BOARD_NED_SYS7 only)
- `init()` - Initialize the default bus tester instance
- `cleanup()` - Clean up the default bus tester instance
- `read(address)` - Read a byte using the default tester
- `write(address, data)` - Write a byte using the default tester
- `read_block(address, length)` - Read a block using the default tester
- `write_block(address, data)` - Write a block using the default tester

##### Constants

- `BOARD_NED_SYS7` - Board type constant for Ned's System 7 Board

##### BusTester Class Methods

- `init()` - Initialize hardware GPIO pins
- `read_byte(address)` - Read a single byte from address
- `write_byte(address, data)` - Write a single byte to address
- `read_block(address, length)` - Read multiple bytes from address range
- `write_block(address, data)` - Write multiple bytes to address range
- `bus_read_cycle(address)` - Perform low-level read bus cycle
- `bus_write_cycle(address, data)` - Perform low-level write bus cycle
- `get_bus_info()` - Get bus configuration information
- `test_rom(address, expected_data)` - Test ROM contents against expected data
- `dump_memory(address, length, width=16)` - Create hex dump of memory
- `read_irq()`, `read_nmi()`, `read_reset()` - Read interrupt lines

#### Hardware Requirements

- RP2350-based board (NED_SYS7 recommended)
- GPIO pins 0-7: Data bus (bidirectional)
- GPIO pins 8-23: Address bus (output)
- GPIO pin 24: E clock (output)
- GPIO pin 25: VMA (Valid Memory Address, output)
- GPIO pin 26: R/W (Read/Write, output)
- GPIO pins 27-29: Interrupt inputs (IRQ, NMI, RESET, active low)

### `gpio_test.py` - MC6800 GPIO Hardware Test

Tests all GPIO lines connected to MC6800 pins by pulsing each one HIGH for 10µs in order of MC6800 pin number. This is useful for hardware validation and debugging connectivity issues.

#### Features

- **Pin-by-Pin Testing**: Tests each GPIO pin individually
- **MC6800 Pin Order**: Tests pins in MC6800 pin number order (starting from pin 4)
- **SPI Debug Testing**: Tests the SPI debug interface
- **Continuous Testing**: Runs in a loop for ongoing hardware validation

#### Usage

```python
import gpio_test

# Run the complete test suite
gpio_test.run_test()
```

#### Test Coverage

The test covers all MC6800 CPU pins plus debug interface pins:

- **Address Bus**: A0-A15 (GPIO 8-23)
- **Data Bus**: D0-D7 (GPIO 0-7)
- **Control Signals**: VMA, R/W, E clock
- **Interrupts**: /IRQ, /NMI, /RESET
- **Debug Interface**: SPI pins (CS, SCK, MOSI)

#### Output

For each pin, the test will output:
```
Testing VMA (GPIO24, MC6800 pin 5): PULSED ✓
```

The test runs continuously until interrupted with Ctrl+C.

## Installation and Setup

### Loading MicroPython Firmware

Before using the MicroPython modules, you need to load the MicroPython firmware onto your RP2350 board:

1. **Enter Bootloader Mode**: Put your RP2350 board into bootloader mode by pressing and holding the BOOTSEL button while plugging it in, or by resetting the board while holding BOOTSEL.

2. **Copy UF2 File**: The board will appear as a USB drive. Copy the `micropython.uf2` file from this directory to the USB drive.

3. **Board Reboots**: The board will automatically reboot with MicroPython firmware loaded.

**Note**: This replaces the MC6800 emulator firmware. To return to emulator mode, flash one of the emulator UF2 images from the `images/` directory (e.g., `BOARD_NED_SYS7.uf2`).

### Using the Modules

#### Option 1: Using mpremote (Recommended)

mpremote is a command-line tool for interacting with MicroPython devices. Install it with:

```bash
pip install mpremote
```

**Copy modules to the board:**
```bash
# Copy bus_test.py to the MicroPython board
mpremote cp bus_test.py :

# Copy gpio_test.py to the board
mpremote cp gpio_test.py :
```

**Run code directly:**
```bash
# Run GPIO test
mpremote run gpio_test.py

# Run bus test interactively
mpremote
>>> import bus_test
>>> bus_test.init()
>>> data = bus_test.read(0x0000)
>>> print("Read:", hex(data))
```

**Access REPL:**
```bash
mpremote repl
```

#### Option 2: Using Thonny IDE

1. Install [Thonny IDE](https://thonny.org/)
2. Connect your MicroPython board
3. Use Thonny's file browser to copy `bus_test.py` and `gpio_test.py` to the board
4. Use the REPL or create/run scripts

#### Option 3: Using rshell or ampy

Alternative command-line tools:

```bash
# Install rshell
pip install rshell

# Connect and copy files
rshell -p /dev/ttyACM0 cp bus_test.py /pyboard/
rshell -p /dev/ttyACM0 cp gpio_test.py /pyboard/

# Install ampy
pip install adafruit-ampy

# Copy files with ampy
ampy -p /dev/ttyACM0 put bus_test.py
ampy -p /dev/ttyACM0 put gpio_test.py
```

#### Running Tests

1. Connect to the board using one of the methods above
2. For bus testing, ensure the MC6800 emulator hardware is properly connected
3. Import and use as shown in the examples above

## Dependencies

- MicroPython firmware on RP2350
- `machine` module (built-in)
- `time` module (built-in)
- `gc` module (built-in)
- `sys` module (built-in)

## Hardware Compatibility

The module supports multiple RP2350-based boards with different GPIO configurations:

### NED_SYS7 Board
- **Address Bus**: 16-bit (A0-A15, 64KB address space)
- **GPIO Pins**: 48-pin version with full address bus support
- **Features**: Full 16-bit address decoding, LED indicators

**Note**: Only the NED_SYS7 board is supported by these modules.

## Notes

- The bus_test module performs cycle-accurate operations synchronized to the E clock
- GPIO initialization includes pull-up resistors on data bus inputs to prevent floating
- The E clock synchronization uses a simplified delay; full synchronization would require PIO integration
- All operations include garbage collection calls to prevent memory issues during long operations

## Related Files

- `tests/bus_test.py` - Host-side equivalent using USB serial communication
- `src/bus.h`, `src/bus.c` - C implementation of bus interface
- `src/board_config.h` - GPIO pin definitions and board-specific configurations
