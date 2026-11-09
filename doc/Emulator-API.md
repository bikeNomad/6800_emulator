<!--
SPDX-License-Identifier: MIT

Copyright 2026 Ned Konz <ned@metamagix.tech>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
-->

# Emulator Control API

All control and inspection of the emulator goes through `src/emu_api.h`. The
USB command shell (`usb_cdc.c`) is one client of it; the planned MicroPython
module will be another. Front ends should not touch `cpu`, the FSM, or the E
clock directly.

## Cores

| Core | Runs | Notes |
|------|------|-------|
| Emulator core (core 0 today) | `run_emulator_sm()`: the state machine in `emulator.c` | Never prints. Everything it runs after start-up is in SRAM. |
| Host core (core 1 today) | Front end: USB CDC shell | Calls `emu_*()` functions. Single-threaded. |

## Communication

Three lock-free single-producer/single-consumer rings (`src/emu_ring.h`):

- **commands** (host → core): run, halt, reset, load, step *n*, pause, resume.
  Each gets exactly one **reply** (core → host) with a sequence number, so a
  command never times out because a state ignored it.
- **events** (core → host): `EMU_EV_HALTED` (with reason: user, breakpoint,
  illegal opcode, HCF, step done, until), `EMU_EV_RESET`, and `EMU_EV_PC_HIT`
  from NOTIFY actions. If the host doesn't drain them, extras are dropped and
  counted (`emu_events_dropped()`); NOTIFY never takes the last 8 free slots,
  so it can't cause that for HALTED/RESET.

## Locking

`emu_lock()` sends *pause*: the emulator core parks between instructions in a
tight SRAM loop until *resume*. The FSM state does not change (no enter/exit
side effects) and, if running, the E clock keeps running; the core catches up
afterwards. Register, memory, breakpoint and status functions lock internally;
the command shell also locks around commands marked `lock_emulator` in its
table. Locks nest. Control commands (`emu_run()` etc.) return
`EMU_ERR_LOCKED` while locked.

Host-side memory accesses save and restore the emulator's pending cycle count,
so inspecting memory doesn't disturb emulated timing.

## PC actions

Breakpoints and GPIO triggers share one table (`src/pc_actions.[ch]`),
checked once per instruction on the emulator core, after interrupts are
taken and before the instruction at PC executes.

Actions (`emu_action_t`, several per address): `BREAK`, `SET`, `CLR`,
`TOGGLE` and `PULSE` on a GPIO pin, `COUNT`, `NOTIFY`. The `emu_bp_*`
functions are BREAK actions. Limits: 64 addresses, 128 actions. Every address
with an action has a 32-bit hit counter (`emu_hits_get/reset`).

Data structures:

| | Size | Use |
|-|------|-----|
| `pc_bitmap` | 8 KiB | one bit per address: anything to do here? |
| `pc_entries` | 64 × 48 B | sorted by address: precomputed hi-bank SIO masks for set/clr/toggle/pulse, pulse widths, flags, hit counter |
| `pc_temp` | 6 B | the pending `next`/`until` stop (address, optional SP); sets its own bitmap bit |

The host keeps the list of actions and rebuilds the entries and bitmap while
the core is parked (`emu_lock()`), so the core never sees a half-built table.
The core writes only hit counters, the notify-armed flag, pulse state,
`pc_temp.active`, and clears a bitmap bit left behind by a finished
temporary stop.

**Cost per instruction** with no action at PC: one bitmap word load and bit
test, plus a test of the "pulse in progress" word. In the built `s_running`
that is about 10 more Cortex-M33 instructions (16 vs 6) than the old
"any breakpoints?" test, roughly 15 cycles ≈ 40 ns at 366 MHz (the old linear
search of up to 16 breakpoints, when any were set, was slower than this), against at least 2.2 µs for the
shortest 6800 instruction (2 E cycles at 894.886 kHz): under 2%. With
actions at PC, `pc_actions_fire()` does a binary search over ≤ 64 entries
(≤ 6 steps) and up to four SIO register writes: on the order of 100-150
cycles (< 0.5 µs).

**GPIO**: trigger pins are GPIO 32-47 only (`pc_pin_allowed()`): 32 and 42-46,
plus 33-36 while the SPI debug output is off (`emu_debug_spi_enable()`
refuses with `EMU_ERR_BUSY` while an action uses them, and vice versa). The
core writes `sio_hw->gpio_hi_set/clr/togl` directly; pin setup (`gpio_init`,
direction) is done on the host core when an action is added.

**Pulses** are timed in real E cycles from the PIO E clock counter
(`eclock_get_pio_cycles()`), so they also work with an external E clock. On
firing, the core sets the pin and records `end = now + width`; at every
instruction boundary (and in the WAI loop) it checks `pc_pulse_active` and,
only if set, reads the counter and clears expired pins. Nothing waits, so
emulation isn't stalled. A pulse ends up to one instruction late. Halt,
reset and load end all pulses (the E clock stops there).

**NOTIFY** posts `EMU_EV_PC_HIT` (pc, prev_pc, hit count). Each address has
an armed flag: the core clears it when it posts, `emu_poll_event()` sets it
again when it pops that event, so at most one is queued per address. It is
also not posted when only 8 slots remain free.

**Resuming**: when the core halts at a BREAK (or the `until` stop) the other
actions there have already fired. The first instruction after `run`/`step`
from a halt skips BREAK and the temporary stop at that PC, and skips the
other actions only if they fired before the halt.

**Step over / run until** (`emu_step_over()`, `emu_run_until()`): the host
sets `pc_temp` under the lock, then runs. For `JSR`/`BSR`/`SWI` the stop is
the next instruction with SP equal to SP before the call. Reaching it halts
with `EMU_HALT_UNTIL`; any halt cancels it.

## SRAM-only rule

While the emulator runs or is parked, the host core may write flash (loading a
ROM, saving the memory map, and later the MicroPython filesystem). Any flash
(XIP) access from the emulator core during an erase or program would fault or
read garbage, so every function the emulator core can reach after start-up is
placed in SRAM with `__time_critical_func()` / `__not_in_flash_func()`, and
helpers it uses per instruction are `__force_inline`.

`tools/check_ram_hotpath.py` walks the call graph from the state handlers in
the built ELF and fails if anything reachable is in flash. It runs after every
`make` (skip with `CHECK_RAM=0`) and on its own with `make check-ram`. Indirect
calls can't be followed, so the state handlers are listed as roots in the
Makefile; add new ones there.

Known exceptions: `s_initializing` (runs once, before the host can write
flash) and `DEBUG_INT_PRINTF` output when built with `DEBUG_INTERRUPTS=1`.

## Tests

`make test-host` builds the real `emulator.c`, `emu_api.c`, `cpu_state.c` and
`simple_fsm.c` for the host against fake hardware (`tests/host/fakes`) with a
toy instruction set, runs the emulator core in a thread, and exercises
run/halt/step/breakpoints/locking/reset/load, and the PC actions (fake SIO
writes logged with the E-cycle count, pulse widths, hit counters, NOTIFY
coalescing and the reserved event slots, step-over including recursion and
SWI, run-until, SPI debug pin exclusion) through the API. Needs only `cc` and
pthreads.
