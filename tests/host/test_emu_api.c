// SPDX-License-Identifier: MIT
//
// Host-side test of the emulator control API (emu_api.c) and the emulator
// core state machine (emulator.c). The emulator core runs in its own thread,
// as it runs on its own core on the RP2350; hardware is faked by
// fake_platform.c with a toy instruction set.
//
// Build and run with `make` in this directory.

#include "clock.h"
#include "cpu_state.h"
#include "emu_api.h"
#include "emulator.h"
#include "interrupts.h"
#include "memory.h"
#include "pc_actions.h"
#include "pc_gpio.h"
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

static int failures;

#define CHECK(cond)                                                                                \
	do {                                                                                       \
		if (!(cond)) {                                                                     \
			printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                   \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

#define CHECK_OK(expr)                                                                             \
	do {                                                                                       \
		emu_result_t r_ = (expr);                                                          \
		if (r_ != EMU_OK) {                                                                \
			printf("  FAIL %s:%d: %s -> %s\n", __FILE__, __LINE__, #expr,              \
			       emu_result_str(r_));                                                \
			failures++;                                                                \
		}                                                                                  \
	} while (0)

static void *core_thread(void *arg)
{
	(void)arg;
	run_emulator_sm();
	return NULL;
}

static bool wait_state(emu_state_t s, int ms)
{
	for (int i = 0; i < ms; i++) {
		if (emu_get_state() == s) {
			return true;
		}
		usleep(1000);
	}
	return emu_get_state() == s;
}

// Wait for the next HALTED event; returns false on timeout.
static bool wait_halted_event(emu_event_t *ev, int ms)
{
	for (int i = 0; i < ms; i++) {
		while (emu_poll_event(ev)) {
			if (ev->type == EMU_EV_HALTED) {
				return true;
			}
		}
		usleep(1000);
	}
	return false;
}

static void drain_events(void)
{
	emu_event_t ev;
	while (emu_poll_event(&ev)) {
	}
}

// Program at $8000:
//   8000 01        NOP
//   8001 4C        INCA
//   8002 01        NOP
//   8003 20 FB     BRA $8000
static void load_program(void)
{
	static const uint8_t prog[] = {0x01, 0x4C, 0x01, 0x20, 0xFB};
	for (size_t i = 0; i < sizeof(prog); i++) {
		fake_mem[0x8000 + i] = prog[i];
	}
	fake_mem[0x8010] = 0xDD; // HCF
	fake_mem[0x8020] = 0x02; // unimplemented
	fake_mem[VECTOR_RESET] = 0x80;
	fake_mem[VECTOR_RESET + 1] = 0x00;
}

static void test_reset_and_run(void)
{
	printf("reset and run\n");
	// After start-up the emulator leaves reset and runs on its own
	CHECK(wait_state(EMU_STATE_RUNNING, 500));
	emu_regs_t r;
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.pc >= 0x8000 && r.pc <= 0x8004);
}

static void test_halt_and_regs(void)
{
	printf("halt, registers\n");
	drain_events();
	CHECK_OK(emu_halt());
	CHECK(emu_get_state() == EMU_STATE_HALTED);
	emu_event_t ev;
	CHECK(wait_halted_event(&ev, 100));
	CHECK(ev.reason == EMU_HALT_USER);

	CHECK_OK(emu_set_reg(EMU_REG_PC, 0x8000));
	CHECK_OK(emu_set_reg(EMU_REG_A, 0x10));
	CHECK_OK(emu_set_reg(EMU_REG_CCR, 0x00));
	CHECK(emu_set_reg(EMU_REG_A, 0x100) == EMU_ERR_RANGE);
	emu_regs_t r;
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.pc == 0x8000);
	CHECK(r.a == 0x10);
	CHECK(r.ccr == 0xC0); // bits 7-6 forced on

	emu_reg_t reg;
	CHECK(emu_reg_from_name("SP", &reg) && reg == EMU_REG_SP);
	CHECK(!emu_reg_from_name("q", &reg));
}

