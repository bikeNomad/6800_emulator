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


# Board types (matching board_config.h)
BOARD_PICO2 = 1      # Raspberry Pi Pico 2 W - Limited GPIO (26 pins)
BOARD_NED_SYS7 = 2   # Ned's System 7 Board - Full GPIO (48 pins)

# Default to NED_SYS7 if not specified
BOARD_TYPE = BOARD_NED_SYS7

# GPIO pin mappings - common across boards
GPIO_DATA_BASE = 0   # GPIO 0-7: Data bus (bi-directional)

# Control signals - common across boards
GPIO_IRQ = 27        # /IRQ input (active low)
GPIO_NMI = 28        # /NMI input (active low)
GPIO_RESET = 29      # /RESET input (active low)

# Board-specific configurations
def _get_board_config(board_type):
    """Get board-specific configuration"""
    if board_type == BOARD_PICO2:
        return {
            'name': 'Raspberry Pi Pico 2 W',
            'addr_lines': 7,
            'addr_mask': 0x7C03,  # Bits 0,1,10-14
            'addr_space_size': 128,
            'max_address': 0x007F,
            'addr_gpio_mask': 0x7F00,  # GPIO 8-14
            'gpio_vma': 22,
            'gpio_rw': 23,
            'gpio_e_clock': 21,
            'addr_base': 8,
            'has_leds': False
        }
    else:  # BOARD_NED_SYS7 (default)
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
        board_type: BOARD_PICO2 or BOARD_NED_SYS7

    Note:
        Must be called before init() if using a different board type than default.
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
    push_thresh=32,
    fifo_join=PIO.JOIN_NONE
)
def pio_bus_read_cycle():
    """
    PIO program for MC6800 bus read cycle.
    Matches the timing and behavior of src/bus_cycle.pio read_cycle program.

    Timing (at 266MHz):
    - Data setup delay: 40 cycles = 150.4ns (exceeds MC6800 100ns requirement)
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

    # Data setup delay: 1 + (10 x 8) = 81 cycles = 304.5ns @ 266MHz
    # Initialize loop counter (1 cycle)
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

    Timing (at 266MHz):
    - Hold time delay: 40 cycles = 150.4ns @ 266MHz
    """
    # Entry point for write cycle
    # Software must set up address bus, data bus, and direction before triggering

    # Sync: Wait for E low, VMA=0, R/W=1 (idle)
    wait(0, gpio, GPIO_E_CLOCK)    .side(0b10)

    # Assert VMA=1, R/W=0 (write mode)
    set(pins, 1)                   .side(0b01)

    # Wait for E high (latch), keep signals
    wait(1, gpio, GPIO_E_CLOCK)    .side(0b01)

    # Hold time delay: 1 + (10 x 8) = 81 cycles = 304.5ns @ 266MHz
    # Initialize loop counter (1 cycle)
    set(x, 9)                      .side(0b01)

    # Loop 10 times x 8 cycles = 80 cycles
    label("write_delay")
    jmp(x_dec, "write_delay") [7]  .side(0b01)

    # Wait for E low, keep signals
    wait(0, gpio, GPIO_E_CLOCK)    .side(0b01)

    # Deassert VMA=0, R/W=1 (idle)
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
            self.read_offset = StateMachine(0, pio_bus_read_cycle, freq=266000000)
            self.write_offset = StateMachine(1, pio_bus_write_cycle, freq=266000000)

            # Initialize state machines
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

        except Exception as e:
            raise PIOBusError("Failed to initialize PIO bus interface: {}".format(e))

    def _init_read_sm(self):
        """Initialize the read state machine"""
        if self.read_sm is not None:
            self.read_sm.active(0)

        # Create state machine for read operations
        self.read_sm = StateMachine(
            0,                           # State machine 0
            pio_bus_read_cycle,          # Program
            freq=266000000,              # 266MHz for 3.76ns resolution
            set_base=machine.Pin(GPIO_ADDR_BASE),  # Address bus base
            sideset_base=machine.Pin(GPIO_VMA),    # VMA + R/W (2 bits consecutive)
            in_base=machine.Pin(GPIO_DATA_BASE),   # Data bus base
            out_base=machine.Pin(GPIO_DATA_BASE)   # Data bus base (for output during setup)
        )

    def _init_write_sm(self):
        """Initialize the write state machine"""
        if self.write_sm is not None:
            self.write_sm.active(0)

        # Create state machine for write operations
        self.write_sm = StateMachine(
            1,                           # State machine 1
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

        # Software: Set up address bus, data bus, and direction
        self._drive_address_bus(address)
        self._drive_data_bus(data)

        # Switch to write cycle program
        self.write_sm.active(0)
        self.write_sm.clear_fifos()
        self.write_sm.restart()
        self.write_sm.exec("jmp({})".format(self.write_offset))
        self.write_sm.active(1)

        # Wait for write cycle to complete (approximately 1 E clock cycle)
        # The write cycle program will complete after one full E clock period
        time.sleep_us(2)  # Conservative wait (2µs > 1.117µs E period)

        # Switch back to read cycle program for next operation
        self.write_sm.active(0)
        self.write_sm.clear_fifos()
        self.write_sm.restart()
        self.write_sm.exec("jmp({})".format(self.read_offset))

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
</file_content>

<task_progress>
- [x] Analyze requirements and existing code
- [x] Create comprehensive plan
- [x] Create PIO-based bus test module (pio_bus_test.py)
- [ ] Create standalone test program
- [ ] Test the implementation
- [ ] Verify compatibility with existing code
- [ ] Document the implementation
</task_progress>
</write_to_file>
