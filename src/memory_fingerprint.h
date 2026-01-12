/*
 * Figure out and adjust the memory map entries based on page scan results
 */
#pragma once

#include "bus.h"
#include "clock.h"
#include "emulator.h"
#include "memory.h"
#include "memory_map.h"

// Memory map building functions (from fingerprinting)

// Supported Williams system architectures
typedef enum {
	ARCH_UNKNOWN,         // Architecture not recognized
	ARCH_EARLY_BALLY,     // Bally AS-2518-17 or AS-2518-35 or Stern MPU-100 or MPU-200
	ARCH_WILLIAMS_SYS3_6, // Williams System 3 or 6
	ARCH_WILLIAMS_SYS7,   // Williams System 7
	ARCH_WILLIAMS_SYS9,   // Williams System 9
	ARCH_WILLIAMS_SYS11,  // Williams System 11
	ARCH_WILLIAMS_WPC
} architecture_type_t;

// Results of memory sanity checking
typedef enum {
	SANITY_OK,            // Memory configuration is valid
	SANITY_NO_SAVED_MAP,  // No saved memory map available
	SANITY_RAM_MISMATCH,  // RAM contents don't match expected values
	SANITY_ROM_UNEXPECTED // ROM contents don't match expected values
} sanity_result_t;

// Memory fingerprinting and auto-configuration

bool memory_scan_and_build_map(printf_func_t printf_func); // Scan memory and build memory map
sanity_result_t memory_get_startup_status(void);           // Get status from boot for display
sanity_result_t memory_sanity_check(void);          // Perform sanity check on memory configuration
void print_scan_results(printf_func_t printf_func); // Print the memory scan results