static void test_step(void)
{
	printf("single step\n");
	drain_events();
	CHECK_OK(emu_step(1));
	CHECK_OK(emu_step_wait(500));
	emu_event_t ev;
	CHECK(wait_halted_event(&ev, 100));
	CHECK(ev.reason == EMU_HALT_STEP_DONE);
	CHECK(ev.pc == 0x8001);
	CHECK(ev.prev_pc == 0x8000);

	CHECK_OK(emu_step(1)); // INCA
	CHECK_OK(emu_step_wait(500));
	emu_regs_t r;
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.a == 0x11);
	CHECK(r.pc == 0x8002);

	CHECK_OK(emu_step(3)); // NOP, BRA, NOP -> $8001
	CHECK_OK(emu_step_wait(500));
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.pc == 0x8001);
	CHECK(emu_get_state() == EMU_STATE_HALTED);

	CHECK(emu_step(0) == EMU_ERR_ARG);
}

static void test_step_requires_halt(void)
{
	printf("step only while halted\n");
	CHECK_OK(emu_run());
	CHECK(emu_step(1) == EMU_ERR_STATE);
	CHECK_OK(emu_halt());
}

static void test_breakpoints(void)
{
	printf("breakpoints\n");
	CHECK_OK(emu_bp_clear());
	CHECK_OK(emu_bp_add(0x8002));
	CHECK(emu_bp_add(0x8002) == EMU_ERR_EXISTS);
	uint16_t list[64];
	CHECK(emu_bp_max() == 64);
	CHECK(emu_bp_list(list, 64) == 1 && list[0] == 0x8002);

	drain_events();
	CHECK_OK(emu_run());
	emu_event_t ev;
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT);
	CHECK(ev.pc == 0x8002);
	CHECK(emu_get_state() == EMU_STATE_HALTED);

	// Resuming must not re-hit the breakpoint we are sitting on, but must hit
	// it again on the next pass through the loop.
	emu_regs_t r1, r2;
	CHECK_OK(emu_get_regs(&r1));
	CHECK_OK(emu_run());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT);
	CHECK(ev.pc == 0x8002);
	CHECK_OK(emu_get_regs(&r2));
	CHECK(r2.a == (uint8_t)(r1.a + 1)); // went round the loop once

	// Stepping from a breakpoint executes it
	CHECK_OK(emu_step(1));
	CHECK_OK(emu_step_wait(500));
	CHECK_OK(emu_get_regs(&r1));
	CHECK(r1.pc == 0x8003);

	// A multi-step that reaches a breakpoint stops there
	drain_events();
	CHECK_OK(emu_step(100));
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT && ev.pc == 0x8002);

	CHECK_OK(emu_bp_remove(0x8002));
	CHECK(emu_bp_remove(0x8002) == EMU_ERR_NOT_FOUND);

	for (int i = 0; i < 64; i++) {
		CHECK_OK(emu_bp_add((uint16_t)(0x9000 + i)));
	}
	CHECK(emu_bp_add(0xA000) == EMU_ERR_FULL);
	CHECK_OK(emu_bp_clear());
	CHECK(emu_bp_list(list, 64) == 0);
}

static void test_hcf_and_illegal(void)
{
	printf("HCF and unimplemented opcodes\n");
	emu_event_t ev;

	drain_events();
	CHECK_OK(emu_set_reg(EMU_REG_PC, 0x8010));
	CHECK_OK(emu_run());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_HCF && ev.opcode == 0xDD && ev.prev_pc == 0x8010);
	CHECK(emu_get_state() == EMU_STATE_HALTED);

	drain_events();
	CHECK_OK(emu_set_reg(EMU_REG_PC, 0x8020));
	CHECK_OK(emu_step(5));
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_ILLEGAL && ev.opcode == 0x02 && ev.prev_pc == 0x8020);
	CHECK(emu_get_state() == EMU_STATE_HALTED);
}

static void test_memory(void)
{
	printf("memory access\n");
	uint8_t buf[4] = {1, 2, 3, 4}, out[4] = {0};
	CHECK_OK(emu_mem_write(0x0100, buf, 4));
	CHECK_OK(emu_mem_read(0x0100, out, 4));
	CHECK(out[0] == 1 && out[3] == 4);
	CHECK(emu_mem_read(0xFFFF, out, 2) == EMU_ERR_RANGE);
	CHECK(emu_mem_read(0x0000, out, 0) == EMU_ERR_RANGE);

	// Host reads must not be charged to the emulated cycle count
	uint32_t before = pending_cycles;
	CHECK_OK(emu_mem_read(0x8000, out, 4));
	CHECK(pending_cycles == before);
}

