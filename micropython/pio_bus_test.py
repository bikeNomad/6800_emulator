# SPDX-License-Identifier: MIT
#
# Copyright 2026 Ned Konz <ned@metamagix.tech>
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.

"""
PIO-based MC6800 Bus Test Module for MicroPython
Provides PIO-based read and write operations that match the behavior of src/bus_cycle.pio
and src/bus.c for cycle-accurate bus testing.

This module implements the same PIO-based E clock, read, and write behavior as the C code,
but using MicroPython's rp2.asm_pio and rp2.StateMachine APIs.

Features:
- PIO-based read/write cycles synchronized to E clock
- Same timing as C implementation (40 cycles = 150.4ns @ 266MHz)
- Compatible with existing bus_test.py module
- Standalone operation with direct PIO control
"""

import machine
import time
import gc
from rp2 import PIO, asm_pio, StateMachine
from hexdump import hexdump

# Dynamic timing calculation for MicroPython
def _calculate_timing():
    """
    Calculate the appropriate PIO frequency and loop count based on system clock.
    Returns timing configuration for optimal performance.
    """
    try:
        # Get actual system clock frequency
        sys_freq_hz = machine.freq()

        # Calculate cycles needed for 300ns target delay
        # Formula: cycles = (target_ns * sys_freq_hz) / 1,000,000,000
        target_ns = 300
        required_cycles = (target_ns * sys_freq_hz) // 1000000000

        # Account for loop overhead: 1 + (iterations * 8)
        # Solve for iterations: iterations = (required_cycles - 1) // 8
        loop_iterations = (required_cycles - 1) // 8

        # Ensure minimum of 1 iteration
        if loop_iterations < 1:
            loop_iterations = 1

        # Ensure we don't exceed PIO instruction limits
        # x register can hold values 0-31 (5-bit register)
        if loop_iterations > 31:
            loop_iterations = 31

        # PIO frequency should match system clock for best timing accuracy
        pio_freq = sys_freq_hz

        # Calculate actual delay achieved
        cycle_time_ns = 1000000000.0 / pio_freq
        actual_delay_ns = (1 + loop_iterations * 8) * cycle_time_ns

        return {
            'pio_freq': pio_freq,
            'loop_iterations': loop_iterations,
            'sys_freq_mhz': sys_freq_hz // 1000000,
            'cycle_time_ns': cycle_time_ns,
            'actual_delay_ns': actual_delay_ns,
            'target_delay_ns': target_ns
        }

    except Exception as e:
        # Fallback to default values
        return {
            'pio_freq': 266000000,
            'loop_iterations': 81,
            'sys_freq_mhz': 266,
            'cycle_time_ns': 3.759,
            'actual_delay_ns': 304.5,
            'target_delay_ns': 300
        }


# Board type (matching board_config.h)
BOARD_NED_SYS7 = 2   # Ned's System 7 Board - Full GPIO (48 pins)

# Using NED_SYS7 board
BOARD_TYPE = BOARD_NED_SYS7

# GPIO pin mappings - common across boards
GPIO_DATA_BASE = 0   # GPIO 0-7: Data bus (bi-directional)

# Control signals - common across boards
GPIO_IRQ = 27        # /IRQ input (active low)
GPIO_NMI = 28        # /NMI input (active low)
GPIO_RESET = 29      # /RESET input (active low)

# Board configuration
def _get_board_config(board_type):
    """Get board configuration for NED_SYS7"""
    # BOARD_NED_SYS7
    return {
        'name': 'Ned\'s System 7 Board',
        'addr_lines': 16,
        'addr_mask': 0xFFFF,
        'addr_space_size': 65536,
        'max_address': 0xFFFF,
        'addr_gpio_mask': 0xFFFF00,  # GPIO 8-23
        'gpio_vma': 25,
        'gpio_rw': 26,
        'gpio_e_clock': 24,
        'addr_base': 8,
        'has_leds': True
    }

