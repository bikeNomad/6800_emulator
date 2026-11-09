#include "emulator.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "debug_spi.h"
#include "hardware/structs/timer.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"

// Emulator core state machine.
//
// Everything in this file that runs after run_emulator_sm() starts is placed
// in SRAM (__time_critical_func) and must stay that way: while the emulator
// is running or parked, the host core may be writing flash, and any XIP access
// from this core during a flash erase/program would fault or return garbage.
// tools/check_ram_hotpath.py verifies this after every build.
//
// This core never prints. It reports to the host through emu_event_q.

#define EMU_CORE_FUNC(name) __time_critical_func(name)

// Rings shared with emu_api.c (host side)
emu_cmd_ring_t emu_cmd_q;
emu_reply_ring_t emu_reply_q;
emu_event_ring_t emu_event_q;
volatile uint32_t emu_event_drops;

volatile emu_state_t emu_core_state = EMU_STATE_INITIALIZING;
volatile uint32_t emu_steps_pending;

static FSM emulator_fsm;

// Command currently being dispatched (valid inside state handlers)
static uint8_t current_cmd_seq;
static uint32_t current_cmd_arg;

// When resuming from HALTED, don't immediately re-hit a breakpoint at the
// instruction we stopped on.
static bool skip_bp_valid;
static uint16_t skip_bp_pc;

// True for the first instruction of a step sequence (breakpoints at the
// starting PC are ignored, as above).
static bool step_first;

static void s_initializing(FSM *fsm, uint8_t event);
static void s_resetting(FSM *fsm, uint8_t event);
static void s_running(FSM *fsm, uint8_t event);
static void s_halted(FSM *fsm, uint8_t event);
static void s_loading(FSM *fsm, uint8_t event);
static void s_waiting_for_interrupt(FSM *fsm, uint8_t event);

// ---------------------------------------------------------------------------
// Messaging helpers

static void EMU_CORE_FUNC(reply)(emu_result_t result)
{
	emu_reply_msg_t m = {.seq = current_cmd_seq, .result = (uint8_t)result};
	// The host waits for each reply before sending another command, so this
	// cannot overflow in normal use.
	(void)emu_reply_ring_push(&emu_reply_q, &m);
}

// Reply to any command a state does not handle, so the host never times out.
static inline void reply_unhandled(uint8_t event)
{
	if (event >= EVT_USER_START) {
		reply(EMU_ERR_STATE);
	}
}

void EMU_CORE_FUNC(emu_core_post_event)(const emu_event_t *ev)
{
	if (!emu_event_ring_push(&emu_event_q, ev)) {
		emu_event_drops++;
	}
}

static void EMU_CORE_FUNC(post_halted)(emu_halt_reason_t reason)
{
	emu_event_t ev = {
		.type = EMU_EV_HALTED,
		.reason = (uint8_t)reason,
		.opcode = (reason == EMU_HALT_ILLEGAL || reason == EMU_HALT_HCF) ? cpu.stop_opcode
										 : 0,
		.pc = cpu.pc,
		.prev_pc = cpu.last_opcode_address,
	};
	emu_core_post_event(&ev);
}

static void EMU_CORE_FUNC(post_reset)(void)
{
	emu_event_t ev = {
		.type = EMU_EV_RESET,
		.pc = cpu.pc,
		.prev_pc = cpu.last_opcode_address,
	};
	emu_core_post_event(&ev);
}

// Park between instructions until the host sends EMU_CMD_RESUME. Nothing in
// the CPU state, memory map or breakpoint list may change on this core while
// parked, so the host can access them directly.
static void EMU_CORE_FUNC(park)(void)
{
	reply(EMU_OK);
	for (;;) {
		emu_cmd_msg_t m;
		if (emu_cmd_ring_pop(&emu_cmd_q, &m)) {
			current_cmd_seq = m.seq;
			if (m.cmd == EMU_CMD_RESUME) {
				reply(EMU_OK);
				return;
			}
			reply(m.cmd == EMU_CMD_PAUSE ? EMU_OK : EMU_ERR_LOCKED);
		}
		tight_loop_contents();
	}
}

static bool EMU_CORE_FUNC(receive_event)(FSM *unused, uint8_t *event)
{
	(void)unused;
	emu_cmd_msg_t m;
	if (!emu_cmd_ring_pop(&emu_cmd_q, &m)) {
		return false;
	}
	current_cmd_seq = m.seq;
	current_cmd_arg = m.arg;

	switch (m.cmd) {
	case EMU_CMD_PAUSE:
		park();
		return false;
	case EMU_CMD_RESUME:
		reply(EMU_OK); // not parked; harmless
		return false;
	default:
		*event = m.cmd;
		return true;
	}
}

