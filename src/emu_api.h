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
 * MC6800 Emulator Control API
 *
 * This is the only interface a front end (the USB CDC command shell today,
 * a MicroPython module later) should use to control and inspect the emulator.
 *
 * Threading model
 * ---------------
 * The emulator runs on its own core (the "emulator core") and owns the CPU
 * state, the E clock and the bus while it is running. All functions in this
 * header are called from a single other core (the "host core"); they are not
 * reentrant and must not be called from the emulator core or from interrupt
 * handlers.
 *
 * Commands (run, halt, step, ...) travel to the emulator core over a
 * lock-free ring buffer and are acknowledged by a reply. Anything that reads
 * or writes CPU state, memory or breakpoints first parks the emulator core
 * between instructions with emu_lock(), does the access directly, then
 * releases it with emu_unlock(). While parked, the emulator core spins in
 * SRAM and does not touch flash, so the host core may also write flash.
 *
 * Asynchronous happenings on the emulator core (a breakpoint was hit, an
 * illegal opcode halted the CPU, a step finished, /RESET was asserted) are
 * reported as emu_event_t records that the host drains with emu_poll_event().
 * The emulator core never prints.
 */

#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// ---------------------------------------------------------------------------
// Results

typedef enum emu_result_t {
	EMU_OK = 0,
	EMU_ERR_TIMEOUT,   // emulator core did not answer in time
	EMU_ERR_QUEUE,     // command queue full
	EMU_ERR_STATE,     // command not valid in the current state
	EMU_ERR_LOCKED,    // command not allowed while the host holds emu_lock()
	EMU_ERR_RANGE,     // address/length/value out of range
	EMU_ERR_FULL,      // no room (e.g. too many breakpoints)
	EMU_ERR_EXISTS,    // already present
	EMU_ERR_NOT_FOUND, // not present
	EMU_ERR_ARG,       // invalid argument
} emu_result_t;

const char *emu_result_str(emu_result_t r);

// ---------------------------------------------------------------------------
// Emulator state

typedef enum emu_state_t {
	EMU_STATE_INITIALIZING,
	EMU_STATE_RESETTING, // /RESET asserted, or reset requested
	EMU_STATE_RUNNING,
	EMU_STATE_WAITING, // executed WAI, waiting for an interrupt
	EMU_STATE_HALTED,  // stopped by user, breakpoint, illegal opcode or after stepping
	EMU_STATE_LOADING, // host is loading a ROM image
} emu_state_t;

emu_state_t emu_get_state(void);

// Name of the state as shown by the 'status' command ("s_running", ...).
// These names are parsed by the web interface; keep them stable.
const char *emu_state_name(emu_state_t s);

// ---------------------------------------------------------------------------
// Events from the emulator core

typedef enum emu_event_type_t {
	EMU_EV_HALTED = 1, // CPU stopped; see reason
	EMU_EV_RESET,      // entered reset (pin or command)
} emu_event_type_t;

typedef enum emu_halt_reason_t {
	EMU_HALT_USER = 0,   // 'halt' command
	EMU_HALT_BREAKPOINT, // breakpoint at pc (not yet executed)
	EMU_HALT_ILLEGAL,    // unimplemented opcode at prev_pc
	EMU_HALT_HCF,        // HCF ($DD) executed at prev_pc
	EMU_HALT_STEP_DONE,  // requested number of steps completed
} emu_halt_reason_t;

typedef struct emu_event_t {
	uint8_t type;   // emu_event_type_t
	uint8_t reason; // emu_halt_reason_t for EMU_EV_HALTED
	uint8_t opcode; // opcode at prev_pc for ILLEGAL/HCF
	uint8_t _pad;
	uint16_t pc;      // PC of the next instruction to execute
	uint16_t prev_pc; // address of the last instruction executed
} emu_event_t;

// Returns false if no event is pending.
bool emu_poll_event(emu_event_t *ev);

// Number of events dropped because the event queue was full.
uint32_t emu_events_dropped(void);

// ---------------------------------------------------------------------------
// Execution control. These are acknowledged by the emulator core before
// returning. They fail with EMU_ERR_LOCKED while emu_lock() is held.

