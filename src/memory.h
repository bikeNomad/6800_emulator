/**
 * MC6800 Memory Subsystem
 * Manages 64KB address space mapping to ROM (flash) and RAM
 *
 * At startup, ROM contents are read from QSPI flash to rom_shadow and
 *   ram_shadow is copied from the target system
 *
 * Assumptions:
 *   - No memory region is smaller than 256 bytes
 *   - ROM can be represented by one contiguous range from mem_config.rom_base to
 *     mem_config.rom_base+mem_config.rom_size
 *   - ROM range is 48kiB at most
 *   - RAM is in one contiguous range from mem_config.ram_base to
 *     mem_config.rom_base+mem_config.ram_size
 *   - CMOS (if it exists) lives somewhere within the RAM range
 *      - NOT TRUE FOR EARLY BALLY; treat this as write-through (unmapped)
 *   - There may be aliasing due to incomplete address line decoding; in this case aliased memory
 *     map entries are copies of the base entries
 */

#pragma once

#include "emulator.h"
#include "memory_map.h"
#include <stdbool.h>
#include <stdint.h>

// Memory configuration
// Persistent, restored from QSPI flash at startup
typedef struct memory_config_t {
	uint16_t rom_base;    // Base address in MC6800 space (e.g., $E000)
	uint16_t rom_size;    // Size of ROM region in bytes
	uint16_t ram_base;    // Base address of RAM in MC6800 space (e.g., $0000)
	uint16_t ram_size;    // Size of RAM region in bytes (e.g., 512 bytes)
	uint8_t architecture; // System architecture (architecture_type_t from memory_fingerprint.h)
	uint8_t decoded_bits; // Number of address bits decoded ([13..16])
} memory_config_t;

// Flash storage for ROM (at the end of program flash)
#define FLASH_TARGET_OFFSET (2 * 1024 * 1024) // 2MB offset (adjust based on program size)
#define MIN_ROM_ADDRESS     0x4000U           // Lowest ROM address
#define MAX_ROM_SIZE        (48 * 1024)       // 48KB max ROM (increased for System 11)
#define MAX_RAM_SIZE        (8 * 1024)        // 8KB max RAM (supports up to 0x1FFF)

extern memory_config_t mem_config;
extern uint8_t rom_shadow[MAX_ROM_SIZE]
	__attribute__((aligned(256))); // Fast RAM copy of ROM for execution

// Compile-time alignment verification
_Static_assert(__alignof__(rom_shadow) == 256, "rom_shadow not 256-byte aligned");

extern uint8_t ram_shadow[MAX_RAM_SIZE]
	__attribute__((aligned(256))); // Fast RAM copy of target RAM for execution

_Static_assert(__alignof__(ram_shadow) == 256, "ram_shadow not 256-byte aligned");

// Initialize memory subsystem
void memory_init(void);

// Configure memory regions (called from USB command)
void memory_configure_rom(uint16_t base, uint16_t size);
void memory_configure_ram(uint16_t base, uint16_t size);

// Get configuration values
void memory_get_rom_config(uint16_t *base, uint16_t *size);
void memory_get_ram_config(uint16_t *base, uint16_t *size);

// Get memory type for address
memory_type_t memory_get_type(uint16_t address);

// Fast-path read (no GPIO polling for ROM/RAM)
uint8_t memory_read_fast(uint16_t address);

// Fast-path write (no GPIO polling for ROM/RAM)
void memory_write_fast(uint16_t address, uint8_t value);

// Load Intel HEX data into flash shadow copy
bool memory_load_hex_data(uint16_t address, const uint8_t *data, uint16_t length);

// Finalize EPROM load (commit to flash)
bool memory_finalize_load(void);

// Initialize ROM from flash on startup
void memory_init_rom_from_flash(void);

// Clear ROM load buffer (for copy_roms command)
void memory_clear_rom_load_buffer(void);

// Read RAM data from bus into shadow copy
void memory_read_ram_from_bus(void);

// Get CMOS data for diagnostics (direct access to shadow copy)
const uint8_t *memory_get_cmos_shadow(void);

// Print a summary of the various memory ranges defined in the memory_map
void memory_print_summary(printf_func_t printf_func);

static inline uint8_t memory_read_rom_shadow(uint16_t address)
{
	// Memory map handles address aliasing, so use logical address directly
	uint16_t rom_offset = address - mem_config.rom_base;
	return rom_shadow[rom_offset];
}

// Turn off all LEDs (for NED_SYS7 board)
void led_all_off(void);
