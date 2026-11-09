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

// Per-address PC actions. See pc_actions.h.

#include "pc_actions.h"
#include "board_config.h"
#include "clock.h"
#include "cpu_state.h"
#include "emulator.h"
#include <string.h>

// Pins that may be used as trigger outputs. On the board (REV2 schematic)
// GPIO 32 and 42-46 are not connected to anything; GPIO 33-36 are the SPI
// debug pins on J3 and are only usable while the SPI debug output is off.
static const uint8_t trigger_pins[] = {32, 33, 34, 35, 36, 42, 43, 44, 45, 46};

_Static_assert(GPIO_SPI_CS == 33 && GPIO_SPI_SCK == 34 && GPIO_SPI_MOSI == 35 &&
		       GPIO_SPI_MISO == 36,
	       "trigger_pins[] assumes the SPI debug pins are GPIO 33-36");
_Static_assert(GPIO_PSRAM_CS == 47, "GPIO 47 is the PSRAM chip select");

// ---------------------------------------------------------------------------
// Shared state (SRAM, .bss)

uint32_t pc_bitmap[65536 / 32];
pc_entry_t pc_entries[PC_MAX_ADDRS];
uint32_t pc_entry_count;
pc_temp_t pc_temp;
uint32_t pc_pulse_active;
static uint32_t pulse_end[PC_PIN_COUNT]; // PIO E-cycle count at which each pulse ends

static __force_inline void bitmap_set(uint16_t a)
{
	pc_bitmap[a >> 5] |= 1u << (a & 31u);
}

static __force_inline void bitmap_clear(uint16_t a)
{
	pc_bitmap[a >> 5] &= ~(1u << (a & 31u));
}

// Binary search of the sorted entry table. Used on both cores.
static __force_inline pc_entry_t *find_entry(uint16_t addr)
{
	uint32_t lo = 0, hi = pc_entry_count;
	while (lo < hi) {
		uint32_t mid = (lo + hi) >> 1;
		uint16_t a = pc_entries[mid].addr;
		if (a == addr) {
			return &pc_entries[mid];
		}
		if (a < addr) {
			lo = mid + 1;
		} else {
			hi = mid;
		}
	}
	return NULL;
}

// ---------------------------------------------------------------------------
// Emulator core (SRAM only)

static __force_inline void start_pulses(const pc_entry_t *e)
{
	uint32_t now = eclock_get_pio_cycles();
	uint32_t m = e->pulse_mask;
	pc_gpio_hi_set(m);
	while (m) {
		uint32_t i = (uint32_t)__builtin_ctz(m);
		m &= m - 1;
		pulse_end[i] = now + e->pulse_width[i];
	}
	pc_pulse_active |= e->pulse_mask;
}

static __force_inline void post_notify(pc_entry_t *e, uint16_t pc)
{
	if (!e->notify_armed ||
	    emu_event_ring_count(&emu_event_q) >= EMU_EVENT_RING_SIZE - PC_NOTIFY_RESERVE) {
		return; // one outstanding per address; hits still counted
	}
	// Disarm before posting: the host re-arms when it pops the event, which
	// it can't do before the push makes the event visible.
	e->notify_armed = 0;
	emu_event_t ev = {
		.type = EMU_EV_PC_HIT,
		.pc = pc,
		.prev_pc = cpu.last_opcode_address,
		.hits = e->hits,
	};
	if (!emu_event_ring_push(&emu_event_q, &ev)) {
		e->notify_armed = 1;
	}
}

