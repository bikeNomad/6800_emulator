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
	uint16_t list[MAX_BREAKPOINTS];
	CHECK(emu_bp_list(list, MAX_BREAKPOINTS) == 1 && list[0] == 0x8002);

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

	for (int i = 0; i < MAX_BREAKPOINTS; i++) {
		CHECK_OK(emu_bp_add((uint16_t)(0x9000 + i)));
	}
	CHECK(emu_bp_add(0xA000) == EMU_ERR_FULL);
	CHECK_OK(emu_bp_clear());
	CHECK(emu_bp_list(list, MAX_BREAKPOINTS) == 0);
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

	if (failures) {
		printf("\n%d check(s) FAILED\n", failures);
		return 1;
	}
	printf("\nAll checks passed\n");
	return 0;
}