emu_result_t emu_run(void);   // from halted/resetting: start executing
emu_result_t emu_halt(void);  // stop executing (E clock stops)
emu_result_t emu_reset(void); // reload PC from the reset vector

// Execute `count` instructions, then halt with EMU_HALT_STEP_DONE.
// Only valid while halted. Returns once the request is accepted; completion is
// signalled by an EMU_EV_HALTED event (STEP_DONE, or BREAKPOINT/ILLEGAL/HCF if
// one of those stops it early). Use emu_step_wait() to block until then.
emu_result_t emu_step(uint32_t count);

// Block until the emulator is halted with no steps outstanding, or timeout.
emu_result_t emu_step_wait(uint32_t timeout_ms);

// Put the emulator in LOADING state while a ROM image is written. Leave it
// with emu_reset().
emu_result_t emu_load_begin(void);

// ---------------------------------------------------------------------------
// Parking the emulator core for direct access (nests).
//
// While locked the emulator executes no instructions but its state machine
// state is unchanged and, if it was running, the E clock keeps running; on
// unlock it catches up. Keep lock periods short while running.

emu_result_t emu_lock(void);
void emu_unlock(void);
bool emu_is_locked(void);

// ---------------------------------------------------------------------------
// Registers

typedef struct emu_regs_t {
	uint16_t pc; // next instruction
	uint16_t x;
	uint16_t sp;
	uint8_t a;
	uint8_t b;
	uint8_t ccr;
	uint16_t prev_pc; // last instruction executed
} emu_regs_t;

typedef enum emu_reg_t {
	EMU_REG_PC,
	EMU_REG_A,
	EMU_REG_B,
	EMU_REG_X,
	EMU_REG_SP,
	EMU_REG_CCR,
} emu_reg_t;

emu_result_t emu_get_regs(emu_regs_t *regs);
emu_result_t emu_set_reg(emu_reg_t reg, uint16_t value);

// Parse "pc", "a", "b", "x", "sp", "ccr" (case-insensitive). Returns false if unknown.
bool emu_reg_from_name(const char *name, emu_reg_t *reg);
const char *emu_reg_name(emu_reg_t reg);

// ---------------------------------------------------------------------------
// Memory

// Read/write through the emulator's memory map: ROM and RAM come from the
// shadow copies, everything else is a real bus cycle (the E clock is started
// temporarily if needed). Writes to ROM are ignored, as on the target.
emu_result_t emu_mem_read(uint16_t addr, uint8_t *buf, size_t len);
emu_result_t emu_mem_write(uint16_t addr, const uint8_t *buf, size_t len);

// Real bus cycles regardless of the memory map.
emu_result_t emu_bus_read(uint16_t addr, uint8_t *buf, size_t len);
emu_result_t emu_bus_write(uint16_t addr, const uint8_t *buf, size_t len);

// ---------------------------------------------------------------------------
// Breakpoints (PC = address of an instruction's opcode)

emu_result_t emu_bp_add(uint16_t addr);
emu_result_t emu_bp_remove(uint16_t addr);
emu_result_t emu_bp_clear(void);
// Copies up to `max` breakpoint addresses into out[]; returns how many are set.
size_t emu_bp_list(uint16_t *out, size_t max);
size_t emu_bp_max(void);

// ---------------------------------------------------------------------------
// Status snapshot

typedef struct emu_status_t {
	emu_state_t state;
	emu_regs_t regs;
	bool halted;
	bool running;
	uint64_t instruction_count;
	uint32_t cycle_count;   // emulated E cycles since last E clock start
	uint32_t pio_cycles;    // real E cycles since last E clock start
	int32_t cycle_overage;  // emulator behind real time (credit)
	int32_t cycle_underage; // total cycles spent waiting for real time
	bool eclock_running;
	bool eclock_external;
	uint32_t steps_pending;
} emu_status_t;

emu_result_t emu_get_status(emu_status_t *st);

// ---------------------------------------------------------------------------
// Setup (called once on the host core before the emulator core starts)

void emu_api_init(void);

// Called by the host while it waits for the emulator core to answer. The
// default implementation busy-waits briefly; a front end can override this
// (it is weak) to keep its own event loop serviced.
void emu_host_idle(void);
