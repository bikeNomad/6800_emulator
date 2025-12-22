/**
 * MC6800 E Clock Generation
 * Uses PIO to generate precise 0.894886 MHz E clock
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "pico.h"
#include "hardware/pio.h"
#include "clock.pio.h"
#include "board_config.h"
#include "hardware/pio_instructions.h"
#include "hardware/structs/iobank0.h"

// E clock output pin is defined in board_config.h as GPIO_ECLOCK

// Read E clock state from OUTTOPAD register (what PIO is driving)
// This is independent of external pin loading or shorting
static inline bool eclock_read_outtopad(void) {
    // Read GPIO STATUS register OUTTOPAD bit (bit 9)
    // This shows what the PIO is driving, not what the pin voltage is
    return (iobank0_hw->io[GPIO_ECLOCK].status & IO_BANK0_GPIO0_STATUS_OUTTOPAD_BITS) != 0;
}

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

static inline bool eclock_is_running(void) {
    return BUS_PIO->ctrl & ((1 << E_SM) << PIO_CTRL_SM_ENABLE_LSB);
}

// Should only be called when E clock is stopped
static inline void eclock_force_low(void) {
    pio_sm_exec(BUS_PIO, E_SM, pio_encode_set(pio_pins, 0));
}

// Get real elapsed E clock cycles from PIO
uint32_t eclock_get_pio_cycles(void);

// Reset PIO cycle counter
static inline void eclock_reset_pio_counter(void) {
    // Write 0xFFFFFFFF to X register to reset counter
    // Execute "mov x, ~null" instruction to set X = 0xFFFFFFFF
    // The invert modifier is encoded in bit 3 (0x08) of the source field
    pio_sm_exec(BUS_PIO, E_SM, pio_encode_mov(pio_x, 0x08 | pio_null));
}

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

// Wait for next E clock edge (for synchronization)
void bus_sync(void);

static inline void eclock_wait_cycles(uint32_t cycles) {
    // Wait for the specified number of E clock cycles
    pio_sm_put(BUS_PIO, SYNC_SM, cycles);  // push cycles to FIFO
    // wait until stalled again
    while (!pio_sm_is_exec_stalled(BUS_PIO, SYNC_SM)) {
        tight_loop_contents();
    }
}


#endif // CLOCK_H
