/**
 * MC6800 E Clock Generation Implementation
 */

#include "clock.h"
#include "cpu_state.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico.h"
#include "clock.pio.h"
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

// Last known E clock state
static bool last_e_state = false;

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
    last_e_state = false;
    printf("E clock started\n");
}

// Stop E clock generation
void eclock_stop(void) {
    // Cache current PIO cycles before stopping (non-blocking)
    eclock_get_pio_cycles();  // Updates last_pio_cycles

    // Disable PIO state machine
    pio_sm_set_enabled(BUS_PIO, E_SM, false);
    
    eclock_force_low();

    printf("E clock stopped (PIO cycles: %lu, E forced LOW)\n", (unsigned long)last_pio_cycles);
}

// Wait for E clock rising edge
void eclock_wait_high(void) {
    // Poll E clock OUTTOPAD (what PIO is driving) until it goes high
    // Using OUTTOPAD instead of gpio_get() makes this independent of external pin loading
    // Exit early if CPU is halted to prevent hanging
    while (!eclock_read_outtopad() && !cpu.halted) {
        tight_loop_contents();
    }
    last_e_state = true;
}

// Wait for E clock falling edge
void eclock_wait_low(void) {
    // Wait until E is high (if not already)
    // Using OUTTOPAD instead of gpio_get() makes this independent of external pin loading
    // Exit early if CPU is halted to prevent hanging
    while (!eclock_read_outtopad() && !cpu.halted) {
        tight_loop_contents();
    }
    // Now wait for the falling edge
    // Exit early if CPU is halted to prevent hanging
    while (eclock_read_outtopad() && !cpu.halted) {
        tight_loop_contents();
    }
    last_e_state = false;
    // Note: cycle_count is now tracked by PIO X register, not incremented here
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

// Reset PIO cycle counter
void eclock_reset_pio_counter(void) {
    // Write 0xFFFFFFFF to X register to reset counter
    // Execute "mov x, ~null" instruction to set X = 0xFFFFFFFF
    // The invert modifier is encoded in bit 3 (0x08) of the source field
    pio_sm_exec(BUS_PIO, E_SM, pio_encode_mov(pio_x, 0x08 | pio_null));
}

void eclock_force_low(void) {
    // force E clock low
    pio_sm_exec(BUS_PIO, E_SM, pio_encode_set(pio_pins, 0));
}

// Check timing and wait if emulator is ahead of real-time
void __time_critical_func(eclock_check_timing)(void) {
    // Get real elapsed cycles from PIO
    uint32_t pio_cycles = eclock_get_pio_cycles();

    // Calculate difference (positive = ahead, negative = behind)
    int32_t diff = (int32_t)(cycle_count - pio_cycles);

    if (diff > 0) {
        // Emulator is ahead of real time - need to wait
        // Apply overage credit first
        diff -= cycle_overage;

        if (diff > 0) {
            // Still need to wait for additional cycles
            for (int32_t i = 0; i < diff; i++) {
                eclock_wait_low();
            }
            cycle_overage = 0;
        } else {
            // Overage covers the difference
            cycle_overage = -diff;
        }
    } else if (diff < 0) {
        // Emulator is behind real time - accumulate overage credit
        // (no waiting, just credit for future)
        cycle_overage += (-diff);
    }
    // If diff == 0, we're exactly on time - no action needed
}

// Note: eclock_consume_cycles, eclock_accumulate, eclock_sync_instruction,
// eclock_get_pending, and eclock_get_count are now inline in clock.h

// (Functions removed - now inline for performance)
