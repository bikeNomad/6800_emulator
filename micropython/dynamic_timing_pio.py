"""
Dynamic Timing PIO Implementation for MicroPython
Uses machine.freq() to calculate loop counts based on actual system clock
"""

import machine
import time
from rp2 import PIO, asm_pio, StateMachine


# Board configuration (matching board_config.h)
BOARD_PICO2 = 1
BOARD_NED_SYS7 = 2
BOARD_TYPE = BOARD_NED_SYS7

# GPIO pin mappings
GPIO_DATA_BASE = 0
GPIO_IRQ = 27
GPIO_NMI = 28
GPIO_RESET = 29

def _get_board_config(board_type):
    """Get board-specific configuration"""
    if board_type == BOARD_PICO2:
        return {
            'name': 'Raspberry Pi Pico 2 W',
            'addr_lines': 7,
            'addr_mask': 0x7C03,
            'max_address': 0x007F,
            'gpio_vma': 22,
            'gpio_rw': 23,
            'gpio_e_clock': 21,
            'addr_base': 8,
        }
    else:
        return {
            'name': 'Ned\'s System 7 Board',
            'addr_lines': 16,
            'addr_mask': 0xFFFF,
            'max_address': 0xFFFF,
            'gpio_vma': 25,
            'gpio_rw': 26,
            'gpio_e_clock': 24,
            'addr_base': 8,
        }

_board_config = _get_board_config(BOARD_TYPE)
GPIO_VMA = _board_config['gpio_vma']
GPIO_RW = _board_config['gpio_rw']
GPIO_E_CLOCK = _board_config['gpio_e_clock']
GPIO_ADDR_BASE = _board_config['addr_base']
ADDR_MASK = _board_config['addr_mask']
MAX_ADDRESS = _board_config['max_address']


def calculate_loop_count(target_ns=300):
    """
    Calculate the number of loop iterations needed for target delay
    based on the actual system clock frequency.

    Args:
        target_ns: Target delay in nanoseconds (default 300ns)

    Returns:
        Number of loop iterations needed
    """
    # Get actual system clock frequency
    sys_freq_hz = machine.freq()

    # Calculate cycles needed for target delay
    # Formula: cycles = (target_ns * sys_freq_hz) / 1,000,000,000
    required_cycles = (target_ns * sys_freq_hz) // 1000000000

    # Account for the loop overhead:
    # - set(x, N) instruction: 1 cycle
    # - jmp(x_dec, label) [7] instruction: 8 cycles per iteration
    # Total per iteration: 8 cycles
    # Total loop cycles = 1 + (iterations * 8)

    # Solve for iterations: iterations = (required_cycles - 1) / 8
    loop_iterations = (required_cycles - 1) // 8

    # Ensure minimum of 1 iteration
    if loop_iterations < 1:
        loop_iterations = 1

    # Ensure we don't exceed PIO instruction limits
    # jmp with delay [7] is maximum delay per instruction
    # x register can hold values 0-31 (5-bit register)
    if loop_iterations > 31:
        loop_iterations = 31

    return loop_iterations, sys_freq_hz, required_cycles


def generate_pio_read_program(loop_iterations):
    """
    Generate PIO read program with calculated loop count.

    Args:
        loop_iterations: Number of loop iterations for delay

    Returns:
        PIO program source as string
    """
    return f"""
.program pio_bus_read_cycle

.side_set 2         ; GPIO {GPIO_VMA}=VMA, {GPIO_RW}=R/W

public read_cycle:
    wait 0 gpio {GPIO_E_CLOCK}      .side 0b10       ; Sync: Wait for E low, VMA=0, R/W=1 (idle)
    nop                 .side 0b11       ; Assert VMA=1, R/W=1 (read mode)
    wait 1 gpio {GPIO_E_CLOCK}      .side 0b11       ; Wait for E high, keep signals
    ; Data setup delay: Calculated for {300}ns at current system clock
    ; Loop iterations: {loop_iterations} (target: ~300ns)
    set x, {loop_iterations}            .side 0b11       ; Initialize loop counter
read_delay:
    jmp x-- read_delay [7] .side 0b11   ; Loop {loop_iterations} times x 8 cycles
    in pins, 8          .side 0b11       ; Sample data (now stable!), keep signals
    push noblock        .side 0b11       ; Push to RX FIFO, keep signals
    wait 0 gpio {GPIO_E_CLOCK}      .side 0b11       ; Wait for E low, keep signals
    nop                 .side 0b10       ; Deassert VMA=0, R/W=1 (idle)

.wrap
"""