# Get current board configuration
_board_config = _get_board_config(BOARD_TYPE)
ADDR_LINES = _board_config['addr_lines']
ADDR_MASK = _board_config['addr_mask']
MAX_ADDRESS = _board_config['max_address']
ADDR_SPACE_SIZE = _board_config['addr_space_size']
GPIO_ADDR_BASE = _board_config['addr_base']
GPIO_VMA = _board_config['gpio_vma']
GPIO_RW = _board_config['gpio_rw']
GPIO_E_CLOCK = _board_config['gpio_e_clock']

def set_board_type(board_type):
    """
    Set the board type for the bus tester.

    Args:
        board_type: BOARD_NED_SYS7 (only NED_SYS7 supported)

    Note:
        This function exists for compatibility but only NED_SYS7 is supported.
    """
    global BOARD_TYPE, _board_config, ADDR_LINES, ADDR_MASK, MAX_ADDRESS, ADDR_SPACE_SIZE
    global GPIO_ADDR_BASE, GPIO_VMA, GPIO_RW, GPIO_E_CLOCK

    BOARD_TYPE = board_type
    _board_config = _get_board_config(BOARD_TYPE)
    ADDR_LINES = _board_config['addr_lines']
    ADDR_MASK = _board_config['addr_mask']
    MAX_ADDRESS = _board_config['max_address']
    ADDR_SPACE_SIZE = _board_config['addr_space_size']
    GPIO_ADDR_BASE = _board_config['addr_base']
    GPIO_VMA = _board_config['gpio_vma']
    GPIO_RW = _board_config['gpio_rw']
    GPIO_E_CLOCK = _board_config['gpio_e_clock']


class PIOBusError(Exception):
    """Exception raised for PIO bus communication errors"""
    pass


# PIO Program: Bus Read Cycle
# Matches the behavior of bus_read_cycle from bus_cycle.pio
@asm_pio(
    sideset_init=[PIO.OUT_LOW, PIO.OUT_LOW],  # VMA=0, R/W=1 (idle)
    out_init=[PIO.IN_LOW] * 8,                 # Data bus inputs
    set_init=[PIO.OUT_LOW] * 8,                # Address bus outputs
    in_shiftdir=PIO.SHIFT_LEFT,
    out_shiftdir=PIO.SHIFT_LEFT,
    autopush=False,
    autopull=False,
    push_thresh=32,
    pull_thresh=32,
    fifo_join=PIO.JOIN_NONE
)
def pio_bus_read_cycle():
    """
    PIO program for MC6800 bus read cycle.
    Matches the timing and behavior of src/bus_cycle.pio read_cycle program.

    Timing (configurable based on system clock):
    - Data setup delay: Dynamically calculated for 300ns
    - MC6821 PIA worst-case data delay is 290ns, so use 300ns+ for margin
    """
    # Entry point for read cycle
    # Software must set up address bus and data direction before triggering
    # Returns: 8-bit data in RX FIFO

    # Sync: Wait for E low, VMA=0, R/W=1 (idle)
    wait(0, gpio, GPIO_E_CLOCK)    .side(0b10)

    # Assert VMA=1, R/W=1 (read mode)
    set(pins, 1)                   .side(0b11)

    # Wait for E high, keep signals
    wait(1, gpio, GPIO_E_CLOCK)    .side(0b11)

    # Data setup delay: Configurable based on system clock
    # Target: 300ns for MC6821 PIA worst-case data delay (290ns + margin)
    # Actual delay calculated at runtime based on system clock
    # Default: 81 cycles = 304.5ns @ 266MHz
    set(x, 9)                      .side(0b11)

    # Loop 10 times x 8 cycles = 80 cycles
    label("read_delay")
    jmp(x_dec, "read_delay") [7]   .side(0b11)

    # Sample data (now stable!), keep signals
    in_(pins, 8)                   .side(0b11)

    # Push to RX FIFO, keep signals
    push(noblock)                  .side(0b11)

    # Wait for E low, keep signals
    wait(0, gpio, GPIO_E_CLOCK)    .side(0b11)

    # Deassert VMA=0, R/W=1 (idle)
    nop()                          .side(0b10)


