#pragma once

#include "pico.h"
#include "pico/multicore.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
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

// SM Events
typedef enum sm_event_t {
	// Commands from USB CDC
	EV_CMD_RUN = EVT_USER_START,
	EV_CMD_HALT,
	EV_CMD_RESET,
	EV_CMD_LOAD,
	EV_PAUSE_EMULATOR, // freeze emulation
	EV_RESUME_EMULATOR,
} sm_event_t;

extern const char *event_names[];

// Notifications to USB CDC
typedef enum sm_notification_t {
	NOTIF_OK,
	NOTIF_ERROR,
} sm_notification_t;

// Returns false if the queue is full
bool post_sm_event(sm_event_t event);

// Returns false if no notification in queue (yet)
bool receive_sm_notification(sm_notification_t *notification);

// Runs until an exception of some sort
bool run_emulator_sm(void);

const char *sm_current_state_name(void);

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