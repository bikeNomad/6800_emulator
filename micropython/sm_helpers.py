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

# Pico SDK-style PIO SM register access in MicroPython
# for RP2350 only
from machine import mem32

# (0,0) => 0x5020_00C8
def _sm_regs_base(pio_id, sm_id):
    pio_base = 0x5020_0000 + (0x0010_0000 * pio_id)
    sm_offset = 0xC8 + (0xE0-0xC8) * sm_id
    return pio_base + sm_offset

def sm_execctrl_addr(pio_id, sm_id):
    return _sm_regs_base(pio_id, sm_id) + 0x04

def sm_shiftctrl_addr(pio_id, sm_id):
    return _sm_regs_base(pio_id, sm_id) + 0x08

# return the RXF0 address for this SM
def sm_rxfifo_base_addr(pio_id, sm_id):
    return (_sm_regs_base(pio_id, sm_id) + 0x60) + (0x10 * sm_id)

def set_sm_join_mode(pio_id, sm_id, join_mode):
    addr = sm_shiftctrl_addr(pio_id, sm_id)
    shiftctrl = mem32[addr]
    jm_mask = (3 << 30) | (3 << 14)
    jm_bits = ((join_mode & 3) << 30) | ((join_mode >> 2) << 14)
    mem32[addr] = (shiftctrl & ~jm_mask) | jm_bits

# (0,0) => 0x5020_0128
# (0,1) => 0x5020_0138
def sm_rxputget_base_addr(pio_id, sm_id):
    return _sm_regs_base(pio_id, sm_id) + 0x60 + (0x10 * sm_id)
