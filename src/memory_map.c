/**
 * MC6800 Memory Map Implementation
 */

#include "memory_map.h"
#include "board_config.h"
#include "bus.h"
#include "clock.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "interrupts.h"
#include "memory.h"
#include "pico.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

// Memory map array
uint32_t memory_map[MEMORY_TABLE_SIZE];

// Test pattern for RAM/CMOS detection
static const uint8_t TEST_DATA[] = "This is a test of the bus";
#define TEST_DATA_SIZE (sizeof(TEST_DATA) - 1)

// PIA register offsets
#define PRA_OFFSET  0
#define DDRA_OFFSET 0
#define CRA_OFFSET  1
#define PRB_OFFSET  2
#define DDRB_OFFSET 2
#define CRB_OFFSET  3
#define SELECT_PR_BIT 0x04  // Bit in CRx to access PRx instead of DDRx

// Page classification types
typedef enum {
    PAGE_EMPTY,    // All 0xFF or all 0x00
    PAGE_ROM,      // Read-only, consistent data
    PAGE_RAM,      // Read/write, test passes
    PAGE_CMOS,     // Williams Sys 3-7 CMOS (high nybble = 0xF)
    PAGE_PIA,      // 6820/6821 PIA detected
    PAGE_UNMAPPED  // Default/unknown
} page_type_t;

// Scan results storage
static scan_result_t scan_results[256];       // One per 256-byte page
static scan_result_t coalesced_results[256];  // Coalesced results for System 11

page_type_t         fingerprint_page(uint16_t address);
architecture_type_t recognize_architecture(scan_result_t *results);
void                build_memory_map_from_scan(scan_result_t *results, architecture_type_t arch, printf_func_t printf_func);
bool                copy_rom_contents_from_bus(scan_result_t *results, printf_func_t printf_func);
void                coalesce_regions(architecture_type_t arch);

//--------------------------------------------------------------------+
// Helper Functions
//--------------------------------------------------------------------+

static const char *page_type_to_string(page_type_t type) {
    switch (type) {
    case PAGE_EMPTY:
        return "EMPTY";
    case PAGE_ROM:
        return "ROM";
    case PAGE_RAM:
        return "RAM";
    case PAGE_CMOS:
        return "CMOS (4-bit)";
    case PAGE_PIA:
        return "PIA";
    case PAGE_UNMAPPED:
        return "UNMAPPED";
    default:
        return "UNKNOWN";
    }
}

static const char *architecture_name(architecture_type_t arch) {
    switch (arch) {
    case ARCH_WILLIAMS_SYS7:
        return "Williams System 3-7";
    case ARCH_WILLIAMS_SYS11:
        return "Williams System 11";
    case ARCH_UNKNOWN:
        return "Unknown";
    default:
        return "Unknown";
    }
}

//--------------------------------------------------------------------+
// Detection Functions
//--------------------------------------------------------------------+

static page_type_t detect_empty(uint16_t address) {
    uint8_t block_data[ENTRY_PAGE_SIZE];

    // Read entire page
    for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
        block_data[i] = bus_read_cycle(address + i);
    }

    // Check if all 0xFF
    bool all_ff = true;
    for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
        if (block_data[i] != 0xFF) {
            all_ff = false;
            break;
        }
    }
    if (all_ff)
        return PAGE_EMPTY;

    // Check if all 0x00
    bool all_00 = true;
    for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
        if (block_data[i] != 0x00) {
            all_00 = false;
            break;
        }
    }
    if (all_00)
        return PAGE_EMPTY;

    return PAGE_UNMAPPED;
}

static page_type_t detect_ram(uint16_t address) {
    uint8_t original_data[TEST_DATA_SIZE];
    uint8_t readback_data[TEST_DATA_SIZE];

    // Read original data
    for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
        original_data[i] = bus_read_cycle(address + i);
    }

    // Write test pattern
    for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
        bus_write_cycle(address + i, TEST_DATA[i]);
    }

    // Read back
    for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
        readback_data[i] = bus_read_cycle(address + i);
    }

    // Restore original data
    for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
        bus_write_cycle(address + i, original_data[i]);
    }

    // Check if test pattern matched
    if (memcmp(readback_data, TEST_DATA, TEST_DATA_SIZE) == 0) {
        return PAGE_RAM;
    }

    return PAGE_UNMAPPED;
}

static page_type_t detect_sys3_7_cmos(uint16_t address) {
    uint8_t original_data[ENTRY_PAGE_SIZE];

    // Read original data and check for CMOS pattern (high nybble = 0xF)
    for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
        original_data[i] = bus_read_cycle(address + i);
        if ((original_data[i] & 0xF0) != 0xF0) {
            return PAGE_UNMAPPED;
        }
    }

    // Write test data (low nybbles only)
    for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
        bus_write_cycle(address + i, TEST_DATA[i]);
    }

    // Read back and check low nybbles
    bool matches = true;
    for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
        uint8_t readback = bus_read_cycle(address + i);
        if ((readback & 0x0F) != (TEST_DATA[i] & 0x0F)) {
            matches = false;
            break;
        }
    }

    // Restore original data
    for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
        bus_write_cycle(address + i, original_data[i]);
    }

    return matches ? PAGE_CMOS : PAGE_UNMAPPED;
}

