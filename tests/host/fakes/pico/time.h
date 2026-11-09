// Host-test stand-in for pico/time.h
#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef uint64_t absolute_time_t;
uint64_t fake_time_us(void);
static inline absolute_time_t make_timeout_time_us(uint64_t us) { return fake_time_us() + us; }
static inline absolute_time_t make_timeout_time_ms(uint32_t ms) { return fake_time_us() + 1000ull * ms; }
static inline bool time_reached(absolute_time_t t) { return fake_time_us() >= t; }
void busy_wait_us_32(uint32_t us);
