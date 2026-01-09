/**
 * MC6800 Memory Map Management
 * Handles the memory mapping table and related functions
 */

#ifndef MEMORY_MAP_H
#define MEMORY_MAP_H

#include <stdbool.h>
#include <stdint.h>

// Forward declarations for types defined in memory.h
typedef enum {
    MEM_TYPE_UNMAPPED,  // Unmapped (peripheral) address - routes to physical bus
    MEM_TYPE_ROM,       // ROM (EPROM) - read only from flash
    MEM_TYPE_RAM,       // RAM - read/write from shadow
    MEM_TYPE_CMOS       // CMOS RAM - read/write from bus for now
} memory_type_t;

typedef enum {
    SANITY_OK,
    SANITY_NO_SAVED_MAP,
    SANITY_RAM_MISMATCH,
    SANITY_ROM_UNEXPECTED
} sanity_result_t;

typedef struct {
    uint8_t  type;  // page_type_t from memory_fingerprint.c
    uint16_t address;
} scan_result_t;

typedef int (*printf_func_t)(const char *format, ...);

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

// Memory map entry flags (local definitions not exported)
#define ENTRY_UNMAPPED 0b001
#define ENTRY_WRITABLE 0b010
#define ENTRY_WRITE_THROUGH 0b100

#define ADDR_TO_TABLE_OFFSET(addr) ((addr) & 0xFF)
#define HIGH_ALIAS_TABLE_OFFSET 0x80
#define MEMORY_TABLE_SIZE (0x10000U / ENTRY_PAGE_SIZE)

// Flash storage for memory config and map
#define FLASH_MEMORY_CONFIG_OFFSET (FLASH_TARGET_OFFSET + MAX_ROM_SIZE)
#define FLASH_MEMORY_CONFIG_SIZE sizeof(memory_config_t)
#define FLASH_MEMORY_CONFIG_PADDED_SIZE 256  // Config must be padded to 256 bytes for flash_range_program
#define FLASH_MEMORY_MAP_OFFSET (FLASH_MEMORY_CONFIG_OFFSET + FLASH_MEMORY_CONFIG_PADDED_SIZE)
#define FLASH_MEMORY_MAP_SIZE (MEMORY_TABLE_SIZE * sizeof(uint32_t))

// Memory map entry definitions
#define ENTRY_ADDR_MASK ~0xFFU
#define ENTRY_FLAG_MASK 0b111
#define ENTRY_MAPPED_RAM 0b010
#define ENTRY_MAPPED_CMOS 0b110
#define ENTRY_MAPPED_ROM 0b000
#define ENTRY_UNMAPPED_BUS 0b011

// Memory map array (declared in memory_map.c)
extern uint32_t memory_map[];

// Core memory map functions
void memory_initialize_map(void);

// Memory map building functions (from fingerprinting)
typedef enum {
    ARCH_UNKNOWN,
    ARCH_WILLIAMS_SYS7,
    ARCH_WILLIAMS_SYS11
} architecture_type_t;

void setup_ram_mapping(uint16_t address);
void setup_cmos_mapping(uint16_t address);
void setup_rom_mapping(uint16_t address);
void apply_system7_rules(void);
void build_memory_map_from_scan(scan_result_t *results, architecture_type_t arch, printf_func_t printf_func);
int  detect_address_aliasing(scan_result_t *results);

// Memory fingerprinting and auto-configuration
bool            memory_scan_and_build_map(printf_func_t printf_func);
sanity_result_t memory_sanity_check(void);

// Scan results access
const scan_result_t *memory_get_scan_results(void);
const scan_result_t *memory_get_coalesced_scan_results(void);

#endif  // MEMORY_MAP_H
