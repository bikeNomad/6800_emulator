/**
 * MC6800 E Clock Generation
 * Uses PIO to generate precise 0.894886 MHz E clock
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"
#include "hardware/structs/iobank0.h"

// E clock output pin is defined in board_config.h as GPIO_ECLOCK

// Read E clock state from OUTTOPAD register (what PIO is driving)
// This is independent of external pin loading or shorting
static inline bool eclock_read_outtopad(void) {
    // Read GPIO STATUS register OUTTOPAD bit (bit 9)
    // This shows what the PIO is driving, not what the pin voltage is
    return (iobank0_hw->io[GPIO_ECLOCK].status & IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS) != 0;
}

// Test pin definitions are now in board_config.h:
// - GPIO_TIMING_TEST (GPIO 42 on NED_SYS7)
// - TIMING_TEST_ENABLED (1 on NED_SYS7, 0 on PICO2)

// Global cycle counters (non-volatile for performance - not accessed by ISRs)
extern uint32_t cycle_count;
extern uint32_t pending_cycles;
extern int32_t cycle_overage;

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

// Get real elapsed E clock cycles from PIO
uint32_t eclock_get_pio_cycles(void);

// Reset PIO cycle counter
void eclock_reset_pio_counter(void);

// Check timing and wait if emulator is ahead of real-time
void eclock_check_timing(void);

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

    // Periodically check timing to prevent running too far ahead
    // Check every 32 cycles (approx 32us)
    if ((cycle_count & 0x1F) == 0) {
        eclock_check_timing();
    }
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
