// Host-test stand-in for debug_spi.h
#pragma once
#include <stdbool.h>
extern bool fake_debug_spi_on;
static inline void debug_spi_log(void) {}
static inline void debug_spi_enable(bool on) { fake_debug_spi_on = on; }
static inline bool debug_spi_is_enabled(void) { return fake_debug_spi_on; }
