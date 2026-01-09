/**
 * MC6800 Memory Subsystem
 * Manages 64KB address space mapping to ROM (flash) and RAM
 *
 * At startup, ROM contents are read from QSPI flash to rom_shadow and
 *   ram_shadow is copied from the target system
 *
 * Assumptions:
 *   - No memory region is smaller than 256 bytes
 *   - ROM is in one contiguous range from mem_config.rom_base to mem_config.rom_base+mem_config.rom_size
 *   - RAM is in one contiguous range from mem_config.ram_base to mem_config.rom_base+mem_config.ram_size
 *   - CMOS (if it exists) lives somewhere within the RAM range
 *   - There may be aliasing due to incomplete address line decoding; in this case aliased memory map
 *     entries are copies of the base entries
 */

#ifndef MEMORY_H
#define MEMORY_H

#include "emulator.h"
#include <stdbool.h>
#include <stdint.h>

// Address translation for missing A15 decode
#define ADDR_MASK_A15 0x7FFF // Mask off A15

// Flash storage for ROM (at the end of program flash)
#define FLASH_TARGET_OFFSET (2 * 1024 * 1024) // 2MB offset (adjust based on program size)
#define MAX_ROM_SIZE (48 * 1024)          // 48KB max ROM (increased for System 11)
#define MAX_RAM_SIZE (8 * 1024)           // 8KB max RAM (supports up to 0x1FFF)

// CMOS RAM
#define CMOS_SIZE 256       // SYS7
#define CMOS_BASE 0x0100    // SYS7

// Memory region types
typedef enum {
    MEM_TYPE_UNMAPPED,  // Unmapped (peripheral) address - routes to physical bus
    MEM_TYPE_ROM,       // ROM (EPROM) - read only from flash
    MEM_TYPE_RAM,       // RAM - read/write from shadow
    MEM_TYPE_CMOS       // CMOS RAM - read/write from bus for now
} memory_type_t;

#define ENTRY_PAGE_SIZE 256 // 256-byte pages in memory map

// Memory configuration
typedef struct {
    uint16_t rom_base;      // Base address in MC6800 space (e.g., $E000)
    uint16_t rom_size;      // Size of ROM region
    uint16_t ram_base;      // Base address of RAM (e.g., $0000)
    uint16_t ram_size;      // Size of RAM (e.g., 512 bytes)
    uint16_t cmos_base;     // Base address of CMOS RAM (0x0100)
    uint16_t cmos_size;     // Size of CMOS RAM (256 bytes)
    uint8_t  alias_offset;  // High address alias offset (default: 0x80)
    bool     configured;    // Configuration complete
} memory_config_t;

extern memory_config_t mem_config;
extern uint8_t         rom_shadow[MAX_ROM_SIZE]
    __attribute__((aligned(256)));  // Fast RAM copy of ROM for execution

// Compile-time alignment verification
_Static_assert(__alignof__(rom_shadow) == 256, "rom_shadow not 256-byte aligned");

extern uint32_t memory_map[];

extern uint8_t ram_shadow[MAX_RAM_SIZE]
    __attribute__((aligned(256)));  // Fast RAM copy of target RAM for execution
                                    //
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

// Load Intel HEX data into CMOS shadow copy
bool memory_load_cmos_data(uint16_t address, const uint8_t *data, uint16_t length);

// Save CMOS RAM to flash
bool memory_save_cmos(void);

// Initialize ROM from flash on startup
void memory_init_rom_from_flash(void);

// Clear ROM load buffer (for copy_roms command)
void memory_clear_rom_load_buffer(void);

// Get CMOS data for diagnostics (direct access to shadow copy)
const uint8_t *memory_get_cmos_shadow(void);

// Print a summary of the various memory ranges defined in the memory_map
void memory_print_summary(printf_func_t printf_func);

// Read RAM data from bus into shadow copy
void memory_read_ram_from_bus(void);

// Memory map persistence functions
void memory_save_memory_map_to_flash(void);
void memory_load_memory_map_from_flash(void);

// ROM mapping management functions
void memory_clear_rom_mapping(void);

// Legacy wrapper functions for backward compatibility
void memory_set_rom_mapping(uint16_t address, bool mapped);
void memory_save_rom_mapping_to_flash(void);

// Memory mapping query functions for external access
memory_type_t memory_get_mapping_type(uint16_t address);
bool          memory_is_address_mapped(uint16_t address);

static inline uint8_t memory_read_rom_shadow(uint16_t address) {
    // Memory map handles address aliasing, so use logical address directly
    uint16_t rom_offset = address - mem_config.rom_base;
    return rom_shadow[rom_offset];
}

// Turn off all LEDs (for NED_SYS7 board)
void led_all_off(void);

// Memory fingerprinting and auto-configuration
bool memory_scan_and_build_map(printf_func_t printf_func);
typedef enum {
    SANITY_OK,
    SANITY_NO_SAVED_MAP,
    SANITY_RAM_MISMATCH,
    SANITY_ROM_UNEXPECTED
} sanity_result_t;
sanity_result_t memory_sanity_check(void);
sanity_result_t memory_get_startup_status(void);  // Get status from boot for display

// Scan result access
typedef struct {
    uint8_t  type;  // page_type_t from memory_fingerprint.c
    uint16_t address;
} scan_result_t;
const scan_result_t *memory_get_scan_results(void);
const scan_result_t *memory_get_coalesced_scan_results(void);

#define ADDR_TO_TABLE_INDEX(addr) ((uint8_t)((addr) >> 8))
#define ENTRY_ADDR_MASK ~0xFFU
#define ENTRY_FLAG_MASK 0b111
#define ENTRY_MAPPED_RAM 0b010
#define ENTRY_MAPPED_CMOS 0b110
#define ENTRY_MAPPED_ROM 0b000
#define ENTRY_UNMAPPED_BUS 0b011

#endif  // MEMORY_H
