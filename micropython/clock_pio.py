from machine import Pin, mem32
from micropython import const
import rp2
from board_config import (
    GPIO_E_CLOCK, GPIO_TEST_PIN,
    ECLOCK_SM, SYNC_SM
)
from sm_helpers import (
    set_sm_join_mode,
    sm_rxputget_base_addr
)


# Constants
_PIO_CYCLES_PER_CLOCK = 65
_TARGET_FREQ = 3_579_545 / 4
_PIO_FREQ = _TARGET_FREQ * _PIO_CYCLES_PER_CLOCK
_CYCLES_READ_ADDR = 0
_JOIN_TXPUT = const(8)

# Globals
eclock_sm = None # State machine instance after initialization
sync_sm = None # State machine instance after initialization


# PIO program to drive the E clock pin.
# Also updates the RXFIFO[0] with the inverse of the
# E clock count.
# Requires the FIFO join mode to be set to "TXPUT" (8)
# to allow the SM to write the ISR to rxfifo

@rp2.asm_pio(set_init=(rp2.PIO.OUT_LOW))
def eclock():
    mov(x, invert(null))                  # 0 Initialize X to 0xFFFFFFFF (count down)
    mov(isr, x)                           # 1 Move X to ISR (1 cycle)
    word(0x8018)                          # 2 (mov rxfifo[0], isr)
                                          #    Write ISR directly to rxfifo[0] (1 cycle)
    wrap_target()
    label("loop")
    set(pins, 1)                     [30] # 3  E clock high, delay 31 cycles
    mov(isr, x)                           # 4  Move X to ISR (1 cycle)
    set(pins, 0)                     [30] # 5  E clock low, delay 31 cycles
    word(0x8018)                          # 6  (mov rxfifo[0], isr)
                                          #     Write ISR directly to rxfifo[0] (1 cycle)
    jmp(x_dec, "loop")                    # 7  Decrement X each cycle and loop
    wrap()

# Program to wait a given number of E clock cycles
# (falling edges)
# single IN pin is E clock
# single SET pin is TEST pin for debugging
# Usage: push (number-1) to txfifo[0]
#        get from rxfifo[0] when done

@rp2.asm_pio(set_init=(rp2.PIO.OUT_LOW))
def sync():
    wrap_target()
    set(pins, 0)                          # 0 test pin low
    pull(block)                           # 1 get count
    set(pins, 1)                          # 2 test pin high
    mov(x, osr)                           # 3 X has count

    label("next_edge")
    wait(1, pin, 0)                       # 4 wait for E clock HIGH
    wait(0, pin, 0)                       # 5 wait for E clock LOW
    jmp(x_dec, "next_edge")               # 6 another edge if X>0

    push(block)                           # 7 signal done
    wrap()

# Micropython initialization functions for PIO state machines
def _init_eclock_pio(pio_id, sm_id):
    """Initialize the eclock PIO state machine"""
    # Create StateMachine instance
    pio = rp2.PIO(pio_id)
    sm = pio.state_machine(sm_id) # Create StateMachine instance

    # Initialize with program and configuration
    sm.init(eclock,
            freq=int(_PIO_FREQ),   # For target E clock frequency: 0.894886 MHz
            set_base=GPIO_E_CLOCK)     # E clock output pin

    set_sm_join_mode(pio_id, sm_id, _JOIN_TXPUT)
    sm.restart()

    global _CYCLES_READ_ADDR
    _CYCLES_READ_ADDR = sm_rxputget_base_addr(pio_id, sm_id) + 0

    # Stop immediately (will be started later)
    sm.active(0)
    return sm

def _init_sync_pio(pio_id, sm_id):
    """Initialize the sync PIO state machine"""
    # Create StateMachine instance
    pio = rp2.PIO(pio_id)
    sm = pio.state_machine(sm_id) # Create StateMachine instance

    # Initialize with program and configuration
    sm.init(sync,
            in_base=GPIO_E_CLOCK,    # E clock input pin
            set_base=GPIO_TEST_PIN)    # Test pin output

    # Enable immediately
    sm.active(1)
    return sm

def eclock_force_low():
    if eclock_sm is None:
        return
    eclock_sm.exec("set(pins, 0)")

def eclock_reset_pio_counter():
    if eclock_sm is None:
        return
    eclock_sm.exec("mov(x, invert(null))")

def eclock_stop():
    """Stop the eclock PIO state machine"""
    if eclock_sm is None:
        return
    if eclock_sm.active():
        eclock_sm.active(0)
        eclock_force_low()

def eclock_start():
    """Stop the eclock PIO state machine"""
    if eclock_sm is None:
        return
    if not eclock_sm.active():
        eclock_sm.restart()
        eclock_sm.active(1)

def eclock_wait_cycles(n: int):
    """Wait for a given number of E clock cycles"""
    if sync_sm is not None:
        sync_sm.put(n - 1)
        sync_sm.get()

def eclock_cycles() -> int:
    """Return the number of E clock cycles since restarting the clock"""
    if eclock_sm is None:
        return 0
    return ~mem32[_CYCLES_READ_ADDR]

def init_clock_pio(pio_id):
    """Initialize the clock PIO state machine"""
    global eclock_sm, sync_sm
    if eclock_sm is not None or sync_sm is not None:
        return

    altmode = Pin.ALT_PIO0 if pio_id == 0 else Pin.ALT_PIO1
    Pin(GPIO_E_CLOCK, Pin.ALT, alt=altmode)
    Pin(GPIO_TEST_PIN, Pin.ALT, alt=altmode)

    eclock_sm = _init_eclock_pio(pio_id, ECLOCK_SM)
    sync_sm = _init_sync_pio(pio_id, SYNC_SM)
