/**
 * Bus Timing Configuration
 * Provides dynamic timing calculation based on SYS_CLOCK_MHZ
 */

#ifndef BUS_TIMING_H
#define BUS_TIMING_H

#include <stdint.h>
#include "hardware/clocks.h"

// Target timing requirements (in nanoseconds)
#define BUS_DATA_SETUP_TIME_NS 300  // Exceeds MC6821 PIA 290ns requirement
#define BUS_DATA_HOLD_TIME_NS  300  // Same as setup for consistency

// Calculate PIO cycles needed for target delay
// Returns the number of PIO cycles required to achieve the target delay
// at the current system clock frequency
static inline uint32_t calculate_pio_cycles_for_ns(uint32_t target_ns) {
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);

    // Convert nanoseconds to cycles: (target_ns * sys_clock_hz) / 1000000000
    uint64_t cycles = ((uint64_t)target_ns * sys_clock_hz) / 1000000000;

    // Ensure minimum delay of 1 cycle
    if (cycles < 1) {
        cycles = 1;
    }

    return (uint32_t)cycles;
}

// Get current system clock frequency in MHz
static inline uint32_t get_sys_clock_mhz(void) {
    return clock_get_hz(clk_sys) / 1000000;
}

// Print timing configuration for debugging
static inline void print_timing_config(void) {
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);
    uint32_t sys_clock_mhz = sys_clock_hz / 1000000;
    float cycle_time_ns = 1000.0f / sys_clock_mhz;

    uint32_t setup_cycles = calculate_pio_cycles_for_ns(BUS_DATA_SETUP_TIME_NS);
    uint32_t hold_cycles = calculate_pio_cycles_for_ns(BUS_DATA_HOLD_TIME_NS);

    float actual_setup_ns = setup_cycles * cycle_time_ns;
    float actual_hold_ns = hold_cycles * cycle_time_ns;

    printf("Bus Timing Configuration:\n");
    printf("  System Clock: %lu MHz\n", (unsigned long)sys_clock_mhz);
    printf("  Cycle Time: %.3f ns\n", cycle_time_ns);
    printf("  Setup Delay: %lu cycles = %.1f ns (target: %d ns)\n",
           (unsigned long)setup_cycles, actual_setup_ns, BUS_DATA_SETUP_TIME_NS);
    printf("  Hold Delay: %lu cycles = %.1f ns (target: %d ns)\n",
           (unsigned long)hold_cycles, actual_hold_ns, BUS_DATA_HOLD_TIME_NS);
}

#endif // BUS_TIMING_H