static void test_lock(void)
{
	printf("lock/unlock while running\n");
	CHECK_OK(emu_set_reg(EMU_REG_PC, 0x8000));
	CHECK_OK(emu_run());
	CHECK_OK(emu_lock());
	CHECK(emu_is_locked());
	emu_regs_t a, b;
	CHECK_OK(emu_get_regs(&a)); // nested lock
	usleep(20000);
	CHECK_OK(emu_get_regs(&b));
	CHECK(a.pc == b.pc && a.a == b.a); // no instructions executed while locked
	CHECK(emu_run() == EMU_ERR_LOCKED);
	CHECK(emu_halt() == EMU_ERR_LOCKED);
	emu_unlock();
	CHECK(!emu_is_locked());
	usleep(20000);
	CHECK_OK(emu_get_regs(&b));
	CHECK(b.a != a.a); // running again
	CHECK(emu_get_state() == EMU_STATE_RUNNING);
}

static void test_status(void)
{
	printf("status\n");
	emu_status_t st;
	CHECK_OK(emu_get_status(&st));
	CHECK(st.state == EMU_STATE_RUNNING);
	CHECK(st.running && !st.halted);
	CHECK(st.instruction_count > 0);
}

static void test_reset_pin(void)
{
	printf("/RESET pin\n");
	drain_events();
	fake_reset_pin = true;
	CHECK(wait_state(EMU_STATE_RESETTING, 500));
	emu_event_t ev;
	bool got_reset = false;
	for (int i = 0; i < 100 && !got_reset; i++) {
		while (emu_poll_event(&ev)) {
			got_reset |= (ev.type == EMU_EV_RESET);
		}
		usleep(1000);
	}
	CHECK(got_reset);
	// Commands still work while held in reset (state polls every 1 ms)
	CHECK_OK(emu_reset());
	emu_regs_t r;
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.pc == 0x8000);
	fake_reset_pin = false;
	CHECK(wait_state(EMU_STATE_RUNNING, 500));
}

static void test_load_state(void)
{
	printf("loading state\n");
	CHECK_OK(emu_load_begin());
	CHECK(emu_get_state() == EMU_STATE_LOADING);
	CHECK(emu_run() == EMU_ERR_STATE); // must reset to leave LOADING
	CHECK(emu_step(1) == EMU_ERR_STATE);
	CHECK_OK(emu_reset());
	CHECK(wait_state(EMU_STATE_RUNNING, 500));
}

// ---------------------------------------------------------------------------
// PC actions

// Halt (if not already), drain events, and put PC at pc.
static void halt_at(uint16_t pc)
{
	if (emu_get_state() != EMU_STATE_HALTED) {
		CHECK_OK(emu_halt());
	}
	CHECK(wait_state(EMU_STATE_HALTED, 500));
	drain_events();
	CHECK_OK(emu_set_reg(EMU_REG_PC, pc));
}

static void step_n(uint32_t n)
{
	CHECK_OK(emu_step(n));
	CHECK_OK(emu_step_wait(1000));
}

#define BIT(pin) (1u << ((pin) - 32))

