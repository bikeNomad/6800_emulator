/**
 * MC6800 Memory Subsystem Implementation
 */

#include "memory.h"
#include "board_config.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "debug_spi.h"
#include "hardware/flash.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "interrupts.h"
#include "pico.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

// LED control helper for NED_SYS7 board (active low - 0=on, 1=off)
// GPIO 37-39 are LED pins, use gpio_put_masked64 for atomic updates (supporting
// >32 GPIOs)
#if BOARD_TYPE == BOARD_NED_SYS7
// LED GPIO values for masked writes: bit positions correspond to GPIO numbers
#define LED_MASK ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) | (1ULL << GPIO_LED_UNMAPPED))
#define LED_ROM_ON                                                                                 \
    ((0ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) |                                             \
     (1ULL << GPIO_LED_UNMAPPED)) // ROM=0, others=1
#define LED_RAM_ON                                                                                 \
    ((1ULL << GPIO_LED_ROM) | (0ULL << GPIO_LED_RAM) |                                             \
     (1ULL << GPIO_LED_UNMAPPED)) // RAM=0, others=1
#define LED_UNMAPPED_ON                                                                            \
    ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) |                                             \
     (0ULL << GPIO_LED_UNMAPPED)) // UNMAPPED=0, others=1
#define LED_ALL_OFF                                                                                \
    ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) |                                             \
     (1ULL << GPIO_LED_UNMAPPED)) // All LEDs off (all=1)

static inline void led_set_rom(void) {
    gpio_put_masked64(LED_MASK, LED_ROM_ON);
}

static inline void led_set_ram(void) {
    gpio_put_masked64(LED_MASK, LED_RAM_ON);
}

static inline void led_set_unmapped(void) {
    gpio_put_masked64(LED_MASK, LED_UNMAPPED_ON);
}

static inline void led_all_off_inline(void) {
    gpio_put_masked64(LED_MASK, LED_ALL_OFF);
}
#endif

// Public function to turn off all LEDs (for use outside memory.c)
void led_all_off(void) {
#if BOARD_TYPE == BOARD_NED_SYS7
    led_all_off_inline();
#endif
}

/* Memory map
The memory_map is a table of 256 entries (each representing a 256-byte page of
6800 address space). Each entry is a 32-bit word, set up like this:
bit[31:8] top 24 bits of shadow base address (assumes 256-byte alignment of shadow areas)
bit[2] flag for write-through (CMOS)
bit[1] 1: writable/RAM, 0: ROM
bit[0] flag for mapped/unmapped (0: mapped, 1: unmapped)

So the bottom 3 bits are encoded like this:
0b000   mapped ROM (read from ROM shadow)
0b001   unmapped (read/write bus)
0b010   mapped RAM (read/write from/to RAM shadow)
0b110   mapped CMOS RAM (read from RAM shadow, write to both shadow and bus)
*/

// Memory map entry flags (local definitions not exported)
#define ENTRY_UNMAPPED 0b001
#define ENTRY_WRITABLE 0b010
#define ENTRY_WRITE_THROUGH 0b100

#define ADDR_TO_TABLE_OFFSET(addr) ((addr) & 0xFF)
#define HIGH_ALIAS_TABLE_OFFSET 0x80
#define MEMORY_TABLE_SIZE (0x10000U / ENTRY_PAGE_SIZE)

uint32_t memory_map[MEMORY_TABLE_SIZE];

// Memory configuration
memory_config_t mem_config;

// Shadow copies for diagnostics and initialization
uint8_t ram_shadow[MAX_RAM_SIZE] __attribute__((aligned(ENTRY_PAGE_SIZE)));  // Exported for fingerprint.c
uint8_t rom_shadow[MAX_ROM_SIZE]
    __attribute__((aligned(ENTRY_PAGE_SIZE)));                               // Fast RAM copy of ROM for execution
static uint8_t rom_load_buffer[MAX_ROM_SIZE];                                // Buffer for loading before flash write

// Startup status (for displaying warnings via USB CDC after boot)
static sanity_result_t startup_status = SANITY_OK;

// Memory map persistence - no bitmap needed anymore

// Flash storage for memory config and map
#define FLASH_MEMORY_CONFIG_OFFSET (FLASH_TARGET_OFFSET + MAX_ROM_SIZE)
#define FLASH_MEMORY_CONFIG_SIZE sizeof(memory_config_t)
#define FLASH_MEMORY_CONFIG_PADDED_SIZE 256  // Config must be padded to 256 bytes for flash_range_program
#define FLASH_MEMORY_MAP_OFFSET (FLASH_MEMORY_CONFIG_OFFSET + FLASH_MEMORY_CONFIG_PADDED_SIZE)
#define FLASH_MEMORY_MAP_SIZE (MEMORY_TABLE_SIZE * sizeof(uint32_t))

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

