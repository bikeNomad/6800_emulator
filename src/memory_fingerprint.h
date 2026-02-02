/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
const char *
architecture_name(architecture_type_t arch); // Get the name of the detected architecture
// Copy the ROM range defined in mem_config into our flash
bool copy_rom_contents_from_bus(printf_func_t printf_func);