static void test_action_api(void)
{
	printf("PC action table API\n");
	halt_at(0x8000);
	CHECK_OK(emu_bp_clear());
	CHECK(emu_action_clear(-1) == EMU_ERR_NOT_FOUND);

	emu_action_t a = {.addr = 0x8000, .type = EMU_ACT_SET, .pin = 42};
	CHECK_OK(emu_action_add(&a));
	CHECK(emu_action_add(&a) == EMU_ERR_EXISTS);
	CHECK(fake_gpio_outputs & (1ull << 42)); // configured on first use
	emu_action_t b = {.addr = 0x8000, .type = EMU_ACT_CLR, .pin = 42};
	CHECK(emu_action_add(&b) == EMU_ERR_EXISTS); // one GPIO action per pin per address
	b.pin = 43;
	CHECK_OK(emu_action_add(&b));
	emu_action_t bad = {.addr = 0x8000, .type = EMU_ACT_SET, .pin = 31};
	CHECK(emu_action_add(&bad) == EMU_ERR_RANGE);
	bad.pin = 47; // PSRAM CS
	CHECK(emu_action_add(&bad) == EMU_ERR_RANGE);
	bad.pin = 40; // UART
	CHECK(emu_action_add(&bad) == EMU_ERR_RANGE);
	bad.type = 99;
	CHECK(emu_action_add(&bad) == EMU_ERR_ARG);

	emu_action_t c = {.addr = 0x7000, .type = EMU_ACT_COUNT};
	CHECK_OK(emu_action_add(&c));
	CHECK(emu_action_add(&c) == EMU_ERR_EXISTS);
	CHECK_OK(emu_bp_add(0x8000)); // breakpoint shares the address

	emu_action_t list[8];
	CHECK(emu_action_list(list, 8) == 4);
	CHECK(list[0].addr == 0x7000 && list[0].type == EMU_ACT_COUNT);
	CHECK(list[1].addr == 0x8000 && list[1].type == EMU_ACT_BREAK);
	CHECK(list[2].type == EMU_ACT_SET && list[2].pin == 42);
	CHECK(list[3].type == EMU_ACT_CLR && list[3].pin == 43);
	uint16_t bps[4];
	CHECK(emu_bp_list(bps, 4) == 1 && bps[0] == 0x8000);

	// trig clear keeps breakpoints; break clear keeps triggers
	CHECK_OK(emu_action_clear(0x8000));
	CHECK(emu_action_list(list, 8) == 2);
	CHECK(emu_bp_list(bps, 4) == 1);
	CHECK_OK(emu_action_add(&a));
	CHECK_OK(emu_bp_clear());
	CHECK(emu_action_list(list, 8) == 2);
	CHECK(emu_action_remove(&b) == EMU_ERR_NOT_FOUND);
	CHECK_OK(emu_action_remove(&a));
	CHECK_OK(emu_action_remove(&c));
	CHECK(emu_action_list(list, 8) == 0);

	// Names
	emu_action_type_t t;
	CHECK(emu_action_from_name("pulse", &t) && t == EMU_ACT_PULSE);
	CHECK(emu_action_from_name("TOG", &t) && t == EMU_ACT_TOGGLE);
	CHECK(!emu_action_from_name("blink", &t));

	// Limits: 64 addresses, 128 actions
	for (int i = 0; i < 64; i++) {
		emu_action_t x = {.addr = (uint16_t)(0x9000 + i), .type = EMU_ACT_COUNT};
		CHECK_OK(emu_action_add(&x));
	}
	emu_action_t x = {.addr = 0xA000, .type = EMU_ACT_COUNT};
	CHECK(emu_action_add(&x) == EMU_ERR_FULL);
	for (int i = 0; i < 64; i++) {
		emu_action_t y = {.addr = (uint16_t)(0x9000 + i), .type = EMU_ACT_NOTIFY};
		CHECK_OK(emu_action_add(&y));
	}
	emu_action_t z = {.addr = 0x9000, .type = EMU_ACT_SET, .pin = 32};
	CHECK(emu_action_add(&z) == EMU_ERR_FULL);
	CHECK_OK(emu_action_clear(-1));
	CHECK(emu_action_list(list, 8) == 0);
}

static void test_spi_debug_exclusive(void)
{
	printf("SPI debug pins vs trigger pins\n");
	emu_action_t a = {.addr = 0x8000, .type = EMU_ACT_SET, .pin = 34};
	CHECK_OK(emu_debug_spi_enable(true));
	CHECK(emu_action_add(&a) == EMU_ERR_BUSY);
	a.pin = 42; // not an SPI pin: fine
	CHECK_OK(emu_action_add(&a));
	a.pin = 34;
	CHECK_OK(emu_debug_spi_enable(false));
	CHECK_OK(emu_action_add(&a));
	CHECK(emu_debug_spi_enable(true) == EMU_ERR_BUSY);
	CHECK(!emu_debug_spi_is_enabled());
	CHECK_OK(emu_action_remove(&a));
	CHECK_OK(emu_debug_spi_enable(true));
	CHECK_OK(emu_debug_spi_enable(false));
	CHECK_OK(emu_action_clear(-1));
	CHECK(emu_trigger_pin_is_spi_debug(36) && !emu_trigger_pin_is_spi_debug(42));
	uint8_t pins[16];
	CHECK(emu_trigger_pins(pins, 16) == 10);
}

