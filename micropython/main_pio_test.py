# Copy to :main.py
from board_config import ECLOCK_PIO, BUS_CYCLE_PIO, BUS_CYCLE_SM

from bus_cycle_pio import (
    init_bus_cycle_pio, bus_read_cycle, bus_write_cycle,
    bus_read_irq, bus_read_nmi, bus_read_reset
)

from clock_pio import (
    init_clock_pio, eclock_stop, eclock_start, eclock_cycles, eclock_wait_cycles,
    eclock_force_low, eclock_reset_pio_counter
)

from machine import mem32

# Must initialize in this order for pindirs to be correct:
init_bus_cycle_pio(BUS_CYCLE_PIO, BUS_CYCLE_SM)
init_clock_pio(ECLOCK_PIO)
