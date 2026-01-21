/**
 * MC6800 Memory Map Management
 * Handles the memory mapping table and related functions
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

/** Function pointer type for printf-style output functions */
typedef int (*printf_func_t)(const char *format, ...);

typedef enum memory_type_t {
	MEM_TYPE_UNMAPPED, // Unmapped (peripheral) address - routes to physical bus
	MEM_TYPE_ROM,      // ROM (EPROM) - read only from flash
	MEM_TYPE_RAM,      // RAM - read/write from shadow
} memory_type_t;

/** Address translation for missing A15 decode - masks off A15 bit */
#define ADDR_MASK_A15 0x7FFF

#define ENTRY_PAGE_SIZE 256 // 256-byte pages in memory map
#define NUM_PAGES       (0x10000U / ENTRY_PAGE_SIZE)

// Flash storage for memory config and map
/** Flash offset for memory configuration storage */
#define FLASH_MEMORY_CONFIG_OFFSET      (FLASH_TARGET_OFFSET + MAX_ROM_SIZE)
/** Size of memory config structure in flash */
#define FLASH_MEMORY_CONFIG_SIZE        sizeof(memory_config_t)
/** Padded size of config in flash (must be 256 bytes for flash_range_program) */
#define FLASH_MEMORY_CONFIG_PADDED_SIZE 256
/** Flash offset for memory map storage */
#define FLASH_MEMORY_MAP_OFFSET         (FLASH_MEMORY_CONFIG_OFFSET + FLASH_MEMORY_CONFIG_PADDED_SIZE)
/** Size of memory map in flash */
#define FLASH_MEMORY_MAP_SIZE           (NUM_PAGES * sizeof(uint32_t))

// Memory map entry flags (local definitions not exported)
#define ENTRY_UNMAPPED      0b001
#define ENTRY_WRITABLE      0b010
#define ENTRY_WRITE_THROUGH 0b100

/** Memory map entry definitions */
/** Mask for extracting address from map entry (upper 24 bits) */
#define ENTRY_ADDR_MASK    ~0xFFU
/** Mask for extracting flags from map entry (lower 8 bits) */
#define ENTRY_FLAG_MASK    0b111
/** Map entry flag combination for RAM mapping */
#define ENTRY_MAPPED_RAM   0b010
/** Map entry flag combination for CMOS mapping */
#define ENTRY_MAPPED_CMOS  0b110
/** Map entry flag combination for ROM mapping */
#define ENTRY_MAPPED_ROM   0b000
/** Map entry flag combination for unmapped bus access */
#define ENTRY_UNMAPPED_BUS 0b011

/** Convert address to memory table index (page number) */
#define ADDR_TO_TABLE_INDEX(addr)  ((uint8_t)((addr) >> 8))
#define TABLE_INDEX_TO_ADDR(index) ((uint8_t)index << 8)
/** Convert address to offset within memory page */
#define ADDR_TO_TABLE_OFFSET(addr) ((addr) & 0xFF)

/** Memory map array - maps 256-byte pages to physical addresses and flags */
// Persistent, restored from QSPI flash at startup
extern uint32_t memory_map[NUM_PAGES];

// Fast memory access using shadows or direct bus access
uint8_t memory_read_fast(uint16_t address);
void memory_write_fast(uint16_t address, uint8_t data);

/** Core memory map functions */
/** Check if an address is mapped in the memory map */
bool memory_is_address_mapped(uint16_t address);
/** Return true if any of the pages in the given range are marked as unmapped */
bool memory_range_has_unmapped(uint16_t address, uint16_t len);
/** Load memory map from flash storage */
bool memory_load_memory_map_from_flash(void);
/** Get the memory type for an address */
memory_type_t memory_get_type(uint16_t address);
/** Get shadow RAM address for fast access to mapped memory */
uint8_t *memory_get_shadow_address(uint16_t address);
/** Get unaliased target physical address corresponding to address */
uint16_t memory_get_unaliased_address(uint16_t address);
/** Clear all ROM mappings from memory map */
void memory_clear_map(void);
/** Initialize memory map with default mappings */
void memory_initialize_map(void);
/** Print memory map summary using provided printf function */
void memory_print_summary(printf_func_t printf_func);
/** Save current memory map to flash storage */
void memory_save_memory_map_to_flash(void);
/** Save ROM mapping portion to flash storage */
void memory_save_rom_mapping_to_flash(void);
/** Set up RAM mapping for an address page */
void map_as_ram(uint16_t address);
/** Set up ROM mapping for an address page */
void map_as_rom(uint16_t address);
/** Update memory map per mem_config's RAM and ROM ranges */
void memory_update_map(void);
