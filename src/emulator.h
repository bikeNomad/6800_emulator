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

#pragma once

#include "pico.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "emu_api.h"
#include "emu_ring.h"
#include "simple_fsm.h" // for EVT_USER_START
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef int (*printf_func_t)(const char *__restrict, ...)
	_ATTRIBUTE((__format__(__printf__, 1, 2)));

// Enable instruction counting for profiling (set to 0 to disable)
#define COUNT_INSTRUCTIONS 1

// Debug output control (set via CMakeLists.txt)
#ifndef DEBUG_INTERRUPTS
#define DEBUG_INTERRUPTS 1
#endif

// Breakpoint system
#define MAX_BREAKPOINTS 16

// System clock speed in MHz (configurable at build time)
#ifndef SYS_CLOCK_MHZ
#define SYS_CLOCK_MHZ 266 // Default: 266MHz (optimized for 133MHz QSPI flash)
#endif

// QSPI flash interface speed divisor (configurable at build time)
#ifndef QSPI_CLOCK_DIVISOR
#define QSPI_CLOCK_DIVISOR 2 // Default: system clock / 2 (133MHz with 266MHz sys clock)
#endif

// ---------------------------------------------------------------------------
// Internal interface between the host-side API (emu_api.c) and the emulator
// core state machine (emulator.c). Front ends use emu_api.h instead.

// Commands, host core -> emulator core
typedef enum emu_cmd_t {
	EMU_CMD_RUN = EVT_USER_START,
	EMU_CMD_HALT,
	EMU_CMD_RESET,
	EMU_CMD_LOAD,
	EMU_CMD_PAUSE, // park the emulator core until EMU_CMD_RESUME
	EMU_CMD_RESUME,
	EMU_CMD_STEP, // arg = instruction count
} emu_cmd_t;

typedef struct emu_cmd_msg_t {
	uint8_t cmd; // emu_cmd_t
	uint8_t seq; // echoed in the reply
	uint16_t _pad;
	uint32_t arg;
} emu_cmd_msg_t;

// Replies, emulator core -> host core (one per command)
typedef struct emu_reply_msg_t {
	uint8_t seq;
	uint8_t result; // emu_result_t
} emu_reply_msg_t;

EMU_RING_DECLARE(emu_cmd_ring, emu_cmd_msg_t, 8)
EMU_RING_DECLARE(emu_reply_ring, emu_reply_msg_t, 8)
EMU_RING_DECLARE(emu_event_ring, emu_event_t, 64)

// Ring instances (defined in emulator.c)
extern emu_cmd_ring_t emu_cmd_q;     // host -> core
extern emu_reply_ring_t emu_reply_q; // core -> host
extern emu_event_ring_t emu_event_q; // core -> host
extern volatile uint32_t emu_event_drops;

// Current state, written only by the emulator core
extern volatile emu_state_t emu_core_state;

// Emulator core main loop. Runs until the state machine terminates.
bool run_emulator_sm(void);

// Number of single steps still to execute (0 when not stepping)
extern volatile uint32_t emu_steps_pending;

// Call this from where you want the debugger to start
// Change the JLink reset type to 12
// see https://kb.segger.com/Reset_and_Halt_After_Bootloader
static inline void jlink_reset_strategy_12_marker(void)
{
	asm volatile("mov r0, #8     \n\t" // 1. Move immediate value 8 into register r0
		     "ldr r0, [r0]   \n\t" // 2. Load word from memory address in r0 into r0
		     "nop            \n\t" // 3. No operation
		     "nop            \n\t" // 4. No operation
		     :                     // No output operands
		     :                     // No input operands
		     : "r0", "memory"      // Clobber list: tell GCC we used r0 and memory
	);
}