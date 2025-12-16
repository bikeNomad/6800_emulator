"""
MC6800 Bus Test Module for MicroPython
Provides functions to read and write via the hardware bus for testing ROMs, PIAs, etc.
Runs directly on the RP2350 board under MicroPython.
"""

import machine
import time
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
            'gpio_vma': 21,
            'gpio_rw': 23,
            'gpio_e_clock': 22,
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
            'gpio_vma': 24,
            'gpio_rw': 26,
            'gpio_e_clock': 25,
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


class BusError(Exception):
    """Exception raised for bus communication errors"""
    pass


class BusTester:
    """
    MC6800 Bus Tester for MicroPython
    Provides methods to read/write data via the hardware bus interface
    """

    def __init__(self):
        """Initialize the bus tester"""
        # GPIO objects (initialized in init())
        self.data_pins = []
        self.addr_pins = []
        self.vma_pin = None
        self.rw_pin = None
        self.irq_pin = None
        self.nmi_pin = None
        self.reset_pin = None
        self.e_clock_pin = None
        self.init()

    def init(self):
        """Initialize the bus interface hardware"""
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

            print("Bus interface initialized for {} board".format(_board_config['name']))
            print("  Data: GPIO {}-{}".format(GPIO_DATA_BASE, GPIO_DATA_BASE + 7))
            print("  Addr: GPIO {}-{} ({} lines)".format(GPIO_ADDR_BASE, addr_gpio_end, _board_config['addr_lines']))
            print("  VMA: GPIO {}".format(GPIO_VMA))
            print("  R/W: GPIO {}".format(GPIO_RW))
            print("  E:   GPIO {}".format(GPIO_E_CLOCK))
            print("  /IRQ: GPIO {}".format(GPIO_IRQ))
            print("  /NMI: GPIO {}".format(GPIO_NMI))
            print("  /RESET: GPIO {}".format(GPIO_RESET))
            print("  Address space: {} bytes (0x{:04X})".format(_board_config['addr_space_size'], _board_config['max_address']))

        except Exception as e:
            raise BusError("Failed to initialize bus interface: {}".format(e))

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

    def _drive_control_read(self):
        """Set control signals for read operation"""
        self.vma_pin.value(1)  # VMA=1
        self.rw_pin.value(1)   # R/W=1 (read)

    def _drive_control_write(self, data):
        """Set control signals and data bus for write operation"""
        # Set data bus to output mode
        for i in range(8):
            self.data_pins[i].init(machine.Pin.OUT)
            self.data_pins[i].value((data >> i) & 1)

        # Set control signals
        self.rw_pin.value(0)   # R/W=0 (write)
        self.vma_pin.value(1)  # VMA=1

    def _deassert_vma(self):
        """De-assert VMA and return R/W to read"""
        self.vma_pin.value(0)  # VMA=0
        self.rw_pin.value(1)   # R/W=1 (read)

    def _data_bus_to_input(self):
        """Set data bus back to input mode"""
        for i in range(8):
            self.data_pins[i].init(machine.Pin.IN, machine.Pin.PULL_UP)

    def _eclock_low(self):
        """Set E clock LOW"""
        self.e_clock_pin.value(0)
        time.sleep_us(1)  # Small delay for signal stability

    def _eclock_high(self):
        """Set E clock HIGH"""
        self.e_clock_pin.value(1)
        time.sleep_us(1)  # Small delay for signal stability

    def _eclock_pulse(self):
        """Complete E clock pulse: LOW -> HIGH -> LOW"""
        self._eclock_low()
        self._eclock_high()
        self._eclock_low()

    def bus_read_cycle(self, address) -> int:
        """
        Perform one read bus cycle (address -> data)
        Synchronized to E clock

        Args:
            address: Address to read from (0-65535)

        Returns:
            The byte value read (0-255)
        """
        if not (0 <= address <= MAX_ADDRESS):
            raise ValueError("Address must be 0-{}".format(MAX_ADDRESS))

        # E clock LOW (beginning of cycle)
        self._eclock_low()

        # Set data bus to input mode
        self._data_bus_to_input()

        # Drive address bus
        self._drive_address_bus(address)

        # Assert VMA and R/W (read = 1)
        self._drive_control_read()

        # E clock HIGH (data valid time)
        self._eclock_high()

        # Read data bus
        data = 0
        for i in range(8):
            if self.data_pins[i].value():
                data |= (1 << i)

        # E clock LOW (end of cycle)
        self._eclock_low()

        # De-assert VMA (R/W stays high)
        self._deassert_vma()

        return data

    def bus_write_cycle(self, address, data):
        """
        Perform one write bus cycle (address + data -> bus)
        Synchronized to E clock

        Args:
            address: Address to write to (0-65535)
            data: Data to write (0-255)
        """
        if not 0 <= address <= MAX_ADDRESS:
            raise ValueError(f"Address must be 0-{MAX_ADDRESS}")
        if not 0 <= data <= 255:
            raise ValueError("Data must be 0-255")

        # E clock LOW (beginning of cycle)
        self._eclock_low()

        # Set data bus to output mode and drive data
        self._drive_control_write(data)

        # Drive address bus
        self._drive_address_bus(address)

        # E clock HIGH (data latches)
        self._eclock_high()

        # E clock LOW (end of cycle)
        self._eclock_low()

        # De-assert VMA and return R/W to read
        self._deassert_vma()

        # Set data bus back to input mode
        self._data_bus_to_input()

    def read_byte(self, address):
        """
        Read a single byte from the specified address.

        Args:
            address: Address to read from (0-65535)

        Returns:
            The byte value read (0-255)
        """
        return self.bus_read_cycle(address)

    def write_byte(self, address, data):
        """
        Write a single byte to the specified address.

        Args:
            address: Address to write to (0-65535)
            data: Data to write (0-255)
        """
        self.bus_write_cycle(address, data)

    def read_block(self, address, length):
        """
        Read a block of bytes from the specified address range.

        Args:
            address: Starting address
            length: Number of bytes to read

        Returns:
            List of byte values
        """
        if not (0 <= address <= MAX_ADDRESS):
            raise ValueError(f"Address must be 0-{MAX_ADDRESS}")
        if address + length > MAX_ADDRESS + 1:
            raise ValueError("Block exceeds address space")

        data = bytearray(length)
        for i in range(length):
            data[i] = self.bus_read_cycle(address + i)

        return data

    def write_block(self, address, data):
        """
        Write a block of bytes to the specified address range.

        Args:
            address: Starting address
            data: Data to write (list of bytes or bytes object)
        """
        if not 0 <= address <= MAX_ADDRESS:
            raise ValueError(f"Address must be 0-{MAX_ADDRESS}")
        if not all(isinstance(b, int) and 0 <= b <= 255 for b in data):
            raise ValueError("All data values must be 0-255")
        if address + len(data) > MAX_ADDRESS + 1:
            raise ValueError("Block exceeds address space")

        for i, byte in enumerate(data):
            self.bus_write_cycle(address + i, byte)

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
            "interface": "Cycle-accurate E-clock synchronized"
        }

    def test_rom(self, address, expected_data):
        """
        Test a ROM by reading data and comparing with expected values.

        Args:
            address: Starting address of ROM
            expected_data: Expected ROM data

        Returns:
            True if ROM matches expected data
        """
        actual_data = self.read_block(address, len(expected_data))
        return actual_data == expected_data
    
    def checksum(self, address, length) -> int:
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
        data = self.read_block(address, length)
        hexdump(data, address)
