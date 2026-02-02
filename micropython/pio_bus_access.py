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

from board_config import ECLOCK_PIO, BUS_CYCLE_PIO, BUS_CYCLE_SM

from bus_cycle_pio import (
    init_bus_cycle_pio,
    bus_read_byte,
    bus_write_byte,
    bus_read_irq,
    bus_read_nmi,
    bus_read_reset,
    bus_read_block_into,
)

from clock_pio import (
    init_clock_pio,
    eclock_stop,
    eclock_start,
    eclock_cycles,
    eclock_wait_cycles,
    eclock_force_low,
    eclock_reset_pio_counter,
    eclock_set_mode,
    ECLOCK_INTERNAL,
    ECLOCK_EXTERNAL,
    eclock_auto_detect,
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
        bus_write_byte(address + i, byte)
    if not was_started:
        eclock_stop()


def bus_fill_block(address, length, byte):
    was_started = eclock_start()
    for i in range(length):
        bus_write_byte(address + i, byte)
    if not was_started:
        eclock_stop()


def block_checksum(address, length) -> int:
    return sum(bus_read_block(address, length)) & 0xFFFF


# Must initialize in this order for pindirs to be correct:
init_bus_cycle_pio(BUS_CYCLE_PIO, BUS_CYCLE_SM)
init_clock_pio(ECLOCK_PIO)

if eclock_auto_detect():
    eclock_set_mode(ECLOCK_EXTERNAL)
else:
    eclock_set_mode(ECLOCK_INTERNAL)