static void test_gpio_actions(void)
{
	printf("GPIO set/clr/toggle\n");
	// Loop: 8000 NOP, 8001 INCA, 8002 NOP, 8003 BRA 8000
	emu_action_t set42 = {.addr = 0x8000, .type = EMU_ACT_SET, .pin = 42};
	emu_action_t tog43 = {.addr = 0x8001, .type = EMU_ACT_TOGGLE, .pin = 43};
	emu_action_t clr42 = {.addr = 0x8002, .type = EMU_ACT_CLR, .pin = 42};
	CHECK_OK(emu_action_add(&set42));
	CHECK_OK(emu_action_add(&tog43));
	CHECK_OK(emu_action_add(&clr42));

	halt_at(0x8000);
	fake_gpio_reset_log();
	step_n(1);
	CHECK(fake_gpio_log_count == 1);
	CHECK(fake_gpio_log[0].op == FAKE_GPIO_SET && fake_gpio_log[0].mask == BIT(42));
	CHECK(fake_gpio_hi_out & BIT(42));
	step_n(1);
	CHECK(fake_gpio_log_count == 2 && fake_gpio_log[1].op == FAKE_GPIO_TOGL &&
	      fake_gpio_log[1].mask == BIT(43));
	CHECK(fake_gpio_hi_out & BIT(43));
	step_n(2); // NOP at 8002 (clr), BRA
	CHECK(fake_gpio_log_count == 3 && fake_gpio_log[2].op == FAKE_GPIO_CLR);
	CHECK(!(fake_gpio_hi_out & BIT(42)));

	// Running: one write per action per pass
	fake_gpio_reset_log();
	CHECK_OK(emu_run());
	usleep(20000);
	CHECK_OK(emu_halt());
	uint32_t hits0 = 0, hits2 = 0;
	CHECK_OK(emu_hits_get(0x8000, &hits0));
	CHECK_OK(emu_hits_get(0x8002, &hits2));
	CHECK(hits0 > 10);
	uint32_t sets = 0, clrs = 0;
	for (uint32_t i = 0; i < fake_gpio_log_count; i++) {
		sets += fake_gpio_log[i].op == FAKE_GPIO_SET;
		clrs += fake_gpio_log[i].op == FAKE_GPIO_CLR;
	}
	CHECK(fake_gpio_log_count < FAKE_GPIO_LOG_SIZE);
	CHECK(sets + clrs + (fake_gpio_log_count - sets - clrs) == fake_gpio_log_count);
	CHECK(sets >= 10 && (sets == clrs || sets == clrs + 1));

	// Removing the last action on a pin drives it low
	halt_at(0x8001);
	step_n(1); // toggle 43 again
	CHECK_OK(emu_action_clear(-1));
	CHECK(fake_gpio_hi_out == 0);
}

static void test_breakpoint_with_actions(void)
{
	printf("breakpoint + actions at one address fire once\n");
	emu_action_t set42 = {.addr = 0x8002, .type = EMU_ACT_SET, .pin = 42};
	CHECK_OK(emu_action_add(&set42));
	CHECK_OK(emu_bp_add(0x8002));
	halt_at(0x8000);
	fake_gpio_reset_log();
	CHECK_OK(emu_run());
	emu_event_t ev;
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT && ev.pc == 0x8002);
	CHECK(fake_gpio_log_count == 1);
	uint32_t hits;
	CHECK_OK(emu_hits_get(0x8002, &hits));
	CHECK(hits == 1);
	// Resume: no second write for the same visit; next pass writes and stops again
	CHECK_OK(emu_run());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT && ev.pc == 0x8002);
	CHECK(fake_gpio_log_count == 2);
	CHECK_OK(emu_hits_get(0x8002, &hits));
	CHECK(hits == 2);
	// Stepping from the breakpoint doesn't fire again either
	step_n(1);
	CHECK(fake_gpio_log_count == 2);
	// A user halt at an address with actions: they fire on resume
	halt_at(0x8002);
	CHECK_OK(emu_bp_clear());
	step_n(1);
	CHECK(fake_gpio_log_count == 3);
	CHECK_OK(emu_action_clear(-1));
}

