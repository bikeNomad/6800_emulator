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

# Board configuration constants (from board_config.h)
# Ned's System 7 Board configuration
GPIO_DATA_BASE = 0  # GPIO0..7 are data lines D0..D7
GPIO_ADDR_BASE = 8  # GPIO8..23 are address lines A0..A15
GPIO_E_CLOCK = 24
GPIO_VMA = 25
GPIO_RW = 26
GPIO_IRQ = 27
GPIO_NMI = 28
GPIO_RESET = 29
GPIO_TEST_PIN = 30
GPIO_ECLOCK_IN = 31  # E clock input for external clock (SPARE_IN)

ADDR_LINES = 16
N_ADDR_DATA_PINS = 8 + ADDR_LINES  # 8 data pins + 16 address pins

# PIO config
ECLOCK_PIO = 0  # Eclock and Sync
ECLOCK_SM = 0
SYNC_PIO = 0
SYNC_SM = 1
BUS_CYCLE_PIO = 1
BUS_CYCLE_SM = 0