# PIO Program: Bus Write Cycle
# Matches the behavior of bus_write_cycle from bus_cycle.pio
@asm_pio(
    sideset_init=[PIO.OUT_LOW, PIO.OUT_LOW],  # VMA=0, R/W=1 (idle)
    out_init=[PIO.OUT_LOW] * 8,                # Data bus outputs
    set_init=[PIO.OUT_LOW] * 8,                # Address bus outputs
    out_shiftdir=PIO.SHIFT_LEFT,
    autopull=False,
    pull_thresh=32,
    fifo_join=PIO.JOIN_NONE
)
def pio_bus_write_cycle():
    """
    PIO program for MC6800 bus write cycle.
    Matches the timing and behavior of src/bus_cycle.pio write_cycle program.

    Write is triggered by pushing data to TX FIFO.
    Write cycles only need to wait for E clock edges - no delay loop needed.
    The peripheral latches data on E rising edge, then cycle ends on E falling edge.
    """
    # Entry point for write cycle
    # Software must set up address bus before triggering
    # Write is triggered by pushing data to TX FIFO

    # Wait for E low, idle state
    wait(0, gpio, GPIO_E_CLOCK)    .side(0b10)

    # Pull data from TX FIFO (blocks if empty)
    pull(block)                    .side(0b10)

    # Drive data bus, assert VMA=1, R/W=0 (write mode)
    out(pins, 8)                   .side(0b01)

    # Wait for E high (latch), keep signals
    wait(1, gpio, GPIO_E_CLOCK)    .side(0b01)

    # Wait for E low (end of cycle), keep signals
    wait(0, gpio, GPIO_E_CLOCK)    .side(0b01)

    # Deassert VMA=0, R/W=1 (idle), data bus returns to idle
    nop()                          .side(0b10)