static void test_pulse(void)
{
	printf("GPIO pulse\n");
	// Toy CPU: NOP and INCA take 1 cycle, BRA 2. Pulse 3 cycles at 8000.
	emu_action_t p = {.addr = 0x8000, .type = EMU_ACT_PULSE, .pin = 44, .width = 3};
	CHECK_OK(emu_action_add(&p));
	halt_at(0x8000);
	fake_gpio_reset_log();
	step_n(8);
	// set at 8000, cleared >= 3 cycles later, within one instruction (<= 2 here)
	CHECK(fake_gpio_log_count >= 2);
	CHECK(fake_gpio_log[0].op == FAKE_GPIO_SET && fake_gpio_log[0].mask == BIT(44));
	CHECK(fake_gpio_log[1].op == FAKE_GPIO_CLR && fake_gpio_log[1].mask == BIT(44));
	uint32_t w = fake_gpio_log[1].cycle - fake_gpio_log[0].cycle;
	CHECK(w >= 3 && w <= 3 + 2);
	if (!(w >= 3 && w <= 5)) {
		printf("    pulse width %u cycles\n", w);
	}

	// Width 0 means 1
	emu_action_t q = {.addr = 0x8001, .type = EMU_ACT_PULSE, .pin = 45, .width = 0};
	CHECK_OK(emu_action_add(&q));
	emu_action_t list[4];
	CHECK(emu_action_list(list, 4) == 2 && list[1].width == 1);

	// A halt ends a pulse in progress
	CHECK_OK(emu_action_remove(&p));
	p.width = 1000;
	CHECK_OK(emu_action_add(&p));
	halt_at(0x8000);
	step_n(1);
	CHECK(!(fake_gpio_hi_out & BIT(44)));
	CHECK(pc_pulse_active == 0);

	// While running, a long pulse is retriggered every pass and stays high
	CHECK_OK(emu_run());
	usleep(10000);
	CHECK(fake_gpio_hi_out & BIT(44));
	CHECK_OK(emu_halt());
	CHECK(!(fake_gpio_hi_out & BIT(44))); // ended by the halt
	CHECK_OK(emu_action_clear(-1));
}

static void test_hits(void)
{
	printf("hit counters\n");
	uint32_t hits;
	CHECK(emu_hits_get(0x8001, &hits) == EMU_ERR_NOT_FOUND);
	emu_action_t c = {.addr = 0x8001, .type = EMU_ACT_COUNT};
	CHECK_OK(emu_action_add(&c));
	halt_at(0x8000);
	step_n(5 * 4); // five passes round the loop
	CHECK_OK(emu_hits_get(0x8001, &hits));
	CHECK(hits == 5);
	// Adding another action at the address keeps the count
	emu_action_t n = {.addr = 0x8001, .type = EMU_ACT_NOTIFY};
	CHECK_OK(emu_action_add(&n));
	CHECK_OK(emu_hits_get(0x8001, &hits));
	CHECK(hits == 5);
	CHECK_OK(emu_hits_reset(0x8001));
	CHECK_OK(emu_hits_get(0x8001, &hits));
	CHECK(hits == 0);
	CHECK(emu_hits_reset(0x8003) == EMU_ERR_NOT_FOUND);
	CHECK_OK(emu_hits_reset(-1));
	CHECK_OK(emu_action_clear(-1));
	CHECK(emu_hits_get(0x8001, &hits) == EMU_ERR_NOT_FOUND);
}