def generate_pio_write_program(loop_iterations):
    """
    Generate PIO write program with calculated loop count.

    Args:
        loop_iterations: Number of loop iterations for delay

    Returns:
        PIO program source as string
    """
    return f"""
.program pio_bus_write_cycle

.side_set 2         ; GPIO {GPIO_VMA}=VMA, {GPIO_RW}=R/W

public write_cycle:
    wait 0 gpio {GPIO_E_CLOCK}      .side 0b10       ; Sync: Wait for E low, VMA=0, R/W=1 (idle)
    nop                 .side 0b01       ; Assert VMA=1, R/W=0 (write mode)
    wait 1 gpio {GPIO_E_CLOCK}      .side 0b01       ; Wait for E high (latch), keep signals
    ; Hold time delay: Calculated for {300}ns at current system clock
    ; Loop iterations: {loop_iterations} (target: ~300ns)
    set x, {loop_iterations}            .side 0b01       ; Initialize loop counter
write_delay:
    jmp x-- write_delay [7] .side 0b01  ; Loop {loop_iterations} times x 8 cycles
    wait 0 gpio {GPIO_E_CLOCK}      .side 0b01       ; Wait for E low, keep signals
    nop                 .side 0b10       ; Deassert VMA=0, R/W=1 (idle)

.wrap
"""