static void EMU_CORE_FUNC(delay_us)(uint32_t us)
{
	uint32_t start = timer_hw->timerawl;
	while (timer_hw->timerawl - start < us) {
		tight_loop_contents();
	}
}

static inline void set_state(emu_state_t s)
{
	emu_core_state = s;
	__dmb();
}

// ---------------------------------------------------------------------------
// Instruction execution shared by RUNNING and single-stepping

// Execute one instruction. Returns true if the instruction asked the CPU to
// stop (HCF or an unimplemented opcode); cpu.stop_reason says which.
static bool EMU_CORE_FUNC(execute_one)(void)
{
	bus_sync();
	instruction_execute();
	debug_spi_log();
	if (cpu.stop_requested) {
		cpu.stop_requested = false;
		return true;
	}
	return false;
}

static inline bool breakpoint_hit(uint16_t pc)
{
	if (cpu.breakpoint_count == 0) {
		return false;
	}
	if (skip_bp_valid && pc == skip_bp_pc) {
		return false;
	}
	return cpu_check_breakpoint(pc);
}

// ---------------------------------------------------------------------------
// States

// Entered after power-on
// NOTE: memory and PIO initialized in main().
static void s_initializing(FSM *fsm, uint8_t event)
{
	set_state(EMU_STATE_INITIALIZING);
	cpu_init();
	interrupts_init();

	switch (event) {
	case EVT_INIT:
		fsm_change_state(fsm, &s_resetting);
		break;
	}
}

// Entered after initialization, and whenever /RESET is asserted.
static void EMU_CORE_FUNC(s_resetting)(FSM *fsm, uint8_t event)
{
	switch (event) {
	case EVT_ENTER:
		set_state(EMU_STATE_RESETTING);
		emu_steps_pending = 0;
		skip_bp_valid = false;
		interrupt_service_reset();
		eclock_stop();
		post_reset();
		break;
	case EVT_POLL:
		if (interrupt_check() != INT_RESET) {
			fsm_change_state(fsm, &s_running);
		} else {
			delay_us(1000);
		}
		break;
	case EMU_CMD_HALT:
		fsm_change_state(fsm, &s_halted);
		reply(EMU_OK);
		break;
	case EMU_CMD_RUN:
		fsm_change_state(fsm, &s_running);
		reply(EMU_OK);
		break;
	case EMU_CMD_RESET:
		interrupt_service_reset();
		reply(EMU_OK);
		break;
	case EMU_CMD_LOAD:
		fsm_change_state(fsm, &s_loading);
		reply(EMU_OK);
		break;
	default:
		reply_unhandled(event);
		break;
	}
}

// Normal emulator running instructions
static void EMU_CORE_FUNC(s_running)(FSM *fsm, uint8_t event)
{
	switch (event) {
	case EVT_POLL: {
		interrupt_t interrupt = interrupt_check();
		if (interrupt == INT_RESET) {
			fsm_change_state(fsm, &s_resetting);
			break;
		}
		// Check for breakpoints before executing the instruction at PC
		if (breakpoint_hit(cpu.pc)) {
			post_halted(EMU_HALT_BREAKPOINT);
			fsm_change_state(fsm, &s_halted);
			break;
		}
		skip_bp_valid = false;

		if (execute_one()) {
			post_halted((emu_halt_reason_t)cpu.stop_reason);
			fsm_change_state(fsm, &s_halted);
		} else if (cpu.wai_state) {
			fsm_change_state(fsm, &s_waiting_for_interrupt);
		}
		break;
	}

	case EVT_ENTER:
		set_state(EMU_STATE_RUNNING);
		eclock_start();
		cpu.running = true;
		cpu.halted = false;
		break;

	case EVT_EXIT:
		// Leave E clock running
		cpu.running = false;
		break;

	case EMU_CMD_RUN:
		reply(EMU_OK);
		break;

	case EMU_CMD_HALT:
		post_halted(EMU_HALT_USER);
		fsm_change_state(fsm, &s_halted);
		reply(EMU_OK);
		break;

	case EMU_CMD_RESET:
		fsm_change_state(fsm, &s_resetting);
		reply(EMU_OK);
		break;

	case EMU_CMD_LOAD:
		fsm_change_state(fsm, &s_loading);
		reply(EMU_OK);
		break;

	default:
		reply_unhandled(event);
		break;
	}
}

// Finish a step sequence: let real time catch up, stop the E clock, report.
static void EMU_CORE_FUNC(step_finish)(emu_halt_reason_t reason)
{
	emu_steps_pending = 0;
	bus_sync();
	eclock_stop();
	post_halted(reason);
}

