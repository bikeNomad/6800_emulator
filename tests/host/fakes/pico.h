// Host-test stand-in for the Pico SDK's pico.h
#pragma once
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#define __time_critical_func(x) x
#define __not_in_flash_func(x) x
#define __force_inline inline __attribute__((always_inline))
#define _ATTRIBUTE(x) __attribute__(x)
typedef unsigned int uint;
static inline void tight_loop_contents(void) { sched_yield(); }
