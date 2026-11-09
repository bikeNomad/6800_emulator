/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * Per-address PC actions: breakpoints, GPIO triggers, hit counters, notify
 * events, and the temporary stop used by step-over / run-until.
 *
 * Front ends use emu_api.h; this header is shared by emu_api.c (host core)
 * and emulator.c (emulator core).
 *
 * Data layout
 * -----------
 *   pc_bitmap[]  8 KiB, one bit per address: "something to do here".
 *   pc_entries[] up to PC_MAX_ADDRS records sorted by address, one per address
 *                that has actions. Each holds precomputed GPIO masks (for
 *                GPIO 32-47, the SIO "hi" bank), pulse widths, flags and a
 *                hit counter.
 *   pc_temp      one temporary stop (address, optionally SP) for step-over
 *                and run-until. It has its own bitmap bit.
 *
 * The host core builds the entries from its own list of actions
 * (pc_actions_host_*) while the emulator core is parked (emu_lock()). The
 * emulator core only reads the table, except for: hit counters (increments),
 * notify_armed (clears), pc_temp.active (clears), the pulse state, and a
 * stale bitmap bit left by a finished temporary stop (clears).
 *
 * Per-instruction cost on the emulator core, with no action at PC: one
 * bitmap test (pc_actions_at()) and one test of pc_pulse_active
 * (pc_pulse_service()).
 */

#pragma once

#include "emu_api.h"
#include "pc_gpio.h"
#include "pico.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define PC_MAX_ADDRS   64  // addresses with at least one action
#define PC_MAX_ACTIONS 128 // actions in total

// Trigger pins are GPIO 32..47 only, so one 16-bit "hi bank" mask covers them.
#define PC_PIN_BASE  32
#define PC_PIN_COUNT 16

// NOTIFY events always leave at least this many slots free in the event
// ring, so HALTED and RESET events have room.
#define PC_NOTIFY_RESERVE 8

enum {
	PCF_BREAK = 1u << 0,
	PCF_NOTIFY = 1u << 1,
	PCF_COUNT = 1u << 2,
};

typedef struct pc_entry_t {
	uint16_t addr;
	uint8_t flags;                 // PCF_*
	volatile uint8_t notify_armed; // host sets, emulator core clears
	uint16_t set_mask;             // hi-bank masks (bit n = GPIO 32+n)
	uint16_t clr_mask;
	uint16_t tog_mask;
	uint16_t pulse_mask;
	volatile uint32_t hits;
	uint16_t pulse_width[PC_PIN_COUNT]; // E cycles, for bits in pulse_mask
} pc_entry_t;

typedef struct pc_temp_t {
	volatile bool active;
	bool match_sp;
	uint16_t addr;
	uint16_t sp;
} pc_temp_t;

extern uint32_t pc_bitmap[65536 / 32];
extern pc_entry_t pc_entries[PC_MAX_ADDRS];
extern uint32_t pc_entry_count;
extern pc_temp_t pc_temp;
extern uint32_t pc_pulse_active; // hi-bank mask of pins with a pulse in progress

// Result of firing the actions at an address
typedef enum pc_hit_t {
	PC_HIT_NONE = 0, // keep running
	PC_HIT_BREAK,    // halt: breakpoint
	PC_HIT_UNTIL,    // halt: temporary stop reached
} pc_hit_t;

// ---------------------------------------------------------------------------
// Emulator core

// True if there may be something to do at pc. O(1): one word load and a test.
static __force_inline bool pc_actions_at(uint16_t pc)
{
	return (pc_bitmap[pc >> 5] >> (pc & 31u)) & 1u;
}

// Fire the actions at pc (only call when pc_actions_at(pc)).
//   skip_actions: the GPIO/count/notify actions at pc already fired (we
//                 halted on a breakpoint here), don't repeat them
//   skip_stops:   ignore BREAK and the temporary stop at pc (resuming from a
//                 halt at pc)
pc_hit_t pc_actions_fire(uint16_t pc, bool skip_actions, bool skip_stops);

// End expired pulses. Reads the PIO E-cycle counter only if a pulse is active.
void pc_pulse_expire(void);
static __force_inline void pc_pulse_service(void)
{
	if (pc_pulse_active) {
		pc_pulse_expire();
	}
}

// End all pulses now (E clock stopping: halt, reset, load).
void pc_pulse_end_all(void);

// ---------------------------------------------------------------------------
// Host core. Functions that change the table must be called with the
// emulator core parked (emu_lock()).

void pc_actions_init(void);

// Pin usable as a trigger output. Pins shared with the SPI debug output are
// only usable while it is off (see emu_debug_spi_enable()).
bool pc_pin_allowed(uint8_t pin);
bool pc_pin_is_spi_debug(uint8_t pin);
size_t pc_trigger_pins(uint8_t *out, size_t max);
// True if any action uses one of the SPI debug pins.
bool pc_spi_debug_pins_claimed(void);

emu_result_t pc_actions_host_add(const emu_action_t *a);    // locked
emu_result_t pc_actions_host_remove(const emu_action_t *a); // locked
// addr < 0: all addresses. include_break: also remove BREAK actions.
// Returns EMU_ERR_NOT_FOUND if nothing was removed.
emu_result_t pc_actions_host_clear(int32_t addr, bool include_break, bool only_break); // locked
size_t pc_actions_host_list(emu_action_t *out, size_t max);
size_t pc_actions_host_count(uint8_t type); // number of actions of a type

const pc_entry_t *pc_actions_host_find(uint16_t addr);
emu_result_t pc_actions_host_hits_reset(int32_t addr); // locked

// Re-arm NOTIFY for addr after its event was consumed.
void pc_actions_host_rearm(uint16_t addr);

// Temporary stop (locked)
void pc_temp_set(uint16_t addr, bool match_sp, uint16_t sp);
void pc_temp_cancel(void);