static void test_notify(void)
{
	printf("notify events\n");
	emu_action_t n = {.addr = 0x8001, .type = EMU_ACT_NOTIFY};
	CHECK_OK(emu_action_add(&n));
	halt_at(0x8000);
	uint32_t drops = emu_events_dropped();
	CHECK_OK(emu_run());
	usleep(20000);
	CHECK_OK(emu_halt());
	// Many hits, but only one event until the host consumes it
	int hits_ev = 0, halted_ev = 0;
	emu_event_t ev, first = {0};
	while (emu_poll_event(&ev)) {
		if (ev.type == EMU_EV_PC_HIT) {
			if (hits_ev++ == 0) {
				first = ev;
			}
		}
		halted_ev += ev.type == EMU_EV_HALTED;
	}
	CHECK(hits_ev == 1 && halted_ev == 1);
	CHECK(first.pc == 0x8001 && first.prev_pc == 0x8000 && first.hits == 1);
	uint32_t hits;
	CHECK_OK(emu_hits_get(0x8001, &hits));
	CHECK(hits > 10);

	// Consuming it re-arms: polling while running gives a stream of events
	CHECK_OK(emu_run());
	hits_ev = 0;
	uint32_t last_hits = 0;
	bool increasing = true;
	for (int i = 0; i < 200 && hits_ev < 5; i++) {
		while (emu_poll_event(&ev)) {
			if (ev.type == EMU_EV_PC_HIT) {
				increasing &= ev.hits > last_hits;
				last_hits = ev.hits;
				hits_ev++;
			}
		}
		usleep(1000);
	}
	CHECK(hits_ev >= 5 && increasing);
	CHECK_OK(emu_halt());
	drain_events();
	CHECK_OK(emu_action_clear(-1));

	// 60 NOTIFY addresses that are not drained must not crowd out HALTED
	for (int i = 0; i < 60; i++) {
		fake_mem[0x9000 + i] = 0x01; // NOP
		emu_action_t a = {.addr = (uint16_t)(0x9000 + i), .type = EMU_ACT_NOTIFY};
		CHECK_OK(emu_action_add(&a));
	}
	fake_mem[0x903C] = 0x20; // BRA $9000
	fake_mem[0x903D] = (uint8_t)(0x9000 - 0x903E);
	halt_at(0x9000);
	CHECK_OK(emu_run());
	usleep(20000);
	CHECK_OK(emu_halt());
	hits_ev = halted_ev = 0;
	while (emu_poll_event(&ev)) {
		hits_ev += ev.type == EMU_EV_PC_HIT;
		halted_ev += ev.type == EMU_EV_HALTED;
	}
	CHECK(halted_ev == 1);
	CHECK(hits_ev == EMU_EVENT_RING_SIZE - PC_NOTIFY_RESERVE);
	CHECK(emu_events_dropped() == drops);
	CHECK_OK(emu_action_clear(-1));
}

// Subroutine test program:
//   8100 8E 01 FF   LDS #$01FF
//   8103 BD 82 00   JSR $8200
//   8106 01         NOP
//   8107 8D 07      BSR $8110
//   8109 3F         SWI           (vector -> $8120)
//   810A 01         NOP
//   810B 20 FE      BRA *
//   8110 4C         INCA
//   8111 39         RTS
//   8120 4C         INCA
//   8121 3B         RTI
//   8200 4C         INCA
//   8201 4C         INCA
//   8202 39         RTS
// Recursion: 8300 BD 83 10 JSR $8310; 8303 39 RTS
//            8310 BD 83 03 JSR $8303 (reaches $8303 deeper); 8313 39 RTS
static void load_sub_program(void)
{
	static const struct {
		uint16_t addr;
		uint8_t bytes[4];
		uint8_t n;
	} code[] = {
		{0x8100, {0x8E, 0x01, 0xFF}, 3}, {0x8103, {0xBD, 0x82, 0x00}, 3},
		{0x8106, {0x01}, 1},             {0x8107, {0x8D, 0x07}, 2},
		{0x8109, {0x3F}, 1},             {0x810A, {0x01, 0x20, 0xFE}, 3},
		{0x8110, {0x4C, 0x39}, 2},       {0x8120, {0x4C, 0x3B}, 2},
		{0x8200, {0x4C, 0x4C, 0x39}, 3}, {0x8300, {0xBD, 0x83, 0x10, 0x39}, 4},
		{0x8310, {0xBD, 0x83, 0x03, 0x39}, 4},
	};
	for (size_t i = 0; i < sizeof(code) / sizeof(code[0]); i++) {
		for (uint8_t j = 0; j < code[i].n; j++) {
			fake_mem[code[i].addr + j] = code[i].bytes[j];
		}
	}
	fake_mem[VECTOR_SWI] = 0x81;
	fake_mem[VECTOR_SWI + 1] = 0x20;
}