class DynamicPIOBusTester:
    """
    Dynamic PIO-based MC6800 Bus Tester for MicroPython
    Automatically calculates timing based on actual system clock
    """

    def __init__(self):
        """Initialize the dynamic PIO bus tester"""
        self.read_sm = None
        self.write_sm = None
        self.data_pins = []
        self.addr_pins = []
        self.vma_pin = None
        self.rw_pin = None
        self.initialized = False

        # Calculate timing based on current system clock
        self.loop_iterations, self.sys_freq_hz, self.required_cycles = calculate_loop_count()
        self.cycle_time_ns = 1000000000.0 / self.sys_freq_hz
        self.actual_delay_ns = (1 + self.loop_iterations * 8) * self.cycle_time_ns

        print(f"Dynamic PIO Bus Tester initialized:")
        print(f"  System clock: {self.sys_freq_hz // 1000000} MHz")
        print(f"  Cycle time: {self.cycle_time_ns:.3f} ns")
        print(f"  Loop iterations: {self.loop_iterations}")
        print(f"  Actual delay: {self.actual_delay_ns:.1f} ns (target: 300 ns)")

    def init(self):
        """Initialize the PIO bus interface with dynamic timing"""
        try:
            # Generate PIO programs with calculated timing
            read_program_src = generate_pio_read_program(self.loop_iterations)
            write_program_src = generate_pio_write_program(self.loop_iterations)

            # Compile PIO programs
            read_program = asm_pio(
                sideset_init=[PIO.OUT_LOW, PIO.OUT_LOW],
                out_init=[PIO.IN_LOW] * 8,
                set_init=[PIO.OUT_LOW] * 8,
                in_shiftdir=PIO.SHIFT_LEFT,
                out_shiftdir=PIO.SHIFT_LEFT,
                autopush=False,
                push_thresh=32,
                fifo_join=PIO.JOIN_NONE
            )(read_program_src)

            write_program = asm_pio(
                sideset_init=[PIO.OUT_LOW, PIO.OUT_LOW],
                out_init=[PIO.OUT_LOW] * 8,
                set_init=[PIO.OUT_LOW] * 8,
                out_shiftdir=PIO.SHIFT_LEFT,
                autopull=False,
                pull_thresh=32,
                fifo_join=PIO.JOIN_NONE
            )(write_program_src)

            # Initialize GPIO pins
            self._init_gpio()

            # Create state machines with system clock frequency
            self.read_sm = StateMachine(
                0,
                read_program,
                freq=self.sys_freq_hz,  # Use actual system clock
                set_base=machine.Pin(GPIO_ADDR_BASE),
                sideset_base=machine.Pin(GPIO_VMA),
                in_base=machine.Pin(GPIO_DATA_BASE),
                out_base=machine.Pin(GPIO_DATA_BASE)
            )

            self.write_sm = StateMachine(
                1,
                write_program,
                freq=self.sys_freq_hz,  # Use actual system clock
                set_base=machine.Pin(GPIO_ADDR_BASE),
                sideset_base=machine.Pin(GPIO_VMA),
                out_base=machine.Pin(GPIO_DATA_BASE)
            )

            self.initialized = True
            print(f"PIO bus interface initialized with dynamic timing")

        except Exception as e:
            raise Exception(f"Failed to initialize dynamic PIO bus interface: {e}")

    def _init_gpio(self):
        """Initialize GPIO pins"""
        # Initialize data bus pins
        for i in range(8):
            pin = machine.Pin(GPIO_DATA_BASE + i, machine.Pin.IN, machine.Pin.PULL_UP)
            self.data_pins.append(pin)

        # Initialize address bus pins
        for i in range(_board_config['addr_lines']):
            pin = machine.Pin(GPIO_ADDR_BASE + i, machine.Pin.OUT)
            pin.value(0)
            self.addr_pins.append(pin)

        # Initialize control signals
        self.vma_pin = machine.Pin(GPIO_VMA, machine.Pin.OUT)
        self.vma_pin.value(0)
        self.rw_pin = machine.Pin(GPIO_RW, machine.Pin.OUT)
        self.rw_pin.value(1)

    def _drive_address_bus(self, address):
        """Drive the address bus"""
        address &= ADDR_MASK
        for i, pin in enumerate(self.addr_pins):
            pin.value((address >> i) & 1)

    def _drive_data_bus(self, data):
        """Drive the data bus"""
        for i in range(8):
            self.data_pins[i].init(machine.Pin.OUT)
            self.data_pins[i].value((data >> i) & 1)

    def _set_data_bus_input(self):
        """Set data bus to input mode"""
        for i in range(8):
            self.data_pins[i].init(machine.Pin.IN, machine.Pin.PULL_UP)

    def read_byte(self, address):
        """Read a byte with dynamic timing"""
        if not self.initialized:
            raise Exception("PIO bus tester not initialized")

        if not (0 <= address <= MAX_ADDRESS):
            raise ValueError(f"Address must be 0-{MAX_ADDRESS}")

        # Set up address and data direction
        self._drive_address_bus(address)
        self._set_data_bus_input()

        # Clear FIFO and start state machine
        while not self.read_sm.rx_fifo_empty():
            self.read_sm.get()
        self.read_sm.active(1)

        # Wait for data
        while self.read_sm.rx_fifo_empty():
            time.sleep_us(1)

        # Read data
        data = self.read_sm.get()
        return data & 0xFF

    def write_byte(self, address, data):
        """Write a byte with dynamic timing"""
        if not self.initialized:
            raise Exception("PIO bus tester not initialized")

        if not (0 <= address <= MAX_ADDRESS):
            raise ValueError(f"Address must be 0-{MAX_ADDRESS}")
        if not (0 <= data <= 255):
            raise ValueError("Data must be 0-255")

        # Set up address, data, and direction
        self._drive_address_bus(address)
        self._drive_data_bus(data)

        # Switch to write program and execute
        self.write_sm.active(0)
        self.write_sm.clear_fifos()
        self.write_sm.restart()
        self.write_sm.exec("jmp(0)")  # Jump to write_cycle
        self.write_sm.active(1)

        # Wait for completion
        time.sleep_us(2)

        # Switch back to read program
        self.write_sm.active(0)
        self.write_sm.clear_fifos()
        self.write_sm.restart()
        self.write_sm.exec("jmp(0)")  # Jump to read_cycle

        # Set data bus back to input
        self._set_data_bus_input()

    def get_timing_info(self):
        """Get current timing configuration"""
        return {
            "system_clock_mhz": self.sys_freq_hz // 1000000,
            "cycle_time_ns": round(self.cycle_time_ns, 3),
            "loop_iterations": self.loop_iterations,
            "actual_delay_ns": round(self.actual_delay_ns, 1),
            "target_delay_ns": 300,
            "margin_ns": round(self.actual_delay_ns - 300, 1)
        }

    def cleanup(self):
        """Clean up resources"""
        if self.read_sm:
            self.read_sm.active(0)
        if self.write_sm:
            self.write_sm.active(0)
        self.initialized = False
        print("Dynamic PIO bus tester cleaned up")


# Example usage and testing
def test_dynamic_timing():
    """Test the dynamic timing implementation"""
    print("\n" + "="*60)
    print("Dynamic Timing Test")
    print("="*60)

    # Create tester (automatically calculates timing)
    tester = DynamicPIOBusTester()

    # Show timing configuration
    timing_info = tester.get_timing_info()
    print(f"\nTiming Configuration:")
    for key, value in timing_info.items():
        print(f"  {key}: {value}")

    # Test with different system clock frequencies
    print(f"\nTesting with current system clock: {timing_info['system_clock_mhz']} MHz")

    # Note: In a real test, you would:
    # 1. Change system clock with machine.freq(new_freq)
    # 2. Recreate the tester to recalculate timing
    # 3. Verify timing is still correct

    print("\nDynamic timing implementation complete!")
    print("To test with different clocks:")
    print("  1. Use machine.freq(new_freq) to change system clock")
    print("  2. Create new DynamicPIOBusTester() instance")
    print("  3. Timing automatically recalculates for new clock speed")


if __name__ == "__main__":
    test_dynamic_timing()