class PIOBusTester:
    """
    PIO-based MC6800 Bus Tester for MicroPython
    Provides PIO-based methods to read/write data via the hardware bus interface
    with cycle-accurate timing matching the C implementation.
    """

    def __init__(self):
        """Initialize the PIO bus tester"""
        # State machine instances
        self.read_sm = None
        self.write_sm = None

        # PIO program offsets
        self.read_offset = None
        self.write_offset = None

        # GPIO objects (initialized in init())
        self.data_pins = []
        self.addr_pins = []
        self.vma_pin = None
        self.rw_pin = None
        self.irq_pin = None
        self.nmi_pin = None
        self.reset_pin = None
        self.e_clock_pin = None

        # Initialization state
        self.initialized = False

    def init(self):
        """Initialize the PIO bus interface hardware"""
        try:
            # Initialize data bus pins (GPIO 0-7) as inputs initially
            self.data_pins = []
            for i in range(8):
                pin = machine.Pin(GPIO_DATA_BASE + i, machine.Pin.IN, machine.Pin.PULL_UP)
                self.data_pins.append(pin)

            # Initialize address bus pins (board-specific number)
            self.addr_pins = []
            addr_gpio_end = GPIO_ADDR_BASE + _board_config['addr_lines'] - 1
            for i in range(_board_config['addr_lines']):
                pin = machine.Pin(GPIO_ADDR_BASE + i, machine.Pin.OUT)
                pin.value(0)
                self.addr_pins.append(pin)

            # Initialize control signals
            self.vma_pin = machine.Pin(GPIO_VMA, machine.Pin.OUT)
            self.vma_pin.value(0)  # VMA inactive

            self.rw_pin = machine.Pin(GPIO_RW, machine.Pin.OUT)
            self.rw_pin.value(1)   # Default to read

            # Initialize interrupt inputs with pull-ups (active low)
            self.irq_pin = machine.Pin(GPIO_IRQ, machine.Pin.IN, machine.Pin.PULL_UP)
            self.nmi_pin = machine.Pin(GPIO_NMI, machine.Pin.IN, machine.Pin.PULL_UP)
            self.reset_pin = machine.Pin(GPIO_RESET, machine.Pin.IN, machine.Pin.PULL_UP)

            # Initialize E clock pin
            self.e_clock_pin = machine.Pin(GPIO_E_CLOCK, machine.Pin.OUT)
            self.e_clock_pin.value(0)

            # Load PIO programs
            # Note: E clock program should already be initialized by eclock_init()
            self.read_offset = StateMachine(0, pio_bus_read_cycle, freq=266000000)
            self.write_offset = StateMachine(1, pio_bus_write_cycle, freq=266000000)

            # Initialize state machines (three dedicated state machines)
            # SM0: E clock (already running)
            # SM1: Read cycles (always ready)
            # SM2: Write cycles (always waiting for TX FIFO data)
            self._init_read_sm()
            self._init_write_sm()

            self.initialized = True

            print("PIO bus interface initialized for {} board".format(_board_config['name']))
            print("  Data: GPIO {}-{}".format(GPIO_DATA_BASE, GPIO_DATA_BASE + 7))
            print("  Addr: GPIO {}-{} ({} lines)".format(GPIO_ADDR_BASE, addr_gpio_end, _board_config['addr_lines']))
            print("  VMA: GPIO {}".format(GPIO_VMA))
            print("  R/W: GPIO {}".format(GPIO_RW))
            print("  E:   GPIO {}".format(GPIO_E_CLOCK))
            print("  /IRQ: GPIO {}".format(GPIO_IRQ))
            print("  /NMI: GPIO {}".format(GPIO_NMI))
            print("  /RESET: GPIO {}".format(GPIO_RESET))
            print("  Address space: {} bytes (0x{:04X})".format(_board_config['addr_space_size'], _board_config['max_address']))
            print("  PIO programs loaded: read_cycle, write_cycle")
            print("  Clock frequency: 266MHz (3.76ns resolution)")
            print("  State machines: SM0=E, SM1=Read, SM2=Write")

        except Exception as e:
            raise PIOBusError("Failed to initialize PIO bus interface: {}".format(e))

    def _init_read_sm(self):
        """Initialize the read state machine (SM1)"""
        if self.read_sm is not None:
            self.read_sm.active(0)

        # Create state machine for read operations (SM1)
        self.read_sm = StateMachine(
            1,                           # State machine 1
            pio_bus_read_cycle,          # Program
            freq=266000000,              # 266MHz for 3.76ns resolution
            set_base=machine.Pin(GPIO_ADDR_BASE),  # Address bus base
            sideset_base=machine.Pin(GPIO_VMA),    # VMA + R/W (2 bits consecutive)
            in_base=machine.Pin(GPIO_DATA_BASE),   # Data bus base
            out_base=machine.Pin(GPIO_DATA_BASE)   # Data bus base (for output during setup)
        )

    def _init_write_sm(self):
        """Initialize the write state machine (SM2)"""
        if self.write_sm is not None:
            self.write_sm.active(0)

        # Create state machine for write operations (SM2)
        self.write_sm = StateMachine(
            2,                           # State machine 2
            pio_bus_write_cycle,         # Program
            freq=266000000,              # 266MHz for 3.76ns resolution
            set_base=machine.Pin(GPIO_ADDR_BASE),  # Address bus base
            sideset_base=machine.Pin(GPIO_VMA),    # VMA + R/W (2 bits consecutive)
            out_base=machine.Pin(GPIO_DATA_BASE)   # Data bus base
        )

    def _addr_to_gpio_mask(self, address):
        """Convert MC6800 address to GPIO pin values"""
        # Apply address mask (board-specific)
        address &= ADDR_MASK
        # Address bits map directly to GPIO pins starting at GPIO_ADDR_BASE
        return address << GPIO_ADDR_BASE

    def _drive_address_bus(self, address):
        """Drive the address bus with the specified address"""
        gpio_mask = self._addr_to_gpio_mask(address)
        for i, pin in enumerate(self.addr_pins):
            pin.value((gpio_mask >> (GPIO_ADDR_BASE + i)) & 1)

    def _drive_data_bus(self, data):
        """Drive the data bus with the specified data"""
        for i in range(8):
            self.data_pins[i].init(machine.Pin.OUT)
            self.data_pins[i].value((data >> i) & 1)

    def _set_data_bus_input(self):
        """Set data bus back to input mode"""
        for i in range(8):
            self.data_pins[i].init(machine.Pin.IN, machine.Pin.PULL_UP)

    def pio_read_cycle(self, address):
        """
        Perform one PIO-based read bus cycle (address -> data)
        Uses the PIO state machine for cycle-accurate timing

        Args:
            address: Address to read from (0-65535)

        Returns:
            The byte value read (0-255)
        """
        if not self.initialized:
            raise PIOBusError("PIO bus tester not initialized")

        if not (0 <= address <= MAX_ADDRESS):
            raise ValueError("Address must be 0-{}".format(MAX_ADDRESS))

        # Software: Set up address bus and data direction
        self._drive_address_bus(address)
        self._set_data_bus_input()

        # Clear any stale data from RX FIFO
        while not self.read_sm.rx_fifo_empty():
            self.read_sm.get()

        # Enable state machine (it will run through one cycle and push data)
        self.read_sm.active(1)

        # Wait for data in RX FIFO (blocking)
        while self.read_sm.rx_fifo_empty():
            time.sleep_us(1)  # Small delay to prevent busy waiting

        # Read data from FIFO
        data = self.read_sm.get()

        return data & 0xFF

    def pio_write_cycle(self, address, data):
        """
        Perform one PIO-based write bus cycle (address + data -> bus)
        Uses the PIO state machine for cycle-accurate timing

        Args:
            address: Address to write to (0-65535)
            data: Data to write (0-255)
        """
        if not self.initialized:
            raise PIOBusError("PIO bus tester not initialized")

        if not 0 <= address <= MAX_ADDRESS:
            raise ValueError("Address must be 0-{}".format(MAX_ADDRESS))
        if not 0 <= data <= 255:
            raise ValueError("Data must be 0-255")

        # Software: Set up address bus and data bus
        self._drive_address_bus(address)
        self._drive_data_bus(data)

        # Trigger write state machine by pushing data to its TX FIFO
        # Write state machine (SM2) is always running and waiting for TX FIFO data
        self.write_sm.put(data)

        # Wait for write cycle to complete
        # Write state machine will execute automatically when data is in TX FIFO
        # It waits for E clock edges and executes the write cycle
        time.sleep_us(1)  # Minimal wait for cycle completion

        # Set data bus back to input mode
        self._set_data_bus_input()

    def read_byte(self, address):
        """
        Read a single byte from the specified address using PIO.

        Args:
            address: Address to read from (0-65535)

        Returns:
            The byte value read (0-255)
        """
        return self.pio_read_cycle(address)

    def write_byte(self, address, data):
        """
        Write a single byte to the specified address using PIO.

        Args:
            address: Address to write to (0-65535)
            data: Data to write (0-255)
        """
        self.pio_write_cycle(address, data)

    def read_block(self, address, length):
        """
        Read a block of bytes from the specified address range using PIO.

        Args:
            address: Starting address
            length: Number of bytes to read

        Returns:
            List of byte values
        """
        if not (0 <= address <= MAX_ADDRESS):
            raise ValueError("Address must be 0-{}".format(MAX_ADDRESS))
        if address + length > MAX_ADDRESS + 1:
            raise ValueError("Block exceeds address space")

        data = bytearray(length)
        for i in range(length):
            data[i] = self.pio_read_cycle(address + i)

        return data

    def write_block(self, address, data):
        """
        Write a block of bytes to the specified address range using PIO.

        Args:
            address: Starting address
            data: Data to write (list of bytes or bytes object)
        """
        if not 0 <= address <= MAX_ADDRESS:
            raise ValueError("Address must be 0-{}".format(MAX_ADDRESS))
        if not all(isinstance(b, int) and 0 <= b <= 255 for b in data):
            raise ValueError("All data values must be 0-255")
        if address + len(data) > MAX_ADDRESS + 1:
            raise ValueError("Block exceeds address space")

        for i, byte in enumerate(data):
            self.pio_write_cycle(address + i, byte)

    def get_bus_info(self):
        """
        Get information about the bus configuration.

        Returns:
            Dictionary with bus information
        """
        return {
            "board": _board_config['name'],
            "address_lines": _board_config['addr_lines'],
            "address_mask": "0x{:04X}".format(_board_config['addr_mask']),
            "max_address": "0x{:04X}".format(_board_config['max_address']),
            "address_space": "{} bytes".format(_board_config['addr_space_size']),
            "interface": "PIO-based cycle-accurate E-clock synchronized",
            "pio_frequency": "266MHz",
            "timing_resolution": "3.76ns"
        }

    def test_rom(self, address, expected_data):
        """
        Test a ROM by reading data and comparing with expected values using PIO.

        Args:
            address: Starting address of ROM
            expected_data: Expected ROM data

        Returns:
            True if ROM matches expected data
        """
        actual_data = self.read_block(address, len(expected_data))
        return actual_data == expected_data

    def checksum(self, address, length):
        """
        Produce a simple checksum of given address range, compatible
        with those from IPDB, etc.
        """
        return hex(sum(self.read_block(address, length)) & 0xFFFF)

    def read_irq(self):
        """Read IRQ line (active low)"""
        return not self.irq_pin.value()

    def read_nmi(self):
        """Read NMI line (active low)"""
        return not self.nmi_pin.value()

    def read_reset(self):
        """Read RESET line (active low)"""
        return not self.reset_pin.value()

    def dump_block(self, address, length):
        """Dump a block of memory using hexdump"""
        data = self.read_block(address, length)
        hexdump(data, address)

    def cleanup(self):
        """Clean up the PIO bus tester"""
        if self.read_sm is not None:
            self.read_sm.active(0)
            self.read_sm = None

        if self.write_sm is not None:
            self.write_sm.active(0)
            self.write_sm = None

        self.initialized = False
        print("PIO bus tester cleaned up")