pc_hit_t __time_critical_func(pc_actions_fire)(uint16_t pc, bool skip_actions, bool skip_stops)
{
	pc_hit_t r = PC_HIT_NONE;
	pc_entry_t *e = find_entry(pc);

	if (e) {
		if (!skip_actions) {
			e->hits++;
			if (e->clr_mask) {
				pc_gpio_hi_clr(e->clr_mask);
			}
			if (e->set_mask) {
				pc_gpio_hi_set(e->set_mask);
			}
			if (e->tog_mask) {
				pc_gpio_hi_togl(e->tog_mask);
			}
			if (e->pulse_mask) {
				start_pulses(e);
			}
			if (e->flags & PCF_NOTIFY) {
				post_notify(e, pc);
			}
		}
		if (!skip_stops && (e->flags & PCF_BREAK)) {
			r = PC_HIT_BREAK;
		}
	}

	bool temp_here = pc_temp.active && pc_temp.addr == pc;
	if (temp_here && !skip_stops && r == PC_HIT_NONE &&
	    (!pc_temp.match_sp || cpu.sp == pc_temp.sp)) {
		r = PC_HIT_UNTIL;
	}
	if (!e && !temp_here) {
		bitmap_clear(pc); // left behind by a finished temporary stop
	}
	return r;
}

void __time_critical_func(pc_pulse_expire)(void)
{
	uint32_t now = eclock_get_pio_cycles();
	uint32_t m = pc_pulse_active;
	uint32_t done = 0;
	while (m) {
		uint32_t i = (uint32_t)__builtin_ctz(m);
		m &= m - 1;
		if ((int32_t)(now - pulse_end[i]) >= 0) {
			done |= 1u << i;
		}
	}
	if (done) {
		pc_gpio_hi_clr(done);
		pc_pulse_active &= ~done;
	}
}

void __time_critical_func(pc_pulse_end_all)(void)
{
	if (pc_pulse_active) {
		pc_gpio_hi_clr(pc_pulse_active);
		pc_pulse_active = 0;
	}
}

// ---------------------------------------------------------------------------
// Host core

static emu_action_t host_actions[PC_MAX_ACTIONS]; // sorted by addr, then type, pin
static uint32_t host_count;
static uint8_t pin_refs[PC_PIN_COUNT];

void pc_actions_init(void)
{
	memset(pc_bitmap, 0, sizeof(pc_bitmap));
	memset(pc_entries, 0, sizeof(pc_entries));
	pc_entry_count = 0;
	memset(&pc_temp, 0, sizeof(pc_temp));
	pc_pulse_active = 0;
	host_count = 0;
	memset(pin_refs, 0, sizeof(pin_refs));
}

bool pc_pin_allowed(uint8_t pin)
{
	for (size_t i = 0; i < sizeof(trigger_pins); i++) {
		if (trigger_pins[i] == pin) {
			return true;
		}
	}
	return false;
}

bool pc_pin_is_spi_debug(uint8_t pin)
{
	return pin == GPIO_SPI_CS || pin == GPIO_SPI_SCK || pin == GPIO_SPI_MOSI ||
	       pin == GPIO_SPI_MISO;
}

size_t pc_trigger_pins(uint8_t *out, size_t max)
{
	for (size_t i = 0; i < sizeof(trigger_pins) && i < max; i++) {
		out[i] = trigger_pins[i];
	}
	return sizeof(trigger_pins);
}

bool pc_spi_debug_pins_claimed(void)
{
	for (uint8_t pin = GPIO_SPI_CS; pin <= GPIO_SPI_MISO; pin++) {
		if (pin_refs[pin - PC_PIN_BASE]) {
			return true;
		}
	}
	return false;
}

static bool is_gpio_type(uint8_t type)
{
	return type == EMU_ACT_SET || type == EMU_ACT_CLR || type == EMU_ACT_TOGGLE ||
	       type == EMU_ACT_PULSE;
}

static int compare_actions(const emu_action_t *a, const emu_action_t *b)
{
	if (a->addr != b->addr) {
		return a->addr < b->addr ? -1 : 1;
	}
	if (a->type != b->type) {
		return a->type < b->type ? -1 : 1;
	}
	if (a->pin != b->pin) {
		return a->pin < b->pin ? -1 : 1;
	}
	return 0;
}

