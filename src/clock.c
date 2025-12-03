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

// Cycle counter
static volatile uint32_t cycle_count = 0;

// Last known E clock state
static volatile bool last_e_state = false;

// Initialize E clock
void eclock_init(void) {
    // Load PIO program
    offset = pio_add_program(pio, &eclock_program);

    // Initialize state machine
    eclock_program_init(pio, sm, offset, GPIO_ECLOCK);

    // Stop initially (will be started after configuration)
    pio_sm_set_enabled(pio, sm, false);

    printf("E clock initialized on GPIO %d\n", GPIO_ECLOCK);
    printf("Target frequency: 0.894886 MHz (period: 1.117µs)\n");
}

// Start E clock generation
void eclock_start(void) {
    pio_sm_set_enabled(pio, sm, true);
    cycle_count = 0;
    last_e_state = false;
    printf("E clock started\n");
}

// Stop E clock generation
void eclock_stop(void) {
    pio_sm_set_enabled(pio, sm, false);
    printf("E clock stopped\n");
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
    // If currently high, wait for fall
    if (last_e_state) {
        while (gpio_get(GPIO_ECLOCK)) {
            tight_loop_contents();
        }
        cycle_count++;
    }
    // Now wait for low
    while (!gpio_get(GPIO_ECLOCK)) {
        tight_loop_contents();
    }
    // Then wait for the falling edge
    while (gpio_get(GPIO_ECLOCK)) {
        tight_loop_contents();
    }
    last_e_state = false;
    cycle_count++;
}

// Get cycle count
uint32_t eclock_get_count(void) {
    return cycle_count;
}