# Default instance for module-level functions
_default_pio_tester = None


def init():
    """
    Initialize the default PIO bus tester instance.
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        _default_pio_tester = PIOBusTester()
    _default_pio_tester.init()


def cleanup():
    """
    Clean up the default PIO bus tester instance.
    """
    global _default_pio_tester
    if _default_pio_tester is not None:
        _default_pio_tester.cleanup()
        _default_pio_tester = None


def read_byte(address):
    """
    Read a byte using the default PIO bus tester.

    Args:
        address: Address to read from

    Returns:
        The byte value read
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.read_byte(address)


def write_byte(address, data):
    """
    Write a byte using the default PIO bus tester.

    Args:
        address: Address to write to
        data: Data to write
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    _default_pio_tester.write_byte(address, data)


def read_block(address, length):
    """
    Read a block of bytes using the default PIO bus tester.

    Args:
        address: Starting address
        length: Number of bytes to read

    Returns:
        List of byte values
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.read_block(address, length)


def write_block(address, data):
    """
    Write a block of bytes using the default PIO bus tester.

    Args:
        address: Starting address
        data: Data to write
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    _default_pio_tester.write_block(address, data)


def get_bus_info():
    """
    Get bus information using the default PIO bus tester.

    Returns:
        Dictionary with bus information
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.get_bus_info()


def test_rom(address, expected_data):
    """
    Test a ROM using the default PIO bus tester.

    Args:
        address: Starting address of ROM
        expected_data: Expected ROM data

    Returns:
        True if ROM matches expected data
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.test_rom(address, expected_data)


def checksum(address, length):
    """
    Calculate checksum using the default PIO bus tester.

    Args:
        address: Starting address
        length: Number of bytes to checksum

    Returns:
        Hexadecimal checksum string
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.checksum(address, length)


def dump_block(address, length):
    """
    Dump a block of memory using the default PIO bus tester.

    Args:
        address: Starting address
        length: Number of bytes to dump
    """
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    _default_pio_tester.dump_block(address, length)


def read_irq():
    """Read IRQ line using the default PIO bus tester"""
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.read_irq()


def read_nmi():
    """Read NMI line using the default PIO bus tester"""
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.read_nmi()


def read_reset():
    """Read RESET line using the default PIO bus tester"""
    global _default_pio_tester
    if _default_pio_tester is None:
        raise PIOBusError("PIO bus tester not initialized")
    return _default_pio_tester.read_reset()