// Execute one instruction of a step sequence (called from s_halted's POLL).
// Returns false if the sequence ended by leaving the HALTED state.
static bool EMU_CORE_FUNC(step_one)(FSM *fsm)
{
	interrupt_t interrupt = interrupt_check();
	if (interrupt == INT_RESET) {
		emu_steps_pending = 0;
		fsm_change_state(fsm, &s_resetting);
		return false;
	}

	if (!step_first && interrupt == INT_NONE && cpu_check_breakpoint(cpu.pc)) {
		step_finish(EMU_HALT_BREAKPOINT);
		return true;
	}
	step_first = false;

	// While in WAI with no interrupt pending, a step executes nothing.
	if (!(cpu.wai_state && interrupt == INT_NONE)) {
		if (execute_one()) {
			step_finish((emu_halt_reason_t)cpu.stop_reason);
			return true;
		}
	}

	if (--emu_steps_pending == 0) {
		step_finish(EMU_HALT_STEP_DONE);
	}
	return true;
}

// Entered on a "halt" command, a breakpoint, HCF or an unimplemented opcode.
// Single steps are executed from here without leaving the state.
static void EMU_CORE_FUNC(s_halted)(FSM *fsm, uint8_t event)
{
	switch (event) {
	case EVT_ENTER:
		set_state(EMU_STATE_HALTED);
		eclock_stop(); // E clock OFF
		cpu.halted = true;
		break;
	case EVT_EXIT:
		cpu.halted = false;
		skip_bp_valid = true;
		skip_bp_pc = cpu.pc;
		break;
	case EVT_POLL:
		if (emu_steps_pending) {
			step_one(fsm);
		}
		break;
	case EMU_CMD_RUN:
		if (emu_steps_pending) {
			step_finish(EMU_HALT_USER);
		}
		fsm_change_state(fsm, cpu.wai_state ? &s_waiting_for_interrupt : &s_running);
		reply(EMU_OK);
		break;
	case EMU_CMD_HALT:
		if (emu_steps_pending) {
			step_finish(EMU_HALT_USER);
		}
		reply(EMU_OK);
		break;
	case EMU_CMD_STEP:
		if (current_cmd_arg == 0) {
			reply(EMU_ERR_ARG);
			break;
		}
		if (!emu_steps_pending) {
			step_first = true;
			eclock_start();
		}
		emu_steps_pending = current_cmd_arg;
		reply(EMU_OK);
		break;
	case EMU_CMD_RESET:
		emu_steps_pending = 0;
		fsm_change_state(fsm, &s_resetting);
		reply(EMU_OK);
		break;
	case EMU_CMD_LOAD:
		emu_steps_pending = 0;
		fsm_change_state(fsm, &s_loading);
		reply(EMU_OK);
		break;
	default:
		reply_unhandled(event);
		break;
	}
}

// Host is writing a new ROM image; leave with a reset.
static void EMU_CORE_FUNC(s_loading)(FSM *fsm, uint8_t event)
{
	switch (event) {
	case EVT_ENTER:
		set_state(EMU_STATE_LOADING);
		eclock_stop(); // E clock OFF
		break;
	case EMU_CMD_LOAD:
		reply(EMU_OK);
		break;
	case EMU_CMD_HALT:
		fsm_change_state(fsm, &s_halted);
		reply(EMU_OK);
		break;
	case EMU_CMD_RESET:
		fsm_change_state(fsm, &s_resetting);
		reply(EMU_OK);
		break;
	default:
		reply_unhandled(event);
		break;
	}
}

// Entered upon a WAI instruction execution
static void EMU_CORE_FUNC(s_waiting_for_interrupt)(FSM *fsm, uint8_t event)
{
	switch (event) {
	case EVT_ENTER:
		set_state(EMU_STATE_WAITING);
		eclock_start();
		break;
	case EVT_POLL: {
		interrupt_t interrupt = interrupt_check();
		if (interrupt == INT_RESET) {
			fsm_change_state(fsm, &s_resetting);
		} else if (interrupt != INT_NONE) {
			fsm_change_state(fsm, &s_running);
		}
		break;
	}
	case EMU_CMD_RUN:
		reply(EMU_OK);
		break;
	case EMU_CMD_HALT:
		post_halted(EMU_HALT_USER);
		fsm_change_state(fsm, &s_halted);
		reply(EMU_OK);
		break;
	case EMU_CMD_RESET:
		fsm_change_state(fsm, &s_resetting);
		reply(EMU_OK);
		break;
	case EMU_CMD_LOAD:
		fsm_change_state(fsm, &s_loading);
		reply(EMU_OK);
		break;
	default:
		reply_unhandled(event);
		break;
	}
}

bool run_emulator_sm(void)
{
	emulator_fsm.receive_event = &receive_event;
	return fsm_run(&emulator_fsm, &s_initializing);
}
