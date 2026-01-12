/**
 * MC6800 E Clock Generation
 * Uses PIO to generate precise 0.894886 MHz E clock
 */

#pragma once

#include "board_config.h"
#include "clock.pio.h"
#include "hardware/pio.h"
#include "hardware/pio_instructions.h"
#include "hardware/structs/iobank0.h"
#include "instructions.h"
#include "pico.h"
#include <stdbool.h>
#include <stdint.h>

// Global cycle counters (non-volatile for performance - not accessed by ISRs)
extern uint32_t cycle_count;
extern uint32_t pending_cycles;
extern int32_t cycle_overage;
extern int32_t cycle_underage;
extern uint32_t last_pio_cycles;

// Initialize E clock PIO
void eclock_init(void);

// Start E clock generation
void eclock_start(void);

// Stop E clock generation
bool eclock_stop(void);

static inline bool eclock_is_running(void)
{
	return ECLK_PIO->ctrl & ((1 << E_SM) << PIO_CTRL_SM_ENABLE_LSB);
}

// Should only be called when E clock is stopped
static inline void eclock_force_low(void)
{
	pio_sm_exec(ECLK_PIO, E_SM, pio_encode_set(pio_pins, 0));
}

// Reset PIO cycle counter
static inline void eclock_reset_pio_counter(void)
{
	// Write 0xFFFFFFFF to X register to reset counter
	// Execute "mov x, ~null" instruction to set X = 0xFFFFFFFF
	// The invert modifier is encoded in bit 3 (0x08) of the source field
	pio_sm_exec(ECLK_PIO, E_SM, pio_encode_mov(pio_x, 0x08 | pio_null));
}

// Accumulate cycles without GPIO polling (inline for performance)
static inline void eclock_accumulate(uint32_t cycles)
{
	pending_cycles += cycles;
}

// Synchronize accumulated cycles at end of instruction (inline for performance)
static inline void eclock_sync_instruction(void)
{
	// Just update the cycle count without GPIO polling
	// (PIO continues to generate E-clock in background for external hardware)
	cycle_count += pending_cycles;
	pending_cycles = 0;
}

// Consume N internal cycles - fast-path: accumulate without GPIO polling
// (inline for performance)
static inline void eclock_consume_cycles(uint8_t cycles)
{
	eclock_accumulate(cycles);
}

// Get cycle count (inline for performance)
static inline uint32_t eclock_get_count(void)
{
	return cycle_count;
}

// Get accumulated cycle count for debugging (inline for performance)
static inline uint32_t eclock_get_pending(void)
{
	return pending_cycles;
}

static inline void eclock_wait_cycles(uint32_t cycles)
{
	// Wait for the specified number of E clock cycles
	if (!cycles) {
		return; // no cycles to wait for
	}
	pio_sm_put(ECLK_PIO, SYNC_SM, cycles - 1);    // push cycles to FIFO
	(void)pio_sm_get_blocking(ECLK_PIO, SYNC_SM); // wait for completion
}

// Get real elapsed E clock cycles from PIO
static inline uint32_t eclock_get_pio_cycles(void)
{
	// Read PIO X register directly from rxf_putget register
	// The PIO program continuously updates rxfifo[0] with the X value
	// using: mov isr, x; push noblock
	// The .fifo txput directive enables rxf_putget access
	uint32_t x_value = ECLK_PIO->rxf_putget[E_SM][0];

	// Invert to get elapsed cycles (0xFFFFFFFF - x_value)
	return ~x_value;
}

// Wait for next E clock edge (with real-time tracking)
static inline void bus_sync(void)
{
	// get simulated cycles from the emulator
	uint32_t simulated_cycles = eclock_get_count();
	// Get real elapsed cycles from PIO
	uint32_t pio_cycles = eclock_get_pio_cycles();

	// Calculate difference (positive = ahead, negative = behind)
	int32_t diff = (int32_t)(simulated_cycles - pio_cycles);

	if (diff > 0) {
		// Emulator is ahead of real time
		// Apply overage credit first
		diff -= cycle_overage;

		if (diff > 0) {
			// Still need to wait for additional cycles
			eclock_wait_cycles((uint32_t)diff);
			cycle_overage = 0;
			cycle_underage += diff;
		} else {
			// Overage covers the difference
			cycle_overage = -diff;
		}
	} else if (diff < 0) {
		// Emulator is behind real time - accumulate overage credit
		cycle_overage += (-diff);
	}
}

static inline void clock_reset_counters(void)
{
	cycle_count = 0;
	pending_cycles = 0;
	cycle_overage = 0;
	cycle_underage = 0;
	last_pio_cycles = 0;
	eclock_reset_pio_counter();
#if COUNT_INSTRUCTIONS
	instruction_count_initialize();
#endif
}
