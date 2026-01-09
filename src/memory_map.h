/**
 * MC6800 Memory Map Management
 * Handles the memory mapping table and related functions
 */

#pragma once

#include <stdbool.h>
#include <stdint.h>

// Forward declarations for types defined in memory.h
typedef enum {
    MEM_TYPE_UNMAPPED,  // Unmapped (peripheral) address - routes to physical bus
    MEM_TYPE_ROM,       // ROM (EPROM) - read only from flash
    MEM_TYPE_RAM,       // RAM - read/write from shadow
    MEM_TYPE_CMOS       // CMOS RAM - read/write from bus for now
} memory_type_t;

typedef int (*printf_func_t)(const char *format, ...);

// Memory configuration
typedef struct {
    uint16_t rom_base;   // Base address in MC6800 space (e.g., $E000)
    uint16_t rom_size;   // Size of ROM region
    uint16_t ram_base;   // Base address of RAM (e.g., $0000)
    uint16_t ram_size;   // Size of RAM (e.g., 512 bytes)
    uint16_t cmos_base;  // Base address of CMOS RAM (0x0100)
    uint16_t cmos_size;  // Size of CMOS RAM (256 bytes)
} memory_config_t;

// Address translation for missing A15 decode
#define ADDR_MASK_A15 0x7FFF // Mask off A15

// Flash storage for ROM (at the end of program flash)
#define FLASH_TARGET_OFFSET (2 * 1024 * 1024) // 2MB offset (adjust based on program size)
#define MAX_ROM_SIZE (48 * 1024)          // 48KB max ROM (increased for System 11)
#define MAX_RAM_SIZE (8 * 1024)           // 8KB max RAM (supports up to 0x1FFF)

// CMOS RAM
#define CMOS_SIZE 256       // SYS7
#define CMOS_BASE 0x0100    // SYS7

#define ENTRY_PAGE_SIZE 256 // 256-byte pages in memory map
#define MEMORY_TABLE_SIZE (0x10000U / ENTRY_PAGE_SIZE)

// Flash storage for memory config and map
#define FLASH_MEMORY_CONFIG_OFFSET (FLASH_TARGET_OFFSET + MAX_ROM_SIZE)
#define FLASH_MEMORY_CONFIG_SIZE sizeof(memory_config_t)
#define FLASH_MEMORY_CONFIG_PADDED_SIZE 256  // Config must be padded to 256 bytes for flash_range_program
#define FLASH_MEMORY_MAP_OFFSET (FLASH_MEMORY_CONFIG_OFFSET + FLASH_MEMORY_CONFIG_PADDED_SIZE)
#define FLASH_MEMORY_MAP_SIZE (MEMORY_TABLE_SIZE * sizeof(uint32_t))

// Memory map entry flags (local definitions not exported)
#define ENTRY_UNMAPPED 0b001
#define ENTRY_WRITABLE 0b010
#define ENTRY_WRITE_THROUGH 0b100

// Memory map entry definitions
#define ENTRY_ADDR_MASK ~0xFFU
#define ENTRY_FLAG_MASK 0b111
#define ENTRY_MAPPED_RAM 0b010
#define ENTRY_MAPPED_CMOS 0b110
#define ENTRY_MAPPED_ROM 0b000
#define ENTRY_UNMAPPED_BUS 0b011
#define ADDR_TO_TABLE_INDEX(addr) ((uint8_t)((addr) >> 8))
#define ADDR_TO_TABLE_OFFSET(addr) ((addr) & 0xFF)

// Memory map array (declared in memory_map.c)
extern uint32_t memory_map[];

// Fast memory access using shadows or direct bus access
uint8_t memory_read_fast(uint16_t address);
void    memory_write_fast(uint16_t address, uint8_t data);

// Core memory map functions
bool          memory_is_address_mapped(uint16_t address);
bool          memory_load_memory_map_from_flash(void);
memory_type_t memory_get_mapping_type(uint16_t address);
memory_type_t memory_get_type(uint16_t address);
uint8_t      *memory_get_shadow_address(uint16_t address);
void          memory_clear_rom_mapping(void);
void          memory_initialize_map(void);
void          memory_print_summary(printf_func_t printf_func);
void          memory_save_memory_map_to_flash(void);
void          memory_save_rom_mapping_to_flash(void);
void          memory_set_rom_mapping(uint16_t address, bool mapped);
void          setup_cmos_mapping(uint16_t address);
void          setup_ram_mapping(uint16_t address);
void          setup_rom_mapping(uint16_t address);