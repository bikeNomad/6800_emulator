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
  illegal opcode, HCF, step done) and `EMU_EV_RESET`. If the host doesn't drain
  them, extras are dropped and counted (`emu_events_dropped()`).

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
run/halt/step/breakpoints/locking/reset/load through the API. Needs only `cc`
and pthreads.
