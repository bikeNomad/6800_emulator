from machine import Pin, mem32
from micropython import const
import rp2
import time
from board_config import (
    GPIO_E_CLOCK, GPIO_ECLOCK_IN, GPIO_TEST_PIN,
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

# E clock modes
ECLOCK_INTERNAL = const(0)
ECLOCK_EXTERNAL = const(1)

# Globals
eclock_sm = None # State machine instance after initialization
sync_sm = None # State machine instance after initialization
_current_mode = ECLOCK_INTERNAL  # Current E clock mode
_pio_id = None  # PIO instance ID for reinit


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


# Program to count external E clock cycles (falling edges)
# IN pin is external E clock input (GPIO_ECLOCK_IN)
# SET pin is E clock output (GPIO_ECLOCK) - mirrors input for bus_cycle PIO
# Provides cycle count via rxfifo[0] (same interface as eclock program)
# Requires the FIFO join mode to be set to "TXPUT" (8)
# to allow the SM to write the ISR to rxfifo

@rp2.asm_pio(set_init=(rp2.PIO.OUT_LOW))
def eclock_external():
    mov(x, invert(null))                  # 0 Initialize X to 0xFFFFFFFF (count down)
    mov(isr, x)                           # 1 Move X to ISR
    word(0x8018)                          # 2 (mov rxfifo[0], isr)
                                          #    Initialize counter in rxfifo[0]
    wrap_target()
    wait(1, pin, 0)                       # 3 Wait for E clock input HIGH
    set(pins, 1)                          # 4 Mirror HIGH to output
    wait(0, pin, 0)                       # 5 Wait for E clock input LOW (falling edge)
    set(pins, 0)                          # 6 Mirror LOW to output
    jmp(x_dec, "continue")                # 7 Decrement X
    label("continue")
    mov(isr, x)                           # 8 Move X to ISR
    word(0x8018)                          # 9 (mov rxfifo[0], isr) Update cycle count
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

def _init_eclock_external_pio(pio_id, sm_id):
    """Initialize the eclock_external PIO state machine for external clock input"""
    # Create StateMachine instance
    pio = rp2.PIO(pio_id)
    sm = pio.state_machine(sm_id)

    # Configure input pin for reading external clock
    Pin(GPIO_ECLOCK_IN, Pin.IN)

    # Initialize with program and configuration
    # SET pin is GPIO_ECLOCK (output, mirrors external clock for bus_cycle PIO)
    # IN pin is GPIO_ECLOCK_IN (input for wait instructions)
    sm.init(eclock_external,
            in_base=GPIO_ECLOCK_IN,    # External clock input pin
            set_base=GPIO_E_CLOCK)      # E clock output pin (mirrors input)

    set_sm_join_mode(pio_id, sm_id, _JOIN_TXPUT)
    sm.restart()

    global _CYCLES_READ_ADDR
    _CYCLES_READ_ADDR = sm_rxputget_base_addr(pio_id, sm_id) + 0

    # Stop immediately (will be started later)
    sm.active(0)
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
    """Stop the eclock PIO state machine.
    Return True if it was started previously."""
    if eclock_sm is None:
        return False
    if eclock_sm.active():
        eclock_sm.active(0)
        eclock_force_low()
        return True
    return False

def eclock_start():
    """Start the eclock PIO state machine.
    Return True if it was already started, else False."""
    if eclock_sm is None:
        return False
    if not eclock_sm.active():
        eclock_sm.restart()
        eclock_sm.active(1)
        return False
    return True

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
    global eclock_sm, sync_sm, _pio_id, _current_mode
    if eclock_sm is not None or sync_sm is not None:
        return

    _pio_id = pio_id
    _current_mode = ECLOCK_INTERNAL

    altmode = Pin.ALT_PIO0 if pio_id == 0 else Pin.ALT_PIO1 if pio_id == 1 else Pin.ALT_PIO2
    Pin(GPIO_E_CLOCK, Pin.ALT, alt=altmode)
    Pin(GPIO_TEST_PIN, Pin.ALT, alt=altmode)

    eclock_sm = _init_eclock_pio(pio_id, ECLOCK_SM)
    sync_sm = _init_sync_pio(pio_id, SYNC_SM)

def eclock_get_mode() -> int:
    """Get current E clock mode (ECLOCK_INTERNAL or ECLOCK_EXTERNAL)"""
    return _current_mode

def eclock_set_mode(mode: int):
    """Set E clock mode (internal or external).
    Must be called when E clock is stopped."""
    global eclock_sm, _current_mode

    if eclock_sm is None or _pio_id is None:
        return

    if eclock_sm.active():
        print("Error: Cannot change E clock mode while running")
        return

    if mode == _current_mode:
        return  # Already in requested mode

    _current_mode = mode

    altmode = Pin.ALT_PIO0 if _pio_id == 0 else Pin.ALT_PIO1 if _pio_id == 1 else Pin.ALT_PIO2
    Pin(GPIO_E_CLOCK, Pin.ALT, alt=altmode)

    if mode == ECLOCK_INTERNAL:
        # Reinitialize for internal clock generation (output)
        eclock_sm = _init_eclock_pio(_pio_id, ECLOCK_SM)
    else:
        # Initialize for external clock counting (input on GPIO_ECLOCK_IN)
        # Mirrors clock to GPIO_ECLOCK for bus_cycle PIO
        eclock_sm = _init_eclock_external_pio(_pio_id, ECLOCK_SM)

    # Reset the cycle counter
    eclock_reset_pio_counter()

def eclock_auto_detect() -> bool:
    """Auto-detect external E clock at startup.
    Returns True if external clock detected, False if using internal."""
    global eclock_sm, _current_mode

    if eclock_sm is None:
        return False

    # Detection parameters
    detection_window_us = 100  # 100µs window
    min_cycles_threshold = 10  # Minimum cycles to consider valid

    # Ensure we start stopped
    if eclock_sm.active():
        eclock_stop()

    # Configure for external clock detection
    eclock_set_mode(ECLOCK_EXTERNAL)
    eclock_reset_pio_counter()

    # Start counting external clock edges
    eclock_sm.active(1)

    # Wait for detection window
    time.sleep_us(detection_window_us)

    # Read cycle count and stop
    cycles = eclock_cycles()
    eclock_sm.active(0)

    print(f"External E clock detection: ({cycles} cycles in {detection_window_us}µs)")

    # Decide based on detected cycles
    if cycles >= min_cycles_threshold:
        # External clock detected - stay in external mode
        print("External E clock detected")
        eclock_reset_pio_counter()
        return True
    else:
        # No external clock - switch to internal
        print("No external E clock detected, using internal")
        eclock_set_mode(ECLOCK_INTERNAL)
        return False