static size_t distinct_addrs(void)
{
	size_t n = 0;
	for (uint32_t i = 0; i < host_count; i++) {
		if (i == 0 || host_actions[i].addr != host_actions[i - 1].addr) {
			n++;
		}
	}
	return n;
}

// Rebuild pc_entries[] and the bitmap from host_actions[]. Keeps the hit
// counters and notify state of addresses that remain.
static void rebuild(void)
{
	static pc_entry_t build[PC_MAX_ADDRS];
	uint32_t n = 0;

	memset(build, 0, sizeof(build));
	for (uint32_t i = 0; i < host_count; i++) {
		const emu_action_t *a = &host_actions[i];
		if (n == 0 || build[n - 1].addr != a->addr) {
			pc_entry_t *e = &build[n++];
			e->addr = a->addr;
			const pc_entry_t *old = find_entry(a->addr);
			if (old) {
				e->hits = old->hits;
				e->notify_armed = (old->flags & PCF_NOTIFY) ? old->notify_armed : 1;
			} else {
				e->notify_armed = 1;
			}
		}
		pc_entry_t *e = &build[n - 1];
		uint16_t bit = is_gpio_type(a->type) ? (uint16_t)(1u << (a->pin - PC_PIN_BASE)) : 0;
		switch ((emu_action_type_t)a->type) {
		case EMU_ACT_BREAK:
			e->flags |= PCF_BREAK;
			break;
		case EMU_ACT_SET:
			e->set_mask |= bit;
			break;
		case EMU_ACT_CLR:
			e->clr_mask |= bit;
			break;
		case EMU_ACT_TOGGLE:
			e->tog_mask |= bit;
			break;
		case EMU_ACT_PULSE:
			e->pulse_mask |= bit;
			e->pulse_width[a->pin - PC_PIN_BASE] = a->width;
			break;
		case EMU_ACT_COUNT:
			e->flags |= PCF_COUNT;
			break;
		case EMU_ACT_NOTIFY:
			e->flags |= PCF_NOTIFY;
			break;
		}
	}

	for (uint32_t i = 0; i < pc_entry_count; i++) {
		bitmap_clear(pc_entries[i].addr);
	}
	memcpy(pc_entries, build, sizeof(build));
	pc_entry_count = n;
	for (uint32_t i = 0; i < n; i++) {
		bitmap_set(pc_entries[i].addr);
	}
	if (pc_temp.active) {
		bitmap_set(pc_temp.addr);
	}
}

static void pin_ref(uint8_t pin)
{
	if (pin_refs[pin - PC_PIN_BASE]++ == 0) {
		pc_gpio_config_output(pin);
	}
}

static void pin_unref(uint8_t pin)
{
	if (--pin_refs[pin - PC_PIN_BASE] == 0) {
		// A pulse in progress on this pin is cut short
		pc_pulse_active &= ~(1u << (pin - PC_PIN_BASE));
		pc_gpio_release(pin);
	}
}

emu_result_t pc_actions_host_add(const emu_action_t *in)
{
	emu_action_t a = *in;
	if (a.type > EMU_ACT_NOTIFY) {
		return EMU_ERR_ARG;
	}
	bool gpio = is_gpio_type(a.type);
	if (gpio) {
		if (!pc_pin_allowed(a.pin)) {
			return EMU_ERR_RANGE;
		}
		if (a.type == EMU_ACT_PULSE) {
			if (a.width == 0) {
				a.width = 1;
			}
		} else {
			a.width = 0;
		}
	} else {
		a.pin = 0;
		a.width = 0;
	}

	// One GPIO action per pin per address; one of each other type.
	bool addr_known = false;
	for (uint32_t i = 0; i < host_count; i++) {
		const emu_action_t *b = &host_actions[i];
		if (b->addr != a.addr) {
			continue;
		}
		addr_known = true;
		if (gpio ? (is_gpio_type(b->type) && b->pin == a.pin) : (b->type == a.type)) {
			return EMU_ERR_EXISTS;
		}
	}
	if (host_count >= PC_MAX_ACTIONS || (!addr_known && distinct_addrs() >= PC_MAX_ADDRS)) {
		return EMU_ERR_FULL;
	}

	// Insert in order
	uint32_t pos = host_count;
	while (pos > 0 && compare_actions(&host_actions[pos - 1], &a) > 0) {
		host_actions[pos] = host_actions[pos - 1];
		pos--;
	}
	host_actions[pos] = a;
	host_count++;
	if (gpio) {
		pin_ref(a.pin);
	}
	rebuild();
	return EMU_OK;
}