static page_type_t detect_pia(uint16_t address) {
    // Save state
    uint8_t pra_ddra = bus_read_cycle(address + PRA_OFFSET);
    uint8_t cra = bus_read_cycle(address + CRA_OFFSET);
    uint8_t prb_ddrb = bus_read_cycle(address + PRB_OFFSET);
    uint8_t crb = bus_read_cycle(address + CRB_OFFSET);

    uint8_t pra, ddra, prb, ddrb;

    // Get actual PR and DDR values
    if (cra & SELECT_PR_BIT) {
        pra = pra_ddra;
        bus_write_cycle(address + CRA_OFFSET, cra & ~SELECT_PR_BIT);
        ddra = bus_read_cycle(address + DDRA_OFFSET);
    } else {
        ddra = pra_ddra;
        bus_write_cycle(address + CRA_OFFSET, cra | SELECT_PR_BIT);
        pra = bus_read_cycle(address + PRA_OFFSET);
    }

    if (crb & SELECT_PR_BIT) {
        prb = prb_ddrb;
        bus_write_cycle(address + CRB_OFFSET, crb & ~SELECT_PR_BIT);
        ddrb = bus_read_cycle(address + DDRB_OFFSET);
    } else {
        ddrb = prb_ddrb;
        bus_write_cycle(address + CRB_OFFSET, crb | SELECT_PR_BIT);
        prb = bus_read_cycle(address + PRB_OFFSET);
    }

    bool is_pia = true;

    // Test: Set both DDRs to all-inputs (0x00)
    bus_write_cycle(address + CRA_OFFSET, 0x00);
    bus_write_cycle(address + DDRA_OFFSET, 0x00);
    uint8_t ddra_readback = bus_read_cycle(address + DDRA_OFFSET);

    bus_write_cycle(address + CRB_OFFSET, 0x00);
    bus_write_cycle(address + DDRB_OFFSET, 0x00);
    uint8_t ddrb_readback = bus_read_cycle(address + DDRB_OFFSET);

    if (ddra_readback != 0x00 || ddrb_readback != 0x00) {
        is_pia = false;
    }

    if (is_pia) {
        // Access PRs
        bus_write_cycle(address + CRA_OFFSET, SELECT_PR_BIT);
        bus_write_cycle(address + CRB_OFFSET, SELECT_PR_BIT);

        // Verify SELECT_PR_BIT is set
        uint8_t cra_now = bus_read_cycle(address + CRA_OFFSET);
        uint8_t crb_now = bus_read_cycle(address + CRB_OFFSET);

        if ((cra_now & SELECT_PR_BIT) == 0 || (crb_now & SELECT_PR_BIT) == 0) {
            is_pia = false;
        }
    }

    if (is_pia) {
        // Read PRs (should be stable since DDRs are inputs)
        uint8_t pra_now = bus_read_cycle(address + PRA_OFFSET);
        uint8_t prb_now = bus_read_cycle(address + PRB_OFFSET);

        // Try to write (should have no effect with all inputs)
        bus_write_cycle(address + PRA_OFFSET, 0xFF);
        bus_write_cycle(address + PRB_OFFSET, 0xFF);

        uint8_t pra_written = bus_read_cycle(address + PRA_OFFSET);
        uint8_t prb_written = bus_read_cycle(address + PRB_OFFSET);

        if (pra_now != pra_written || prb_now != prb_written) {
            is_pia = false;
        }
    }

    // Restore state - Access DDRs
    bus_write_cycle(address + CRA_OFFSET, cra & ~SELECT_PR_BIT);
    bus_write_cycle(address + CRB_OFFSET, crb & ~SELECT_PR_BIT);
    bus_write_cycle(address + DDRA_OFFSET, ddra);
    bus_write_cycle(address + DDRB_OFFSET, ddrb);

    // Access PRs
    bus_write_cycle(address + CRA_OFFSET, cra | SELECT_PR_BIT);
    bus_write_cycle(address + CRB_OFFSET, crb | SELECT_PR_BIT);
    bus_write_cycle(address + PRA_OFFSET, pra);
    bus_write_cycle(address + PRB_OFFSET, prb);

    // Restore CRs
    bus_write_cycle(address + CRA_OFFSET, cra);
    bus_write_cycle(address + CRB_OFFSET, crb);

    return is_pia ? PAGE_PIA : PAGE_UNMAPPED;
}

page_type_t fingerprint_page(uint16_t address) {
    // Try detectors in order: PIA, RAM, CMOS, empty
    // PIA must be checked first to avoid false RAM detection
    page_type_t type;

    type = detect_pia(address);
    if (type == PAGE_PIA)
        return type;

    type = detect_ram(address);
    if (type == PAGE_RAM)
        return type;

    type = detect_sys3_7_cmos(address);
    if (type == PAGE_CMOS)
        return type;

    type = detect_empty(address);
    if (type == PAGE_EMPTY)
        return type;

    // Default to ROM for anything else
    return PAGE_ROM;
}

//--------------------------------------------------------------------+
// Architecture Recognition
//--------------------------------------------------------------------+

architecture_type_t recognize_architecture(scan_result_t *results) {
    // Count CMOS at specific addresses (System 7 signature)
    int cmos_count = 0;
    if (results[0x01].type == PAGE_CMOS)
        cmos_count++;
    if (results[0x05].type == PAGE_CMOS)
        cmos_count++;
    if (results[0x09].type == PAGE_CMOS)
        cmos_count++;
    if (results[0x0D].type == PAGE_CMOS)
        cmos_count++;

    // System 7: CMOS pattern + RAM at page 0
    if (cmos_count >= 3 && results[0x00].type == PAGE_RAM) {
        return ARCH_WILLIAMS_SYS7;
    }

    // Count contiguous RAM pages at start (System 11 signature)
    int ram_pages = 0;
    for (int i = 0; i <= 0x07; i++) {
        if (results[i].type == PAGE_RAM)
            ram_pages++;
    }

    // System 11: Contiguous RAM at start, no CMOS pattern
    if (ram_pages >= 6 && cmos_count == 0) {
        return ARCH_WILLIAMS_SYS11;
    }

    return ARCH_UNKNOWN;
}

