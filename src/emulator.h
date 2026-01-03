#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "pico.h"
#include "pico/stdlib.h"
#include "pico/util/queue.h"
#include "pico/multicore.h"

#include "simple_fsm.h"

typedef int	(*printf_func_t)(const char *__restrict, ...)
               _ATTRIBUTE ((__format__ (__printf__, 1, 2)));

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
#define SYS_CLOCK_MHZ 266  // Default: 266MHz (optimized for 133MHz QSPI flash)
#endif

// QSPI flash interface speed divisor (configurable at build time)
#ifndef QSPI_CLOCK_DIVISOR
#define QSPI_CLOCK_DIVISOR 2  // Default: system clock / 2 (133MHz with 266MHz sys clock)
#endif

// SM Events
typedef enum sm_event_t {
  // Commands from USB CDC
  EV_CMD_RUN = EVT_USER_START,
  EV_CMD_HALT,
  EV_CMD_RESET,
  EV_CMD_BOOTLOADER,
  EV_CMD_READ,
  EV_CMD_WRITE,
  EV_CMD_LOAD,
  EV_PAUSE_EMULATOR,  // freeze emulation
  EV_RESUME_EMULATOR,

  // Events from hardware
  EV_IRQ_ASSERTED,
  EV_NMI_ASSERTED,
  EV_RESET_ASSERTED,

  // Events from emulated instructions
  EV_WAI,
  EV_UNIMPLEMENTED_OPCODE,  // halts
  EV_HCF, // halt and catch fire
  EV_BREAKPOINT_HIT,
} sm_event_t;

typedef enum sm_notification_t {
  // Notifications to USB CDC
  NOTIF_OK,
  NOTIF_ERROR,
  NOTIF_STATUS,
} sm_notification_t;

bool post_sm_event(sm_event_t event);
bool receive_sm_notification(sm_notification_t *notification);
bool run_emulator_sm(void);