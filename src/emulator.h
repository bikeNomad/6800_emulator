#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "pico.h"
#include "pico/stdlib.h"

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