memory_type_t memory_get_type(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t table_entry = memory_map[table_index];
    uint8_t  flags = table_entry & ENTRY_FLAG_MASK;
    switch (flags) {
    case ENTRY_MAPPED_ROM:
        return MEM_TYPE_ROM;
    case ENTRY_MAPPED_RAM:
        return MEM_TYPE_RAM;
    case ENTRY_MAPPED_CMOS:
        return MEM_TYPE_CMOS;
    default:
        return MEM_TYPE_UNMAPPED;
    }
}

// Initialize memory map
void memory_initialize_map(void) {
    // Set all entries to unmapped initially
    for (uint16_t i = 0; i < MEMORY_TABLE_SIZE; i++) {
        memory_map[i] = ENTRY_UNMAPPED_BUS;
    }

    // Set RAM entries including aliases (RAM is always mapped)
    for (uint16_t address = mem_config.ram_base;
         address < mem_config.ram_base + mem_config.ram_size;
         address += ENTRY_PAGE_SIZE) {
        uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&ram_shadow[0] + (address - mem_config.ram_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_RAM;  // mapped RAM
        memory_map[table_index] = table_entry;
        memory_map[(table_index + mem_config.alias_offset)] = table_entry;          // high alias
        // System 7 RAM mirroring: $1000-$10FF mirrors $0000-$00FF
        if (address < 0x0100) {
            uint8_t mirror_table_index = ADDR_TO_TABLE_INDEX(address + 0x1000);
            memory_map[mirror_table_index] = table_entry;
            memory_map[mirror_table_index + mem_config.alias_offset] = table_entry;
        }
    }

    // Set CMOS entries including aliases (CMOS is always mapped)
    for (uint16_t address = mem_config.cmos_base;
         address < mem_config.cmos_base + mem_config.cmos_size;
         address += ENTRY_PAGE_SIZE) {
        uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&ram_shadow[0] + (address - mem_config.ram_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_CMOS;  // mapped CMOS
        memory_map[table_index] = table_entry;
        memory_map[(table_index + mem_config.alias_offset)] = table_entry;
    }

    // ROM entries are now mapped based on the persistent memory_map loaded from flash
}

uint8_t __time_critical_func(memory_read_fast)(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint8_t  offset = ADDR_TO_TABLE_OFFSET(address);
    uint32_t table_entry = memory_map[table_index];
    if (table_entry & ENTRY_UNMAPPED) {  // unmapped
        return bus_read_cycle(address);
    }
    eclock_accumulate(1);                // Track cycle, don't wait
    uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
    uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
    return *shadow_address;
}

void __time_critical_func(memory_write_fast)(uint16_t address, uint8_t data) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint8_t  offset = ADDR_TO_TABLE_OFFSET(address);
    uint32_t table_entry = memory_map[table_index];
    if (table_entry & ENTRY_UNMAPPED) {  // unmapped
        bus_write_cycle(address, data);
        return;
    }
    if (!(table_entry & ENTRY_WRITABLE)) {  // Ignore writes to ROM
        eclock_accumulate(1);               // Track cycle, don't wait
        return;
    }
    eclock_accumulate(1);                   // Track cycle, don't wait
    uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
    uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
    *shadow_address = data;
    if (table_entry & ENTRY_WRITE_THROUGH) {  // CMOS write-through
        bus_write_cycle(address, data);
    }
}

// Print a summary of the various memory ranges defined in the memory_map
void memory_print_summary(printf_func_t printf_func) {
    printf_func("Memory Map Summary:\r\n");
    printf_func("  ROM: $%04X-$%04X (%d bytes)\r\n", mem_config.rom_base,
                mem_config.rom_base + mem_config.rom_size - 1, mem_config.rom_size);
    printf_func("  RAM: $%04X-$%04X (%d bytes)\r\n", mem_config.ram_base,
                mem_config.ram_base + mem_config.ram_size - 1, mem_config.ram_size);
    printf_func("  CMOS: $%04X-$%04X (%d bytes)\r\n", mem_config.cmos_base,
                mem_config.cmos_base + mem_config.cmos_size - 1, mem_config.cmos_size);
    printf_func("  Flash offset: 0x%08lX, size: %u bytes\r\n",
                (unsigned long)FLASH_TARGET_OFFSET, (unsigned int)mem_config.rom_size);
    printf_func("  Configuration: %s\r\n", mem_config.configured ? "configured" : "default");

    // Count mapped vs unmapped pages
    uint16_t mapped_pages = 0;
    uint16_t rom_pages = 0;
    uint16_t ram_pages = 0;
    uint16_t cmos_pages = 0;
    uint16_t unmapped_pages = 0;

    for (uint16_t i = 0; i < MEMORY_TABLE_SIZE; i++) {
        uint32_t entry = memory_map[i];
        if (entry & ENTRY_UNMAPPED) {
            unmapped_pages++;
        } else {
            mapped_pages++;
            if (entry & ENTRY_WRITABLE) {
                if (entry & ENTRY_WRITE_THROUGH) {
                    cmos_pages++;
                } else {
                    ram_pages++;
                }
            } else {
                rom_pages++;
            }
        }
    }

    printf_func("  Memory map: %u mapped pages (%u ROM, %u RAM, %u CMOS), %u unmapped pages\r\n",
                mapped_pages, rom_pages, ram_pages, cmos_pages, unmapped_pages);
    printf_func("  Total address space: %u pages (%u bytes)\r\n", MEMORY_TABLE_SIZE,
                MEMORY_TABLE_SIZE * ENTRY_PAGE_SIZE);
}

// Save memory config and map to flash
void memory_save_memory_map_to_flash(void) {
    // flash_range_program requires sizes to be multiples of FLASH_PAGE_SIZE (256 bytes)
    // FLASH_PAGE_SIZE is defined in hardware/flash.h

    // Round up config size to page boundary
    uint32_t config_write_size = (FLASH_MEMORY_CONFIG_SIZE + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
    uint32_t map_write_size = (FLASH_MEMORY_MAP_SIZE + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);

    // Calculate erase size (round up to sector boundary)
    uint32_t total_size = config_write_size + map_write_size;
    uint32_t erase_size = (total_size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);

    // Disable interrupts during flash operations
    uint32_t ints = save_and_disable_interrupts();

    // Erase flash sector(s)
    flash_range_erase(FLASH_MEMORY_CONFIG_OFFSET, erase_size);

    // Create aligned buffers
    static uint8_t config_buffer[256] __attribute__((aligned(4)));
    static uint8_t map_buffer[sizeof(memory_map)] __attribute__((aligned(4)));

    // Copy data to aligned buffers and zero-pad
    static_assert(sizeof(mem_config) <= sizeof(config_buffer));
    memset(config_buffer, 0, sizeof(config_buffer));
    memcpy(config_buffer, &mem_config, FLASH_MEMORY_CONFIG_SIZE);

    memset(map_buffer, 0, sizeof(map_buffer));
    memcpy(map_buffer, memory_map, FLASH_MEMORY_MAP_SIZE);

    // Program memory config to flash (must be 256-byte aligned size)
    flash_range_program(FLASH_MEMORY_CONFIG_OFFSET, config_buffer, config_write_size);

    // Program memory map to flash (already 1024 bytes, which is 256-byte aligned)
    flash_range_program(FLASH_MEMORY_MAP_OFFSET, map_buffer, map_write_size);

    // Flush cache
    flash_flush_cache();

    // Restore interrupts
    restore_interrupts(ints);

    printf("Memory config and map saved to flash (%u + %u bytes, padded to %lu + %lu)\n",
           FLASH_MEMORY_CONFIG_SIZE, FLASH_MEMORY_MAP_SIZE,
           (unsigned long)config_write_size, (unsigned long)map_write_size);
}

// Load memory config and map from flash
void memory_load_memory_map_from_flash(void) {
    const uint8_t *config_flash_ptr = (const uint8_t *)(XIP_BASE + FLASH_MEMORY_CONFIG_OFFSET);
    const uint8_t *map_flash_ptr = (const uint8_t *)(XIP_BASE + FLASH_MEMORY_MAP_OFFSET);

    // Check if memory config data exists (not all 0xFF)
    bool has_config_data = false;
    for (uint32_t i = 0; i < FLASH_MEMORY_CONFIG_SIZE; i++) {
        if (config_flash_ptr[i] != 0xFF) {
            has_config_data = true;
            break;
        }
    }

    // Check if memory map data exists (not all 0xFF)
    bool has_map_data = false;
    for (uint32_t i = 0; i < FLASH_MEMORY_MAP_SIZE; i++) {
        if (map_flash_ptr[i] != 0xFF) {
            has_map_data = true;
            break;
        }
    }

    if (has_config_data && has_map_data) {
        // Load config and validate it
        memory_config_t loaded_config;
        memcpy(&loaded_config, config_flash_ptr, FLASH_MEMORY_CONFIG_SIZE);

        // Validate loaded config
        bool config_valid = true;
        printf("Debug: Loaded config - rom_base=$%04X rom_size=%u ram_base=$%04X ram_size=%u\n",
               loaded_config.rom_base, loaded_config.rom_size,
               loaded_config.ram_base, loaded_config.ram_size);
        printf("Debug: MAX_ROM_SIZE=%u MAX_RAM_SIZE=%u\n", MAX_ROM_SIZE, MAX_RAM_SIZE);

        if (loaded_config.rom_size > MAX_ROM_SIZE) {
            printf("Debug: rom_size check failed\n");
            config_valid = false;
        }
        if (loaded_config.ram_size > MAX_RAM_SIZE) {
            printf("Debug: ram_size check failed\n");
            config_valid = false;
        }
        if (loaded_config.rom_size == 0) {
            printf("Debug: rom_size is zero\n");
            config_valid = false;
        }
        if (loaded_config.ram_size == 0) {
            printf("Debug: ram_size is zero\n");
            config_valid = false;
        }
        // Allow rom_base + rom_size to equal 0x10000 (full address space for System 11)
        if ((uint32_t)loaded_config.rom_base + loaded_config.rom_size > 0x10000) {
            printf("Debug: rom address range check failed (base+size=%lu > 0x10000)\n",
                   (unsigned long)((uint32_t)loaded_config.rom_base + loaded_config.rom_size));
            config_valid = false;
        }
        if ((uint32_t)loaded_config.ram_base + loaded_config.ram_size > 0x10000) {
            printf("Debug: ram address range check failed\n");
            config_valid = false;
        }

        printf("Debug: config_valid=%d\n", config_valid);

        if (config_valid) {
            // Load and validate memory map
            uint32_t loaded_map[MEMORY_TABLE_SIZE];
            memcpy(loaded_map, map_flash_ptr, FLASH_MEMORY_MAP_SIZE);

            // Validate loaded map - check for reasonable distribution
            int rom_pages = 0, ram_pages = 0, cmos_pages = 0, unmapped_pages = 0;
            for (unsigned int i = 0; i < MEMORY_TABLE_SIZE; i++) {
                uint32_t entry = loaded_map[i];
                if (entry & ENTRY_UNMAPPED) {
                    unmapped_pages++;
                } else if (entry & ENTRY_WRITABLE) {
                    if (entry & ENTRY_WRITE_THROUGH) {
                        cmos_pages++;
                    } else {
                        ram_pages++;
                    }
                } else {
                    rom_pages++;
                }
            }

            // Check for reasonable map (not all ROM, not all unmapped, etc.)
            bool map_valid = true;
            if (rom_pages > 200 || rom_pages < 10 || ram_pages > 50 || ram_pages < 1 ||
                unmapped_pages > 200 || cmos_pages > 10) {
                map_valid = false;
            }

            if (map_valid) {
                memcpy(&mem_config, &loaded_config, FLASH_MEMORY_CONFIG_SIZE);
                // Always use the correct flash offset
                memcpy(memory_map, loaded_map, FLASH_MEMORY_MAP_SIZE);
                printf("Memory config and map loaded from flash (%u + %u bytes)\n",
                       FLASH_MEMORY_CONFIG_SIZE, FLASH_MEMORY_MAP_SIZE);
            } else {
                printf("Invalid memory map data in flash, using default initialization\n");
                config_valid = false;  // Force default initialization
            }
        }

        if (!config_valid) {
            printf("Invalid memory config data in flash, using default initialization\n");
        }
    } else {
        printf("No memory config/map data found in flash, using default initialization\n");
    }
}

// Clear all ROM mapping (set ROM pages to unmapped in memory map)
void memory_clear_rom_mapping(void) {
    // Set all ROM pages to unmapped
    for (uint32_t address = mem_config.rom_base;
         address < (uint32_t)mem_config.rom_base + mem_config.rom_size;
         address += ENTRY_PAGE_SIZE) {
        uint8_t table_index = ADDR_TO_TABLE_INDEX(address);

        if (memory_map[table_index] == ENTRY_MAPPED_ROM) {
            // Set to unmapped (route to bus)
            memory_map[table_index] = ENTRY_UNMAPPED_BUS;
        }
    }

    // Save updated memory map to flash
    memory_save_memory_map_to_flash();
    printf("All ROM pages unmapped\n");
}

// Memory mapping query functions for external access

/**
 * Get the memory type and mapping status for a specific address
 * Returns the memory type (MEM_TYPE_ROM, MEM_TYPE_RAM, MEM_TYPE_CMOS, or MEM_TYPE_UNMAPPED)
 */
memory_type_t memory_get_mapping_type(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t table_entry = memory_map[table_index];

    if (table_entry & ENTRY_UNMAPPED) {
        return MEM_TYPE_UNMAPPED;
    }

    if (table_entry & ENTRY_WRITABLE) {
        if (table_entry & ENTRY_WRITE_THROUGH) {
            return MEM_TYPE_CMOS;
        } else {
            return MEM_TYPE_RAM;
        }
    } else {
        return MEM_TYPE_ROM;
    }
}

// Legacy wrapper functions for backward compatibility

// Set ROM mapping for specific address (256-byte page) - legacy wrapper
void memory_set_rom_mapping(uint16_t address, bool mapped) {
    // Use hardcoded A15 mask for backward compatibility (legacy behavior)
    uint16_t physical_addr = address & ADDR_MASK_A15;
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(physical_addr);

    if (mapped) {
        // Map this page as ROM
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&rom_shadow[0] + (physical_addr - mem_config.rom_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_ROM;
        memory_map[table_index] = table_entry;
        memory_map[table_index + HIGH_ALIAS_TABLE_OFFSET] = table_entry;  // Use hardcoded offset

        printf("Mapped ROM page at $%04X (page %u)\n", physical_addr & ~0xFF,
               (physical_addr - mem_config.rom_base) / ENTRY_PAGE_SIZE);
    } else {
        // Unmap this page (route to bus)
        memory_map[table_index] = ENTRY_UNMAPPED_BUS;
        memory_map[table_index + HIGH_ALIAS_TABLE_OFFSET] = ENTRY_UNMAPPED_BUS;  // Use hardcoded offset
    }
}

// Save ROM mapping to flash - legacy wrapper (now saves full memory map)
void memory_save_rom_mapping_to_flash(void) {
    memory_save_memory_map_to_flash();
}

/**
 * Check if an address is currently mapped (not unmapped)
 * Returns true if the address is mapped to ROM, RAM, or CMOS
 */
bool memory_is_address_mapped(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t table_entry = memory_map[table_index];
    return !(table_entry & ENTRY_UNMAPPED);
}

// Memory map building functions (moved from memory_fingerprint.c)

void setup_ram_mapping(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t shadow_addr = (uint32_t)(uintptr_t)&ram_shadow[address - mem_config.ram_base];
    uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_RAM;
    memory_map[table_index] = table_entry;
}

void setup_cmos_mapping(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t shadow_addr = (uint32_t)(uintptr_t)&ram_shadow[address - mem_config.ram_base];
    uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_CMOS;
    memory_map[table_index] = table_entry;
}

void setup_rom_mapping(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t shadow_addr = (uint32_t)(uintptr_t)&rom_shadow[address - mem_config.rom_base];
    uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_ROM;
    memory_map[table_index] = table_entry;
}

// Williams System 3-7 does not decode A15, and duplicates the zero page of RAM at $1000
void apply_system7_rules(void) {
    // Add $0000-$00FF → $1000-$10FF RAM mirror
    uint32_t page0_entry = memory_map[0x00];
    if ((page0_entry & ENTRY_FLAG_MASK) == ENTRY_MAPPED_RAM) {
        memory_map[0x10] = page0_entry;
    }

    // A15 not decoded: mirror low pages to high
    for (int i = 0; i < 128; i++) {
        memory_map[i + 128] = memory_map[i];
    }
}

void build_memory_map_from_scan(scan_result_t *results, architecture_type_t arch, printf_func_t printf_func) {
    // Initialize all to unmapped
    for (int i = 0; i < 256; i++) {
        memory_map[i] = ENTRY_UNMAPPED_BUS;
    }

    // Determine regions from scan
    uint16_t ram_start = 0xFFFF, ram_end = 0;
    uint16_t rom_start = 0xFFFF, rom_end = 0;
    uint16_t cmos_start = 0xFFFF, cmos_end = 0;

    // First pass: determine regions
    // For System 11, scan entire address space since A15 is fully decoded
    // For other systems, only scan low address space to avoid aliases
    int max_pages = (arch == ARCH_WILLIAMS_SYS11) ? 256 : 128;

    for (int page = 0; page < max_pages; page++) {
        uint16_t addr = page << 8;

        switch (results[page].type) {
        case PAGE_RAM:
            if (addr < ram_start)
                ram_start = addr;
            if (addr > ram_end)
                ram_end = addr;
            break;
        case PAGE_ROM:
            if (addr < rom_start)
                rom_start = addr;
            if (addr > rom_end)
                rom_end = addr;
            break;
        case PAGE_CMOS:
            if (addr < cmos_start)
                cmos_start = addr;
            if (addr > cmos_end)
                cmos_end = addr;
            break;
        default:
            break;
        }
    }

    // Update mem_config
    if (ram_start != 0xFFFF) {
        mem_config.ram_base = ram_start;
        mem_config.ram_size = (ram_end - ram_start) + 256;
    }
    if (rom_start != 0xFFFF) {
        mem_config.rom_base = rom_start;
        mem_config.rom_size = (rom_end - rom_start) + 256;
        // For System 11, if ROM extends to the end of address space, ensure full coverage
        if (arch == ARCH_WILLIAMS_SYS11 && rom_end >= 0xFF00) {
            mem_config.rom_size = 0x10000 - rom_start;
        }
    }
    if (cmos_start != 0xFFFF) {
        mem_config.cmos_base = cmos_start;
        mem_config.cmos_size = (cmos_end - cmos_start) + 256;
    }

    // Second pass: build map entries
    for (int page = 0; page < max_pages; page++) {
        uint16_t addr = page << 8;

        switch (results[page].type) {
        case PAGE_RAM:
            setup_ram_mapping(addr);
            break;
        case PAGE_CMOS:
            setup_cmos_mapping(addr);
            break;
        case PAGE_ROM:
            setup_rom_mapping(addr);
            break;
        case PAGE_EMPTY:
            // map EMPTY pages in ROM range as ROM
            if (addr >= rom_start && addr <= rom_end) {
                setup_rom_mapping(addr);
            }
            break;
        default:
            // PIA, UNMAPPED stay as ENTRY_UNMAPPED_BUS
            break;
        }
    }

    // Apply architecture-specific rules
    if (arch == ARCH_WILLIAMS_SYS7) {
        apply_system7_rules();
    } else if (arch == ARCH_WILLIAMS_SYS11) {
        // System 11 uses full A15 decode, no aliasing needed
        // ROM is continuous from $4000-$FFFF
    } else {
        // Unknown architecture - analyze actual address aliasing from scan results
        int decoded_bits = detect_address_aliasing(results);
        printf_func("Address decoding: %d bits decoded\r\n", decoded_bits);

        if (decoded_bits < 16) {
            // Apply mirroring based on detected aliasing
            int mirror_size = 1 << (16 - decoded_bits);  // Size of repeating block in pages
            int num_blocks = 256 / mirror_size;

            for (int block = 1; block < num_blocks; block++) {
                for (int i = 0; i < mirror_size; i++) {
                    memory_map[block * mirror_size + i] = memory_map[i];
                }
            }
        }
        // If decoded_bits == 16, all address lines are decoded, no mirroring needed
    }
}

bool memory_scan_and_build_map(printf_func_t printf_func) {
    printf_func("Starting memory scan...\r\n");

    // 1. Pause emulator (keep E clock running for bus operations)
    bool was_paused = false;
    if (!post_sm_event(EV_PAUSE_EMULATOR)) {
        printf_func("Error: Failed to pause emulator\r\n");
        return false;
    }
    // Wait for pause to complete
    sm_notification_t notification;
    uint32_t          tries = 0;
    while (!receive_sm_notification(&notification)) {
        sleep_ms(1);
        tries++;
        if (tries > 100) {
            printf_func("Timeout waiting for pause notification\r\n");
            return false;
        }
    }
    if (notification != NOTIF_OK) {
        printf_func("Pause failed\r\n");
        return false;
    }
    was_paused = true;

    // 2. Initialize E clock for bus operations
    eclock_init();

    // 3. Initialize PIO bus cycles for bus operations
    bus_cycle_pio_init();

    // 4. Start E clock for bus operations
    bool eclock_was_running = eclock_is_running();
    if (!eclock_was_running) {
        eclock_start();
    }

    // 4. Scan all 256 pages
    printf_func("Scanning 256 pages...\r\n");
    for (uint16_t page = 0; page < 256; page++) {
        uint16_t addr = page << 8;
        scan_results[page].address = addr;
        scan_results[page].type = fingerprint_page(addr);
    }
    printf_func("Scan complete\r\n");

    // 5. Recognize architecture
    architecture_type_t arch = recognize_architecture(scan_results);
    printf_func("Architecture: %s\r\n", architecture_name(arch));

    // 6. Coalesce regions for better presentation
    coalesce_regions(arch);

    // 7. Build memory map from scan + architecture rules
    build_memory_map_from_scan(coalesced_results, arch, printf_func);

    // 8. Copy ROM contents from bus to flash (use coalesced results)
    if (!copy_rom_contents_from_bus(coalesced_results, printf_func)) {
        printf_func("Warning: Failed to copy ROM contents\r\n");
    }

    // 9. Save memory map to flash
    memory_save_memory_map_to_flash();

    // 10. Stop E clock if we started it
    if (!eclock_was_running) {
        eclock_stop();
    }

    // 11. Resume emulator
    if (was_paused) {
        if (!post_sm_event(EV_RESUME_EMULATOR)) {
            printf_func("Error: Failed to resume emulator\r\n");
            return false;
        }
    }

    printf_func("Memory scan and configuration complete\r\n");
    return true;
}

sanity_result_t memory_sanity_check(void) {
    // Check if we have a valid saved map
    bool has_valid_map = false;
    for (int i = 0; i < 256; i++) {
        if (memory_map[i] != ENTRY_UNMAPPED_BUS) {
            has_valid_map = true;
            break;
        }
    }

    if (!has_valid_map) {
        return SANITY_NO_SAVED_MAP;
    }

    // Initialize E clock for bus operations
    eclock_init();

    // Initialize PIO bus cycles for bus operations
    bus_cycle_pio_init();

    // Start E clock for bus operations
    bool eclock_was_running = eclock_is_running();
    if (!eclock_was_running) {
        eclock_start();
    }

    sanity_result_t result = SANITY_OK;

    // Check RAM pages (sample every 1KB)
    for (uint16_t addr = mem_config.ram_base;
         addr < mem_config.ram_base + mem_config.ram_size && addr < 0x8000;
         addr += 0x400) {
        page_type_t actual = fingerprint_page(addr);
        if (actual != PAGE_RAM && actual != PAGE_CMOS) {
            printf("RAM mismatch at $%04X: expected RAM, got %s\n",
                   addr, page_type_to_string(actual));
            result = SANITY_RAM_MISMATCH;
            break;
        }
    }

    // Check ROM pages (sample every 4KB) - EMPTY or ROM is OK, RAM is error
    if (result == SANITY_OK && mem_config.rom_size > 0) {
        for (uint16_t addr = mem_config.rom_base;
             addr < mem_config.rom_base + mem_config.rom_size && addr < 0x8000;
             addr += 0x1000) {
            page_type_t actual = fingerprint_page(addr);
            if (actual == PAGE_RAM) {
                printf("ROM region mismatch at $%04X: found RAM instead\n", addr);
                result = SANITY_ROM_UNEXPECTED;
                break;
            }
        }
    }

    // Stop E clock if we started it
    if (!eclock_was_running) {
        eclock_stop();
    }

    return result;
}

//--------------------------------------------------------------------+
// ROM Copying
//--------------------------------------------------------------------+

bool copy_rom_contents_from_bus(scan_result_t *results, printf_func_t printf_func) {
    // Clear ROM load buffer first
    memory_clear_rom_load_buffer();

    uint16_t pages_copied = 0;

    // Determine how many pages to scan based on architecture
    // For System 11, scan full 64KB address space since A15 is fully decoded
    // For other systems, only scan low address space to avoid aliases
    architecture_type_t arch = ARCH_UNKNOWN;
    // We need to re-detect architecture since we don't have it passed in
    int ram_pages = 0;
    for (int i = 0; i <= 0x07; i++) {
        if (results[i].type == PAGE_RAM)
            ram_pages++;
    }
    if (ram_pages >= 6) {
        arch = ARCH_WILLIAMS_SYS11;
    }

    int max_pages = (arch == ARCH_WILLIAMS_SYS11) ? 256 : 128;

    // For System 11, copy all pages in ROM range since ROM is continuous
    // For other systems, only copy pages detected as ROM
    if (arch == ARCH_WILLIAMS_SYS11) {
        // Copy all pages in the configured ROM range
        for (uint32_t addr = mem_config.rom_base;
             addr < (uint32_t)mem_config.rom_base + mem_config.rom_size;
             addr += ENTRY_PAGE_SIZE) {

            // Check if this page is actually ROM (skip PIA, etc.)
            uint16_t page = (uint16_t)addr >> 8;
            if (page < 256 && results[page].type != PAGE_ROM && results[page].type != PAGE_EMPTY) {
                // Skip non-ROM pages (PIA, etc.) - leave as 0xFF in flash
                continue;
            }

            // Read 256-byte page from bus
            uint8_t page_buffer[ENTRY_PAGE_SIZE];
            for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
                page_buffer[i] = bus_read_cycle((uint16_t)addr + i);
                busy_wait_us(3);
            }

            // Load into ROM buffer using existing function
            if (memory_load_hex_data((uint16_t)addr, page_buffer, ENTRY_PAGE_SIZE)) {
                pages_copied++;
            } else {
                printf_func("Warning: Failed to load ROM page at $%04X\r\n", (uint16_t)addr);
            }
        }
    } else {
        // For other architectures, only copy pages detected as ROM
        for (int page = 0; page < max_pages; page++) {
            if (results[page].type != PAGE_ROM) {
                continue;
            }

            uint16_t addr = page << 8;

            // Only copy if address is in configured ROM range
            if (addr < mem_config.rom_base ||
                addr >= mem_config.rom_base + mem_config.rom_size) {
                continue;
            }

            // Read 256-byte page from bus
            uint8_t page_buffer[ENTRY_PAGE_SIZE];
            for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
                page_buffer[i] = bus_read_cycle(addr + i);
                busy_wait_us(3);
            }

            // Load into ROM buffer using existing function
            if (memory_load_hex_data(addr, page_buffer, ENTRY_PAGE_SIZE)) {
                pages_copied++;
            } else {
                printf_func("Warning: Failed to load ROM page at $%04X\r\n", addr);
            }
        }
    }

    printf_func("Copied %u ROM pages from bus\r\n", pages_copied);

    // If we copied any ROM data, finalize the load
    if (pages_copied > 0) {
        if (!memory_finalize_load()) {
            printf_func("Error: Failed to finalize ROM load\r\n");
            return false;
        }
        printf_func("ROM contents saved to flash\r\n");
    }

    return true;
}

//--------------------------------------------------------------------+
// Coalescing Logic
//--------------------------------------------------------------------+

// Coalesce regions for cleaner memory map presentation
void coalesce_regions(architecture_type_t arch) {
    const uint16_t SYS11_ROM_START = 0x4000;

    // Copy original results to coalesced results initially
    memcpy(coalesced_results, scan_results, sizeof(scan_results));

    // Coalesce consecutive regions based on architecture
    uint16_t start = 0;
    uint8_t  last_effective_type = scan_results[0].type;

    for (int page = 1; page <= 256; page++) {
        uint8_t  current_type = (page < 256) ? scan_results[page].type : 255;  // 255 = sentinel
        uint32_t current_addr = (uint32_t)page << 8;

        // Determine effective type for coalescing
        uint8_t current_effective_type = current_type;

        // For System 11, treat EMPTY as ROM in ROM space for coalescing
        if (arch == ARCH_WILLIAMS_SYS11 && current_addr >= SYS11_ROM_START && current_type == PAGE_EMPTY) {
            current_effective_type = PAGE_ROM;
        }

        uint32_t last_addr_check = (uint32_t)(page - 1) << 8;
        uint8_t  last_effective_check = last_effective_type;

        // For System 11, treat EMPTY as ROM in ROM space for coalescing
        if (arch == ARCH_WILLIAMS_SYS11 && last_addr_check >= SYS11_ROM_START && last_effective_type == PAGE_EMPTY) {
            last_effective_check = PAGE_ROM;
        }

        if (current_effective_type != last_effective_check || page == 256) {
            uint32_t end = ((uint32_t)page << 8) - 1;

            // For System 11 ROM space regions that were coalesced, determine the correct type
            if (arch == ARCH_WILLIAMS_SYS11 && last_effective_check == PAGE_ROM && start >= SYS11_ROM_START) {
                // Check if this region contains any actual ROM data
                bool has_real_rom = false;
                for (uint16_t check_page = start >> 8; check_page < ((end + 1) >> 8) && check_page < 256; check_page++) {
                    if (scan_results[check_page].type == PAGE_ROM) {
                        has_real_rom = true;
                        break;
                    }
                }

                // Set the coalesced type - if no real ROM, keep as EMPTY
                uint8_t coalesced_type = has_real_rom ? PAGE_ROM : PAGE_EMPTY;

                // Apply the coalesced type to all pages in this region
                for (uint32_t addr = start; addr <= end && (addr >> 8) < 256; addr += 256) {
                    uint16_t page_idx = addr >> 8;
                    if (page_idx < 256) {
                        coalesced_results[page_idx].type = coalesced_type;
                    }
                }
            }
            // For other architectures, just coalesce consecutive regions of the same type
            // (no special processing needed since we're not changing the types)

            start = (uint32_t)page << 8;
            last_effective_type = current_type;
        }
    }
}

// Helper function needed by build_memory_map_from_scan
int detect_address_aliasing(scan_result_t *results) {
    // Check for repeated sequences that indicate non-decoded address lines
    // Start with largest possible alias (A15) and work down

    // Check if A15 is not decoded (first 128 pages == second 128 pages)
    bool a15_aliased = true;
    for (int i = 0; i < 128 && a15_aliased; i++) {
        if (results[i].type != results[i + 128].type) {
            a15_aliased = false;
        }
    }

    if (a15_aliased) {
        // Check if A14 is also not decoded (each 64-page block repeats 4 times)
        bool a14_aliased = true;
        for (int block = 0; block < 4 && a14_aliased; block++) {
            int base = block * 64;
            for (int i = 0; i < 64 && a14_aliased; i++) {
                if (results[base + i].type != results[i].type) {
                    a14_aliased = false;
                }
            }
        }

        if (a14_aliased) {
            // Check if A13 is also not decoded (each 32-page block repeats 8 times)
            bool a13_aliased = true;
            for (int block = 0; block < 8 && a13_aliased; block++) {
                int base = block * 32;
                for (int i = 0; i < 32 && a13_aliased; i++) {
                    if (results[base + i].type != results[i].type) {
                        a13_aliased = false;
                    }
                }
            }

            if (a13_aliased) {
                return 13;  // A13 and below not decoded
            } else {
                return 14;  // A14 and below not decoded
            }
        } else {
            return 15;      // A15 and below not decoded
        }
    }

    return 16;  // All address lines decoded
}

//--------------------------------------------------------------------+
// Scan Results Access
//--------------------------------------------------------------------+

const scan_result_t *memory_get_scan_results(void) {
    return scan_results;
}

const scan_result_t *memory_get_coalesced_scan_results(void) {
    return coalesced_results;
}
