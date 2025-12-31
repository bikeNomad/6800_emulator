from machine import Pin
from micropython import const
import rp2
from board_config import (
    GPIO_ADDR_BASE, GPIO_DATA_BASE, GPIO_E_CLOCK, GPIO_VMA, GPIO_RW,
    GPIO_IRQ, GPIO_NMI, GPIO_RESET, N_ADDR_DATA_PINS
)


# Constants
_ADDR_SHIFT = const(8)
assert _ADDR_SHIFT == GPIO_ADDR_BASE
_E_CLOCK = const(24)
assert _E_CLOCK == GPIO_E_CLOCK
_WRITE_PINDIRS = const(0xFFFFFFFF)
_READ_PINDIRS = const(0xFFFFFF00)

# Globals
bus_cycle_sm = None # State machine instance after initialization
irq_pin = Pin(GPIO_IRQ, Pin.IN, Pin.PULL_UP)
nmi_pin = Pin(GPIO_NMI, Pin.IN, Pin.PULL_UP)
reset_pin = Pin(GPIO_RESET, Pin.IN, Pin.PULL_UP)


# OUT pins: 8 data + 16 address
# SIDESET pins: VMA, RW
# Uses E Clock as a wait source
@rp2.asm_pio(sideset_init=([rp2.PIO.OUT_LOW]*2),
             out_init=([rp2.PIO.OUT_LOW]*24))
def bus_cycle():
    wrap_target()

    label("loop")

    pull(block)             .side(2)      # 0 read pindirs into OSR
    mov(x, osr)             .side(2)      # 1 and into X
    mov(y, invert(x))       .side(2)      # 2 Y is 0 for writes
    pull(block)             .side(2)      # 3 OSR has address+data
    wait(1, gpio, _E_CLOCK) .side(2)      # 4 Wait for E high
    wait(0, gpio, _E_CLOCK) .side(2)      # 5 Wait for E low (start of cycle)
    jmp(not_y, "do_write")  .side(2)      # 6 VMA low

    label("do_read")

    word(0xb861)            .side(3)      # 7 (mov pindirs, x side 3)
    out(pins, 32)           .side(3)      # 8 drive address (data lines are inputs)
    wait(1, gpio, _E_CLOCK) .side(3)      # 9 Wait for E high
    wait(0, gpio, _E_CLOCK) .side(3)      # 10 Wait for E low (end of cycle)
    in_(pins, 8)            .side(3)      # 11 Sample data
    push(block)             .side(2) [3]  # 12 Push to RX FIFO, VMA=0 (delay for addr hold)

    jmp("loop")             .side(2)      # 13

    label("do_write")

    word(0xa861)            .side(1)      # 14 (mov pindirs, x side 1)
    out(pins, 32)           .side(1)      # 15 drive address, data, VMA=1, R/W=0
    wait(1, gpio, _E_CLOCK) .side(1)      # 16 Wait for E high (latch)
    wait(0, gpio, _E_CLOCK) .side(1) [6]  # 17 wait for E low (end of cycle), delay for addr hold

    wrap()


# Micropython initialization function for bus cycle PIO state machine
def init_bus_cycle_pio(pio_id, sm_id):
    """Initialize the bus cycle PIO state machine"""
    global bus_cycle_sm
    
    if bus_cycle_sm is not None:
        return

    # Assign all the GPIO to this PIO
    altmode = Pin.ALT_PIO0 if pio_id == 0 else Pin.ALT_PIO1
    for i in range(N_ADDR_DATA_PINS):
        Pin(GPIO_ADDR_BASE + i, Pin.ALT, alt=altmode)
    Pin(GPIO_VMA, Pin.ALT, alt=altmode)
    Pin(GPIO_RW, Pin.ALT, alt=altmode)

    pio = rp2.PIO(pio_id)
    sm = pio.state_machine(sm_id) # Create StateMachine instance

    bus_cycle_sm = sm

    # Initialize with program and configuration
    sm.init(bus_cycle,
            in_base=GPIO_DATA_BASE,      # Data bus input pins (0-7)
            out_base=GPIO_DATA_BASE,     # Data bus output pins (0-7)
            sideset_base=GPIO_VMA,       # Side-set pins for VMA/R/W
            push_thresh=8,               # Push 8 bits at a time
            pull_thresh=0)              # Pull 32 bits at a time (address + data)

    # Enable state machine
    sm.active(1)
    return sm

def bus_read_irq() -> bool:
    return irq_pin.value() == 0

def bus_read_nmi() -> bool:
    return nmi_pin.value() == 0

def bus_read_reset() -> bool:
    return reset_pin.value() == 0

@micropython.native
def bus_write_cycle(address: int, data: int) -> None:
    sm = bus_cycle_sm
    sm.put(_WRITE_PINDIRS)
    sm.put((address << _ADDR_SHIFT) | data)

@micropython.native
def bus_read_cycle(address: int) -> int:
    sm = bus_cycle_sm
    sm.put(_READ_PINDIRS)
    sm.put(address << _ADDR_SHIFT)
    return sm.get()

@micropython.native
def bus_read_block_into(address: int, data: bytearray, length: int):
    sm = bus_cycle_sm
    for i in range(length):
        sm.put(_READ_PINDIRS)
        sm.put((address + i) << _ADDR_SHIFT)
        data[i] = sm.get()
