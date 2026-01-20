/**
 * MC6800 E Clock Generation Implementation
 */

#include "clock.h"
#include "cpu_state.h"
#include "hardware/clocks.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <stdio.h>

// PIO instance and state machine
static int eclock_offset;          // Internal E clock program
static int eclock_external_offset; // External E clock program
static int sync_offset;            // SYNC_SM

// Current E clock mode
static eclock_mode_t current_mode = ECLOCK_INTERNAL;

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
void eclock_init(void)
{
	pio_clear_instruction_memory(ECLK_PIO);

	// Load internal E clock program (generates clock as output)
	eclock_offset = pio_add_program(ECLK_PIO, &eclock_program);
	if (eclock_offset < 0) {
		printf("Failed to add E clock program to PIO\r\n");
		return;
	}

	// Load external E clock program (counts external clock as input)
	eclock_external_offset = pio_add_program(ECLK_PIO, &eclock_external_program);
	if (eclock_external_offset < 0) {
		printf("Failed to add external E clock program to PIO\r\n");
		return;
	}

	// Load sync program
	sync_offset = pio_add_program(ECLK_PIO, &sync_program);
	if (sync_offset < 0) {
		printf("Failed to add sync program to PIO\r\n");
		return;
	}
	sync_program_init(ECLK_PIO, SYNC_SM, sync_offset, GPIO_ECLOCK, GPIO_TIMING_TEST);

	// Initialize with internal mode (default)
	current_mode = ECLOCK_INTERNAL;
	eclock_program_init(ECLK_PIO, E_SM, eclock_offset, GPIO_ECLOCK);

	// Initialize X register while stopped (sets X = 0xFFFFFFFF for countdown)
	eclock_reset_pio_counter();

	// Reset cached PIO cycles to 0
	last_pio_cycles = 0;
}

// Start E clock generation
void eclock_start(void)
{
	// Reset PIO cycle counter before starting
	eclock_reset_pio_counter();

	pio_sm_set_enabled(ECLK_PIO, E_SM, true);
	cycle_count = 0;
	cycle_overage = 0;
	cycle_underage = 0;
	last_pio_cycles = 0;
}

// Stop E clock generation
bool eclock_stop(void)
{
	if (!eclock_is_running()) {
		return false; // already stopped
	}
	// Cache current PIO cycles before stopping (non-blocking)
	last_pio_cycles = eclock_get_pio_cycles(); // Update last_pio_cycles
	// Disable PIO state machine
	pio_sm_set_enabled(ECLK_PIO, E_SM, false);
	// Only force low in internal mode (when we control the pin)
	if (current_mode == ECLOCK_INTERNAL) {
		eclock_force_low();
	}
	return true;
}

// Get current E clock mode
eclock_mode_t eclock_get_mode(void)
{
	return current_mode;
}

// Set E clock mode (internal or external)
// Must be called when E clock is stopped
void eclock_set_mode(eclock_mode_t mode)
{
	if (eclock_is_running()) {
		printf("Error: Cannot change E clock mode while running\r\n");
		return;
	}

	if (mode == current_mode) {
		return; // Already in requested mode
	}

	current_mode = mode;

	if (mode == ECLOCK_INTERNAL) {
		// Reinitialize for internal clock generation (output)
		eclock_program_init(ECLK_PIO, E_SM, eclock_offset, GPIO_ECLOCK);
	} else {
		// Initialize for external clock counting (input)
		eclock_external_program_init(ECLK_PIO, E_SM, eclock_external_offset, GPIO_ECLOCK);
	}

	// Reset the cycle counter
	eclock_reset_pio_counter();
	last_pio_cycles = 0;
}

// Auto-detect external E clock at startup
// Returns true if external clock detected, false if using internal
bool eclock_auto_detect(void)
{
	// Detection parameters
	const uint32_t detection_window_us = 100; // 100µs window
	const uint32_t min_cycles_threshold = 10; // Minimum cycles to consider valid

	// Ensure we start stopped
	if (eclock_is_running()) {
		eclock_stop();
	}

	// Configure for external clock detection
	eclock_set_mode(ECLOCK_EXTERNAL);
	eclock_reset_pio_counter();

	// Start counting external clock edges
	pio_sm_set_enabled(ECLK_PIO, E_SM, true);

	// Wait for detection window
	busy_wait_us(detection_window_us);

	// Read cycle count and stop
	uint32_t cycles = eclock_get_pio_cycles();
	pio_sm_set_enabled(ECLK_PIO, E_SM, false);

	printf("External E clock detection: (%lu cycles in %luµs)\r\n", cycles,
	       detection_window_us);

	// Decide based on detected cycles
	if (cycles >= min_cycles_threshold) {
		// External clock detected - stay in external mode
		printf("External E clock detected\r\n");
		eclock_reset_pio_counter();
		return true;
	} else {
		// No external clock - switch to internal
		printf("No external E clock detected, using internal\r\n");
		eclock_set_mode(ECLOCK_INTERNAL);
		return false;
	}
}
