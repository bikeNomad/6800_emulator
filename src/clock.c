/**
 * MC6800 E Clock Generation Implementation
 */

#include "clock.h"
#include "cpu_state.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include <stdio.h>

// PIO instance and state machine
static int eclock_offset; // E_SM
static int sync_offset;   // SYNC_SM

// Cycle counter (non-volatile for performance - not accessed by ISRs)
uint32_t cycle_count = 0;

// Pending cycles for fast-path accumulation (non-volatile for performance)
uint32_t pending_cycles = 0;

// Overage counter (cycles we're ahead of real time)
int32_t cycle_overage = 0;

int32_t cycle_underage = 0;

// Last known PIO cycles value (cached when SM is stopped)
uint32_t last_pio_cycles = 0;

// Initialize E clock
void eclock_init(void) {
    pio_clear_instruction_memory(ECLK_PIO);

    // Load PIO program and Initialize state machine (will be stopped)
    eclock_offset = pio_add_program(ECLK_PIO, &eclock_program);
    if (eclock_offset < 0) {
        printf("Failed to add E clock program to PIO\r\n");
        return;
    }
    eclock_program_init(ECLK_PIO, E_SM, eclock_offset, GPIO_ECLOCK);

    sync_offset = pio_add_program(ECLK_PIO, &sync_program);
    if (sync_offset < 0) {
        printf("Failed to add sync program to PIO\r\n");
        return;
    }
    sync_program_init(ECLK_PIO, SYNC_SM, sync_offset, GPIO_ECLOCK, GPIO_TIMING_TEST);

    // Initialize X register while stopped (sets X = 0xFFFFFFFF for countdown)
    eclock_reset_pio_counter();

    // Reset cached PIO cycles to 0
    last_pio_cycles = 0;
}

// Start E clock generation
void eclock_start(void) {
    // Reset PIO cycle counter before starting
    eclock_reset_pio_counter();

    pio_sm_set_enabled(ECLK_PIO, E_SM, true);
    cycle_count = 0;
    cycle_overage = 0;
    cycle_underage = 0;
    last_pio_cycles = 0;
}

// Stop E clock generation
bool eclock_stop(void) {
    if (!eclock_is_running()) {
        return false; // already stopped
    }
    // Cache current PIO cycles before stopping (non-blocking)
    last_pio_cycles = eclock_get_pio_cycles(); // Update last_pio_cycles
    // Disable PIO state machine
    pio_sm_set_enabled(ECLK_PIO, E_SM, false);
    eclock_force_low();
    return true;
}
