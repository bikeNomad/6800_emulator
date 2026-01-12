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
    ARCH_UNKNOWN,          // Architecture not recognized
    ARCH_EARLY_BALLY,      // Bally AS-2518-17 or AS-2518-35 or Stern MPU-100 or MPU-200
    ARCH_WILLIAMS_SYS3_6,  // Williams System 3 or 6
    ARCH_WILLIAMS_SYS7,    // Williams System 7
    ARCH_WILLIAMS_SYS9,    // Williams System 9
    ARCH_WILLIAMS_SYS11,   // Williams System 11
    ARCH_WILLIAMS_WPC
} architecture_type_t;

// Results of memory sanity checking
typedef enum {
    SANITY_OK,             // Memory configuration is valid
    SANITY_NO_SAVED_MAP,   // No saved memory map available
    SANITY_RAM_MISMATCH,   // RAM contents don't match expected values
    SANITY_ROM_UNEXPECTED  // ROM contents don't match expected values
} sanity_result_t;

// Result of scanning a memory page
// Page classification types
typedef enum {
    PAGE_EMPTY,    // All 0xFF or all 0x00
    PAGE_ROM,      // Read-only, consistent data
    PAGE_RAM,      // Read/write, test passes
    PAGE_CMOS,     // Williams Sys 3-7 CMOS (high nybble = 0xF)
    PAGE_PIA,      // 6820/6821 PIA detected
    PAGE_UNMAPPED  // Default/unknown
} page_type_t;

// Memory fingerprinting and auto-configuration

architecture_type_t recognize_architecture(void);                           // Determine system architecture from scan results
bool                copy_rom_contents_from_bus(page_type_t        *results,
                                               architecture_type_t arch,
                                               printf_func_t       printf_func);  // Copy ROM data from bus to memory
bool                memory_scan_and_build_map(printf_func_t printf_func);   // Scan memory and build memory map
void                coalesce_regions(architecture_type_t arch);             // Coalesce adjacent regions of same type
const page_type_t  *memory_get_coalesced_scan_results(void);                // Get coalesced memory scan results
const page_type_t  *memory_get_scan_results(void);                          // Get raw memory scan results
int                 count_decoded_address_bits(page_type_t *results);       // Detect if addresses are aliased
page_type_t         fingerprint_page(uint16_t address);                     // Determine page type at address
sanity_result_t     memory_get_startup_status(void);                        // Get status from boot for display
sanity_result_t     memory_sanity_check(void);                              // Perform sanity check on memory configuration
void                apply_system7_rules(void);                              // Apply special rules for System 7 architecture
// Build memory map from scan results
void build_memory_map_from_scan(page_type_t        *results,
                                architecture_type_t arch,
                                printf_func_t       printf_func);