static void test_step_over(void)
{
	printf("step over (next)\n");
	load_sub_program();
	halt_at(0x8100);
	CHECK_OK(emu_set_reg(EMU_REG_A, 0));
	emu_event_t ev;
	emu_regs_t r;

	// Not a call: same as step 1
	CHECK_OK(emu_step_over());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_STEP_DONE && ev.pc == 0x8103);

	// JSR
	CHECK_OK(emu_step_over());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x8106);
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.a == 2 && r.sp == 0x01FF);

	CHECK_OK(emu_step_over()); // NOP
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.pc == 0x8107);

	// BSR
	CHECK_OK(emu_step_over());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x8109);
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.a == 3 && r.sp == 0x01FF);

	// SWI
	CHECK_OK(emu_step_over());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x810A);
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.a == 3 && r.sp == 0x01FF); // the handler's INCA is undone by RTI

	// Recursion: the return address is reached deeper first; stop only at the same SP
	emu_action_t c = {.addr = 0x8303, .type = EMU_ACT_COUNT};
	CHECK_OK(emu_action_add(&c));
	halt_at(0x8300);
	CHECK_OK(emu_set_reg(EMU_REG_SP, 0x01FF));
	CHECK_OK(emu_step_over());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x8303);
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.sp == 0x01FF);
	uint32_t hits;
	CHECK_OK(emu_hits_get(0x8303, &hits));
	CHECK(hits == 2);
	CHECK_OK(emu_action_clear(-1));

	// A breakpoint inside the callee stops it first and cancels the stop
	halt_at(0x8103);
	CHECK_OK(emu_set_reg(EMU_REG_SP, 0x01FF));
	CHECK_OK(emu_bp_add(0x8201));
	CHECK_OK(emu_step_over());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT && ev.pc == 0x8201);
	CHECK(!pc_temp.active);
	CHECK_OK(emu_bp_clear());
	CHECK_OK(emu_bp_add(0x810B));
	CHECK_OK(emu_run());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_BREAKPOINT && ev.pc == 0x810B); // not UNTIL at 8106
	CHECK_OK(emu_bp_clear());

	// Only while halted
	CHECK_OK(emu_run());
	CHECK(emu_step_over() == EMU_ERR_STATE);
	CHECK_OK(emu_halt());
}

static void test_run_until(void)
{
	printf("run until\n");
	emu_event_t ev;
	emu_regs_t r;
	halt_at(0x8000);
	CHECK_OK(emu_run_until(0x8003));
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x8003);
	CHECK_OK(emu_get_regs(&r));
	uint8_t a0 = r.a;

	// Until the current address: goes round once
	CHECK_OK(emu_run_until(0x8003));
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x8003);
	CHECK_OK(emu_get_regs(&r));
	CHECK(r.a == (uint8_t)(a0 + 1));

	// While running
	CHECK_OK(emu_run());
	usleep(2000);
	drain_events();
	CHECK_OK(emu_run_until(0x8002));
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_UNTIL && ev.pc == 0x8002);

	// A user halt cancels it
	CHECK_OK(emu_set_reg(EMU_REG_PC, 0x810B)); // BRA * never reaches 8000
	CHECK_OK(emu_run_until(0x8000));
	usleep(2000);
	CHECK_OK(emu_halt());
	CHECK(wait_halted_event(&ev, 500));
	CHECK(ev.reason == EMU_HALT_USER);
	CHECK(!pc_temp.active);

	CHECK_OK(emu_load_begin());
	CHECK(emu_run_until(0x8000) == EMU_ERR_STATE);
	CHECK_OK(emu_reset());
	CHECK(wait_state(EMU_STATE_RUNNING, 500));
	CHECK_OK(emu_halt());
}

int main(void)
{
	load_program();
	emu_api_init();

	pthread_t core;
	pthread_create(&core, NULL, core_thread, NULL);

	test_reset_and_run();
	test_halt_and_regs();
	test_step();
	test_step_requires_halt();
	test_breakpoints();
	test_hcf_and_illegal();
	test_memory();
	test_lock();
	test_status();
	test_reset_pin();
	test_load_state();
	test_action_api();
	test_spi_debug_exclusive();
	test_gpio_actions();
	test_breakpoint_with_actions();
	test_pulse();
	test_hits();
	test_notify();
	test_step_over();
	test_run_until();

	if (failures) {
		printf("\n%d check(s) FAILED\n", failures);
		return 1;
	}
	printf("\nAll checks passed\n");
	return 0;
}
