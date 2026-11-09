#include "emulator.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "debug_spi.h"
#include "hardware/structs/timer.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"
#include "pc_actions.h"

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

// Resuming (run or step) from a halt at resume_pc: don't stop there again,
// and if the PC actions there already fired before the halt, don't repeat
// them. Applies to the first instruction only.
static bool resume_valid;
static uint16_t resume_pc;
static bool resume_skip_actions;

// The current halt happened after firing the PC actions at cpu.pc
// (BREAK or the temporary stop).
static bool halt_fired;

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
	halt_fired = (reason == EMU_HALT_BREAKPOINT || reason == EMU_HALT_UNTIL);
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

// Fire the PC actions (if any) for the instruction about to execute.
// Returns EMU_HALT_BREAKPOINT or EMU_HALT_UNTIL if the CPU must halt before
// executing it, or -1 to go on. Cost with nothing at PC: one bitmap test.
static __force_inline int check_pc_actions(void)
{
	uint16_t pc = cpu.pc;
	bool resuming = resume_valid && pc == resume_pc;
	resume_valid = false;
	if (__builtin_expect(!pc_actions_at(pc), 1)) {
		return -1;
	}
	switch (pc_actions_fire(pc, resuming && resume_skip_actions, resuming)) {
	case PC_HIT_BREAK:
		return EMU_HALT_BREAKPOINT;
	case PC_HIT_UNTIL:
		return EMU_HALT_UNTIL;
	default:
		return -1;
	}
}

static __force_inline void set_resume_point(void)
{
	resume_valid = true;
	resume_pc = cpu.pc;
	resume_skip_actions = halt_fired;
}

// The E clock is stopping (or the CPU is being reset/loaded): end pulses and
// drop a pending step-over/run-until stop.
static __force_inline void stop_pc_activity(void)
{
	pc_pulse_end_all();
	pc_temp.active = false;
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
		resume_valid = false;
		halt_fired = false;
		stop_pc_activity();
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
		pc_pulse_service();
		// PC actions (breakpoints, triggers) before executing the instruction at PC
		int stop = check_pc_actions();
		if (stop >= 0) {
			post_halted((emu_halt_reason_t)stop);
			fsm_change_state(fsm, &s_halted);
			break;
		}

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
	pc_pulse_end_all();
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

	pc_pulse_service();

	// While in WAI with no interrupt pending, a step executes nothing.
	if (!(cpu.wai_state && interrupt == INT_NONE)) {
		int stop = check_pc_actions();
		if (stop >= 0) {
			step_finish((emu_halt_reason_t)stop);
			return true;
		}
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
		stop_pc_activity();
		cpu.halted = true;
		break;
	case EVT_EXIT:
		cpu.halted = false;
		set_resume_point();
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
			set_resume_point();
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
		stop_pc_activity();
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
		pc_pulse_service();
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
