# Copy to :main.py
from board_config import ECLOCK_PIO, BUS_CYCLE_PIO, BUS_CYCLE_SM

from bus_cycle_pio import (
    init_bus_cycle_pio, bus_read_cycle, bus_write_cycle,
    bus_read_irq, bus_read_nmi, bus_read_reset, bus_read_block_into
)

from clock_pio import (
    init_clock_pio, eclock_stop, eclock_start, eclock_cycles, eclock_wait_cycles,
    eclock_force_low, eclock_reset_pio_counter
)

def bus_read_block(address, length):
    was_started = eclock_start()
    data = bytearray(length)
    bus_read_block_into(address, data, length)
    if not was_started:
        eclock_stop()
    return data

def bus_write_block(address, data):
    was_started = eclock_start()
    for i, byte in enumerate(data):
        bus_write_cycle(address + i, byte)
    if not was_started:
        eclock_stop()

def bus_fill_block(address, length, byte):
    was_started = eclock_start()
    for i in range(length):
        bus_write_cycle(address + i, byte)
    if not was_started:
        eclock_stop()

def block_checksum(address, length) -> int:
    return sum(bus_read_block(address, length)) & 0xFFFF


# Must initialize in this order for pindirs to be correct:
init_bus_cycle_pio(BUS_CYCLE_PIO, BUS_CYCLE_SM)
init_clock_pio(ECLOCK_PIO)
