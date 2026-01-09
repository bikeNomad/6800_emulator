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

/**
 * Get startup status for displaying warnings via USB CDC
 */
sanity_result_t memory_get_startup_status(void) {
    return startup_status;
}

// Clear ROM load buffer (for copy_roms command)
void memory_clear_rom_load_buffer(void) {
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));
}

// For each RAM block, load ram_shadow from bus
void memory_read_ram_from_bus(void) {
    // TODO
}