// Initialize memory subsystem
void memory_init(void) {
    memset(&mem_config, 0, sizeof(mem_config));
    memset(ram_shadow, 0, sizeof(ram_shadow));
    memset(rom_shadow, 0xFF, sizeof(rom_shadow));
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));

    // Default configuration to SYS7 (A15 not decoded)
    mem_config.rom_base = 0x4000;
    mem_config.rom_size = 0x4000;    // 16KB (4000-7FFF, aliased at C000-FFFF)
    mem_config.ram_base = 0x0000;
    mem_config.ram_size = 0x1400;    // 5KB (0000-13FF)
    mem_config.cmos_base = CMOS_BASE;
    mem_config.cmos_size = CMOS_SIZE;
    mem_config.alias_offset = 0x80;  // Default: high alias at +0x80
    mem_config.configured = true;    // Use defaults

    // Try to load complete memory map from flash
    memory_load_memory_map_from_flash();

    // Check if we have a valid saved map
    bool has_valid_map = false;
    for (int i = 0; i < 256; i++) {
        if (memory_map[i] != ENTRY_UNMAPPED_BUS) {
            has_valid_map = true;
            break;
        }
    }

    if (!has_valid_map) {
        printf("No valid memory map found. Using defaults.\n");
        printf("Run 'scan_memory' command to auto-configure.\n");
        startup_status = SANITY_NO_SAVED_MAP;
        memory_initialize_map();
    } else {
        printf("Loaded memory map from flash\n");
        // Don't run sanity check at boot - it requires E clock and bus operations
        // User can run 'verify_memory' command later
        startup_status = SANITY_OK;
    }

    printf("Memory initialized: ROM=$%04X-$%04X RAM=$%04X-$%04X\n", mem_config.rom_base,
           mem_config.rom_base + mem_config.rom_size - 1, mem_config.ram_base,
           mem_config.ram_base + mem_config.ram_size - 1);
    printf("  ROM aliasing: A15 not decoded, $%04X-$%04X aliases at $%04X-$%04X\n",
           mem_config.rom_base, mem_config.rom_base + mem_config.rom_size - 1,
           mem_config.rom_base | 0x8000, (mem_config.rom_base | 0x8000) + mem_config.rom_size - 1);
    printf("  Vectors at $FFF8-$FFFF access physical $7FF8-$7FFF\n");
    printf("  RAM mirroring: $0000-$00FF mirrored at $1000-$10FF\n");
    printf("  CMOS RAM: $%04X-$%04X\n", mem_config.cmos_base,
           mem_config.cmos_base + mem_config.cmos_size - 1);
    printf("  Initially all ROM regions are unmapped (will be mapped as data is loaded)\n");

    // Restore ROM from flash if present
    memory_init_rom_from_flash();
}

// Get ROM configuration
void memory_get_rom_config(uint16_t *base, uint16_t *size) {
    *base = mem_config.rom_base;
    *size = mem_config.rom_size;
}

// Get RAM configuration
void memory_get_ram_config(uint16_t *base, uint16_t *size) {
    *base = mem_config.ram_base;
    *size = mem_config.ram_size;
}

// Configure ROM region
void memory_configure_rom(uint16_t base, uint16_t size) {
    if (size > MAX_ROM_SIZE) {
        printf("ROM size %d exceeds maximum %d\n", size, MAX_ROM_SIZE);
        return;
    }

    mem_config.rom_base = base;
    mem_config.rom_size = size;
    mem_config.configured = true;

    memory_initialize_map();

    printf("ROM configured: $%04X-$%04X (%d bytes)\n", base, base + size - 1, size);
}

// Configure RAM region
void memory_configure_ram(uint16_t base, uint16_t size) {
    if (size > MAX_RAM_SIZE) {
        printf("RAM size %d exceeds maximum %d\n", size, MAX_RAM_SIZE);
        return;
    }

    mem_config.ram_base = base;
    mem_config.ram_size = size;
    mem_config.configured = true;

    // Clear RAM shadow
    memset(ram_shadow, 0, size);

    memory_initialize_map();

    memory_read_ram_from_bus();

    printf("RAM configured: $%04X-$%04X (%d bytes)\n", base, base + size - 1, size);
}

uint8_t __time_critical_func(memory_read_fast)(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint8_t  offset = ADDR_TO_TABLE_OFFSET(address);
    uint32_t table_entry = memory_map[table_index];
    if (table_entry & ENTRY_UNMAPPED) {  // unmapped
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_unmapped();
#endif
        return bus_read_cycle(address);
    }
#if BOARD_TYPE == BOARD_NED_SYS7
    if (table_entry & ENTRY_WRITABLE) {
        led_set_ram();
    } else {
        led_set_rom();
    }
#endif
    eclock_accumulate(1);  // Track cycle, don't wait
    uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
    uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
    return *shadow_address;
}

