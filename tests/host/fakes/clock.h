// Host-test stand-in for clock.h: no real E clock, just bookkeeping
#pragma once
#include <stdbool.h>
#include <stdint.h>
typedef enum { ECLOCK_INTERNAL, ECLOCK_EXTERNAL } eclock_mode_t;
extern uint32_t cycle_count, pending_cycles, last_pio_cycles;
extern int32_t cycle_overage, cycle_underage;
extern bool fake_eclock_running;
void eclock_start(void);
bool eclock_stop(void);
static inline bool eclock_is_running(void) { return fake_eclock_running; }
static inline uint32_t eclock_get_count(void) { return cycle_count; }
static inline uint32_t eclock_get_pio_cycles(void) { return cycle_count; }
static inline eclock_mode_t eclock_get_mode(void) { return ECLOCK_INTERNAL; }
static inline void bus_sync(void) {}
