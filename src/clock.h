/**
 * MC6800 E Clock Generation
 * Uses PIO to generate precise 0.894886 MHz E clock
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

// E clock output pin is defined in board_config.h as GPIO_ECLOCK

// Global cycle counters (non-volatile for performance - not accessed by ISRs)
extern uint32_t cycle_count;
extern uint32_t pending_cycles;

// Initialize E clock PIO
void eclock_init(void);

// Start E clock generation
void eclock_start(void);

// Stop E clock generation
void eclock_stop(void);

// Wait for E clock rising edge
void eclock_wait_high(void);

// Wait for E clock falling edge
void eclock_wait_low(void);

// Accumulate cycles without GPIO polling (inline for performance)
static inline void eclock_accumulate(uint32_t cycles) {
    pending_cycles += cycles;
}

// Synchronize accumulated cycles at end of instruction (inline for performance)
static inline void eclock_sync_instruction(void) {
    // Just update the cycle count without GPIO polling
    // (PIO continues to generate E-clock in background for external hardware)
    cycle_count += pending_cycles;
    pending_cycles = 0;
}

// Consume N internal cycles - fast-path: accumulate without GPIO polling (inline for performance)
static inline void eclock_consume_cycles(uint8_t cycles) {
    eclock_accumulate(cycles);
}

// Get cycle count (inline for performance)
static inline uint32_t eclock_get_count(void) {
    return cycle_count;
}

// Get accumulated cycle count for debugging (inline for performance)
static inline uint32_t eclock_get_pending(void) {
    return pending_cycles;
}

#endif // CLOCK_H