void __time_critical_func(memory_write_fast)(uint16_t address, uint8_t data) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint8_t  offset = ADDR_TO_TABLE_OFFSET(address);
    uint32_t table_entry = memory_map[table_index];
    if (table_entry & ENTRY_UNMAPPED) {  // unmapped
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_unmapped();
#endif
        bus_write_cycle(address, data);
        return;
    }
    if (!(table_entry & ENTRY_WRITABLE)) {  // Ignore writes to ROM
        eclock_accumulate(1);               // Track cycle, don't wait
        return;
    }
#if BOARD_TYPE == BOARD_NED_SYS7
    led_set_ram();
#endif
    eclock_accumulate(1);  // Track cycle, don't wait
    uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
    uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
    *shadow_address = data;
    if (table_entry & ENTRY_WRITE_THROUGH) {  // CMOS write-through
        bus_write_cycle(address, data);
    }
}

// Load Intel HEX data into ROM load buffer
bool memory_load_hex_data(uint16_t address, const uint8_t *data, uint16_t length) {
    // Check if address is in CMOS range - route to CMOS loader
    if (address >= mem_config.cmos_base && address < mem_config.cmos_base + mem_config.cmos_size) {
        return memory_load_cmos_data(address, data, length);
    }

    // Memory map now handles address aliasing, so use logical address directly
    uint16_t physical_addr = address;

    // Check if address is in ROM range
    if (physical_addr < mem_config.rom_base ||
        physical_addr >= mem_config.rom_base + mem_config.rom_size) {
        printf("HEX address $%04X outside ROM/CMOS range\n", address);
        return false;
    }

    uint16_t rom_offset = physical_addr - mem_config.rom_base;
    if (rom_offset + length > mem_config.rom_size) {
        printf("HEX data exceeds ROM size\n");
        return false;
    }

    // Copy to ROM load buffer
    memcpy(&rom_load_buffer[rom_offset], data, length);

    return true;
}

// Finalize EPROM load (write buffer to flash)
bool memory_finalize_load(void) {
    printf("Writing %u bytes to flash at offset 0x%08lX...\n", (unsigned int)mem_config.rom_size,
           (unsigned long)FLASH_TARGET_OFFSET);

    // Disable interrupts during flash write
    uint32_t ints = save_and_disable_interrupts();

    // Erase flash sector(s)
    uint32_t erase_size =
        (mem_config.rom_size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    flash_range_erase(FLASH_TARGET_OFFSET, erase_size);

    // Program flash
    flash_range_program(FLASH_TARGET_OFFSET, rom_load_buffer, mem_config.rom_size);

    // Flush cache after flash programming
    flash_flush_cache();

    // Restore interrupts
    restore_interrupts(ints);

    printf("Flash programming complete\n");

    // Verify
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    if (memcmp(flash_ptr, rom_load_buffer, mem_config.rom_size) != 0) {
        printf("Flash verification FAILED\n");
        return false;
    }

    printf("Flash verification OK\n");

    // Copy ROM to RAM shadow for fast execution
    printf("Copying ROM to RAM shadow for fast access...\n");
    memcpy(rom_shadow, rom_load_buffer, mem_config.rom_size);
    printf("ROM shadow copy complete (%u bytes)\n", (unsigned int)mem_config.rom_size);

    return true;
}

// Initialize ROM from flash on startup
void memory_init_rom_from_flash(void) {
    const uint8_t *flash_base = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);
    uint16_t       bytes_loaded = 0;

    // Simply load ALL ROM data from flash contiguously - the flash contains
    // all pages from rom_base to rom_base+rom_size stored contiguously
    memcpy(rom_shadow, flash_base, mem_config.rom_size);
    bytes_loaded = mem_config.rom_size;

    printf("ROM restored from flash (%u bytes)\n", bytes_loaded);
}

// Load Intel HEX data into CMOS shadow copy
bool memory_load_cmos_data(uint16_t address, const uint8_t *data, uint16_t length) {
    // Validate address range
    if (address < mem_config.cmos_base || address >= mem_config.cmos_base + mem_config.cmos_size) {
        printf("CMOS address $%04X outside CMOS range\n", address);
        return false;
    }

    uint16_t cmos_offset = address - mem_config.cmos_base;
    if (cmos_offset + length > mem_config.cmos_size) {
        printf("CMOS data exceeds CMOS size\n");
        return false;
    }

    // Copy to RAM shadow for immediate use
    memcpy(&ram_shadow[address], data, length);

    return true;
}

// Get CMOS data for diagnostics (direct access to shadow copy)
const uint8_t *memory_get_cmos_shadow(void) {
    return &ram_shadow[mem_config.cmos_base];
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

// Clear ROM load buffer (for copy_roms command)
void memory_clear_rom_load_buffer(void) {
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));
}

// For each RAM block, load ram_shadow from bus
void memory_read_ram_from_bus(void) {
    // TODO
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

/**
 * Get startup status for displaying warnings via USB CDC
 */
sanity_result_t memory_get_startup_status(void) {
    return startup_status;
}
