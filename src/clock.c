/**
 * MC6800 E Clock Generation Implementation
 */

#include "clock.h"
#include "cpu_state.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include <stdio.h>

// PIO instance and state machine
static uint eclock_offset;  // E_SM
static uint sync_offset;    // SYNC_SM

// Cycle counter (non-volatile for performance - not accessed by ISRs)
uint32_t cycle_count = 0;

// Pending cycles for fast-path accumulation (non-volatile for performance)
uint32_t pending_cycles = 0;

// Overage counter (cycles we're ahead of real time)
int32_t cycle_overage = 0;

// Last known PIO cycles value (cached when SM is stopped)
static uint32_t last_pio_cycles = 0;

// Initialize E clock
void eclock_init(void) {
    // Load PIO program and Initialize state machine (will be stopped)
    eclock_offset = pio_add_program(BUS_PIO, &eclock_program);
    eclock_program_init(BUS_PIO, E_SM, eclock_offset, GPIO_ECLOCK);
    
    sync_offset = pio_add_program(BUS_PIO, &sync_program);
    sync_program_init(BUS_PIO, SYNC_SM, sync_offset, GPIO_ECLOCK, GPIO_TIMING_TEST);

    // Initialize X register while stopped (sets X = 0xFFFFFFFF for countdown)
    eclock_reset_pio_counter();

    // Reset cached PIO cycles to 0
    last_pio_cycles = 0;
}

// Start E clock generation
void eclock_start(void) {
    // Reset PIO cycle counter before starting
    eclock_reset_pio_counter();

    pio_sm_set_enabled(BUS_PIO, E_SM, true);
    cycle_count = 0;
    cycle_overage = 0;
    last_pio_cycles = 0;
}

// Stop E clock generation
void eclock_stop(void) {
    // Cache current PIO cycles before stopping (non-blocking)
    eclock_get_pio_cycles();  // Updates last_pio_cycles
    // Disable PIO state machine
    pio_sm_set_enabled(BUS_PIO, E_SM, false);
    eclock_force_low();
}

// Get real elapsed E clock cycles from PIO
uint32_t __time_critical_func(eclock_get_pio_cycles)(void) {
    // Check if state machine is enabled (check CTRL register bit)
    bool sm_enabled = (BUS_PIO->ctrl & (1u << E_SM)) != 0;

    if (!sm_enabled) {
        // SM is stopped, return cached value
        return last_pio_cycles;
    }

    // Read PIO X register directly from rxf_putget register
    // The PIO program continuously updates rxfifo[0] with the X value
    // using: mov isr, x; push noblock
    // The .fifo txput directive enables rxf_putget access
    uint32_t x_value = BUS_PIO->rxf_putget[E_SM][0];

    // Invert to get elapsed cycles (0xFFFFFFFF - x_value)
    uint32_t pio_cycles = ~x_value;

    // Cache the value
    last_pio_cycles = pio_cycles;

    return pio_cycles;
}

// Wait for next E clock edge (with real-time tracking)
void __time_critical_func(bus_sync)(void) {
    // Get real elapsed cycles from PIO
    uint32_t pio_cycles = eclock_get_pio_cycles();

    // Calculate difference (positive = ahead, negative = behind)
    int32_t diff = (int32_t)(eclock_get_count() - pio_cycles);

    if (diff > 0) {
        // Emulator is ahead of real time
        // Apply overage credit first
        diff -= cycle_overage;

        if (diff > 0) {
            // Still need to wait for additional cycles
            eclock_wait_cycles((uint32_t)diff);
            cycle_overage = 0;
        } else {
            // Overage covers the difference
            cycle_overage = -diff;
        }
    } else if (diff < 0) {
        // Emulator is behind real time - accumulate overage credit
        cycle_overage += (-diff);
    }
}
