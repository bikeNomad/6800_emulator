/**
 * MC6800 E Clock Generation Implementation
 */

#include "clock.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "clock.pio.h"
#include <stdio.h>

// PIO instance and state machine
static PIO pio = pio0;
static uint sm = 0;
static uint offset;

// Cycle counter (non-volatile for performance - not accessed by ISRs)
uint32_t cycle_count = 0;

// Pending cycles for fast-path accumulation (non-volatile for performance)
uint32_t pending_cycles = 0;

// Overage counter (cycles we're ahead of real time)
int32_t cycle_overage = 0;

// Last known PIO cycles value (cached when SM is stopped)
static uint32_t last_pio_cycles = 0;

// Last known E clock state
static volatile bool last_e_state = false;

// Initialize E clock
void eclock_init(void) {
    // Load PIO program
    offset = pio_add_program(pio, &eclock_program);

    // Initialize state machine (this starts it by default)
    eclock_program_init(pio, sm, offset, GPIO_ECLOCK);

    // Stop immediately (will be started when CPU runs)
    pio_sm_set_enabled(pio, sm, false);

    // Initialize X register while stopped (sets X = 0xFFFFFFFF for countdown)
    eclock_reset_pio_counter();

    // Reset cached PIO cycles to 0
    last_pio_cycles = 0;

    // Verify SM is actually stopped
    bool is_enabled = (pio->ctrl & (1u << sm)) != 0;
    printf("E clock initialized on GPIO %d (%s)\n", GPIO_ECLOCK,
           is_enabled ? "ERROR: STILL RUNNING!" : "stopped");
    printf("Target frequency: 0.894886 MHz (period: 1.117µs)\n");
}

// Start E clock generation
void eclock_start(void) {
    // Reset PIO cycle counter before starting
    eclock_reset_pio_counter();

    pio_sm_set_enabled(pio, sm, true);
    cycle_count = 0;
    cycle_overage = 0;
    last_pio_cycles = 0;
    last_e_state = false;
    printf("E clock started\n");
}

// Stop E clock generation
void eclock_stop(void) {
    // Check if state machine is enabled (check CTRL register bit)
    bool sm_enabled = (pio->ctrl & (1u << sm)) != 0;

    // Cache current PIO cycles before stopping
    if (sm_enabled) {
        // Read current value before stopping
        pio_sm_exec(pio, sm, pio_encode_in(pio_x, 32));
        pio_sm_exec(pio, sm, pio_encode_push(false, false));
        uint32_t x_value = pio_sm_get_blocking(pio, sm);
        last_pio_cycles = ~x_value;
    }

    pio_sm_set_enabled(pio, sm, false);
    printf("E clock stopped (PIO cycles: %lu)\n", (unsigned long)last_pio_cycles);
}

// Wait for E clock rising edge
void eclock_wait_high(void) {
    // Poll E clock pin until it goes high
    while (!gpio_get(GPIO_ECLOCK)) {
        tight_loop_contents();
    }
    last_e_state = true;
}

// Wait for E clock falling edge
void eclock_wait_low(void) {
    // Wait until E is high (if not already)
    while (!gpio_get(GPIO_ECLOCK)) {
        tight_loop_contents();
    }
    // Now wait for the falling edge
    while (gpio_get(GPIO_ECLOCK)) {
        tight_loop_contents();
    }
    last_e_state = false;
    // Note: cycle_count is now tracked by PIO X register, not incremented here
}

// Get real elapsed E clock cycles from PIO
uint32_t eclock_get_pio_cycles(void) {
    // Check if state machine is enabled (check CTRL register bit)
    bool sm_enabled = (pio->ctrl & (1u << sm)) != 0;

    if (!sm_enabled) {
        // SM is stopped, return cached value
        return last_pio_cycles;
    }

    // Read PIO X register by temporarily moving it to ISR and pushing to FIFO
    // Move X to ISR (in source, bits = 32)
    pio_sm_exec(pio, sm, pio_encode_in(pio_x, 32));

    // Push ISR to FIFO
    pio_sm_exec(pio, sm, pio_encode_push(false, false));

    // Read from FIFO (this is the X value, counting down from 0xFFFFFFFF)
    uint32_t x_value = pio_sm_get_blocking(pio, sm);

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
    pio_sm_exec(pio, sm, pio_encode_mov(pio_x, 0x08 | pio_null));
}

// Check timing and wait if emulator is ahead of real-time
void eclock_check_timing(void) {
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