static void remove_at(uint32_t i)
{
	uint8_t type = host_actions[i].type;
	uint8_t pin = host_actions[i].pin;
	memmove(&host_actions[i], &host_actions[i + 1],
		(host_count - i - 1) * sizeof(host_actions[0]));
	host_count--;
	if (is_gpio_type(type)) {
		pin_unref(pin);
	}
}

emu_result_t pc_actions_host_remove(const emu_action_t *a)
{
	bool gpio = is_gpio_type(a->type);
	for (uint32_t i = 0; i < host_count; i++) {
		const emu_action_t *b = &host_actions[i];
		if (b->addr == a->addr && b->type == a->type && (!gpio || b->pin == a->pin)) {
			remove_at(i);
			rebuild();
			return EMU_OK;
		}
	}
	return EMU_ERR_NOT_FOUND;
}

emu_result_t pc_actions_host_clear(int32_t addr, bool include_break, bool only_break)
{
	bool removed = false;
	for (uint32_t i = 0; i < host_count;) {
		const emu_action_t *b = &host_actions[i];
		bool is_break = (b->type == EMU_ACT_BREAK);
		bool match = (addr < 0 || b->addr == (uint16_t)addr) &&
			     (only_break ? is_break : (include_break || !is_break));
		if (match) {
			remove_at(i);
			removed = true;
		} else {
			i++;
		}
	}
	if (!removed) {
		return EMU_ERR_NOT_FOUND;
	}
	rebuild();
	return EMU_OK;
}

size_t pc_actions_host_list(emu_action_t *out, size_t max)
{
	for (uint32_t i = 0; i < host_count && i < max; i++) {
		out[i] = host_actions[i];
	}
	return host_count;
}

size_t pc_actions_host_count(uint8_t type)
{
	size_t n = 0;
	for (uint32_t i = 0; i < host_count; i++) {
		n += (host_actions[i].type == type);
	}
	return n;
}

const pc_entry_t *pc_actions_host_find(uint16_t addr)
{
	return find_entry(addr);
}

emu_result_t pc_actions_host_hits_reset(int32_t addr)
{
	if (addr < 0) {
		for (uint32_t i = 0; i < pc_entry_count; i++) {
			pc_entries[i].hits = 0;
		}
		return EMU_OK;
	}
	pc_entry_t *e = find_entry((uint16_t)addr);
	if (!e) {
		return EMU_ERR_NOT_FOUND;
	}
	e->hits = 0;
	return EMU_OK;
}

void pc_actions_host_rearm(uint16_t addr)
{
	pc_entry_t *e = find_entry(addr);
	if (e && (e->flags & PCF_NOTIFY)) {
		e->notify_armed = 1;
	}
}

void pc_temp_set(uint16_t addr, bool match_sp, uint16_t sp)
{
	pc_temp.addr = addr;
	pc_temp.match_sp = match_sp;
	pc_temp.sp = sp;
	pc_temp.active = true;
	bitmap_set(addr);
}

void pc_temp_cancel(void)
{
	pc_temp.active = false;
	// The bitmap bit, if no entry needs it, is cleared the next time the
	// emulator core reaches the address.
}
