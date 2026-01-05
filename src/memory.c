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

#define ENTRY_MAPPED 0b000
#define ENTRY_UNMAPPED 0b001
#define ENTRY_RAM 0b010
#define ENTRY_WRITABLE 0b010
#define ENTRY_ROM 0b000
#define ENTRY_WRITE_THROUGH 0b100
#define ENTRY_NO_WRITE_THROUGH 0b000

#define ENTRY_MAPPED_RAM (ENTRY_RAM | ENTRY_NO_WRITE_THROUGH)
#define ENTRY_MAPPED_CMOS (ENTRY_RAM | ENTRY_WRITE_THROUGH)
#define ENTRY_MAPPED_ROM (ENTRY_ROM | ENTRY_NO_WRITE_THROUGH)
#define ENTRY_UNMAPPED_BUS (ENTRY_UNMAPPED | ENTRY_WRITABLE)

#define ENTRY_FLAG_MASK 0b111
#define ENTRY_ADDR_MASK ~0xFFU
#define ADDR_TO_TABLE_INDEX(addr) ((addr) >> 8)
#define ADDR_TO_TABLE_OFFSET(addr) ((addr) & 0xFF)
#define HIGH_ALIAS_TABLE_OFFSET 0x80
#define MEMORY_TABLE_SIZE (0x10000U / ENTRY_PAGE_SIZE)

uint32_t memory_map[MEMORY_TABLE_SIZE];

// Memory configuration
memory_config_t mem_config;

// Shadow copies for diagnostics and initialization
static uint8_t ram_shadow[MAX_RAM_SIZE] __attribute__((aligned(ENTRY_PAGE_SIZE)));
uint8_t        rom_shadow[MAX_ROM_SIZE]
    __attribute__((aligned(ENTRY_PAGE_SIZE)));  // Fast RAM copy of ROM for execution
static uint8_t rom_load_buffer[MAX_ROM_SIZE];   // Buffer for loading before flash write

// ROM mapping bitmap - tracks which 256-byte pages have valid ROM data
// 256 pages maximum, 1 bit per page = 32 bytes
static uint8_t rom_mapping_bitmap[32] = { 0 };

// Flash storage for ROM mapping bitmap (32 bytes)
#define FLASH_MAPPING_OFFSET (FLASH_TARGET_OFFSET + MAX_ROM_SIZE)

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
        uint16_t phys_addr = address & ADDR_MASK_A15;  // low alias
        uint8_t  table_index = ADDR_TO_TABLE_INDEX(phys_addr);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&ram_shadow[0] + (phys_addr - mem_config.ram_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_RAM;  // mapped RAM
        memory_map[table_index] = table_entry;
        memory_map[(table_index + HIGH_ALIAS_TABLE_OFFSET)] = table_entry;          // high alias
        // System 7 RAM mirroring: $1000-$10FF mirrors $0000-$00FF
        if (phys_addr < 0x0100) {
            uint8_t mirror_table_index = ADDR_TO_TABLE_INDEX(phys_addr + 0x1000);
            memory_map[mirror_table_index] = table_entry;
            memory_map[mirror_table_index + HIGH_ALIAS_TABLE_OFFSET] = table_entry;
        }
    }

    // Set CMOS entries including aliases (CMOS is always mapped)
    for (uint16_t address = mem_config.cmos_base;
         address < mem_config.cmos_base + mem_config.cmos_size;
         address += ENTRY_PAGE_SIZE) {
        uint16_t phys_addr = address & ADDR_MASK_A15;  // low alias
        uint8_t  table_index = ADDR_TO_TABLE_INDEX(phys_addr);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&ram_shadow[0] + (phys_addr - mem_config.ram_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_CMOS;  // mapped CMOS
        memory_map[table_index] = table_entry;
        memory_map[(table_index + HIGH_ALIAS_TABLE_OFFSET)] = table_entry;
    }

    // ROM entries will be mapped based on persistent bitmap via
    // memory_update_rom_mapping_from_bitmap() This is called after loading the bitmap from flash
}

// Initialize memory subsystem
void memory_init(void) {
    memset(&mem_config, 0, sizeof(mem_config));
    memset(ram_shadow, 0, sizeof(ram_shadow));
    memset(rom_shadow, 0xFF, sizeof(rom_shadow));
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));

    // Default configuration (A15 not decoded)
    mem_config.rom_base = 0x4000;
    mem_config.rom_size = 0x4000;  // 16KB (4000-7FFF, aliased at C000-FFFF)
    mem_config.ram_base = 0x0000;
    mem_config.ram_size = 0x1400;  // 5KB (0000-13FF)
    mem_config.flash_offset = FLASH_TARGET_OFFSET;
    mem_config.flash_size = mem_config.rom_size;
    mem_config.cmos_base = CMOS_BASE;
    mem_config.cmos_size = CMOS_SIZE;
    mem_config.configured = true;  // Use defaults

    // Load persistent ROM mapping state from flash
    memory_load_rom_mapping_from_flash();

    // Initialize memory map - START WITH ALL ROM REGIONS UNMAPPED
    memory_initialize_map();

    // Update ROM mapping based on persistent bitmap
    memory_update_rom_mapping_from_bitmap();

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
    mem_config.flash_size = size;
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

    memory_read_cmos_from_bus();

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

    // Translate address for missing A15 decode
    // (e.g., $D800 -> $5800, $FFF8 -> $7FF8)
    uint16_t physical_addr = address & ADDR_MASK_A15;

    // Check if address is in ROM range
    if (physical_addr < mem_config.rom_base ||
        physical_addr >= mem_config.rom_base + mem_config.rom_size) {
        printf("HEX address $%04X (physical $%04X) outside ROM/CMOS range\n", address,
               physical_addr);
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
    printf("Writing %u bytes to flash at offset 0x%08lX...\n", (unsigned int)mem_config.flash_size,
           (unsigned long)mem_config.flash_offset);

    // Disable interrupts during flash write
    uint32_t ints = save_and_disable_interrupts();

    // Erase flash sector(s)
    uint32_t erase_size =
        (mem_config.flash_size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
    flash_range_erase(mem_config.flash_offset, erase_size);

    // Program flash
    flash_range_program(mem_config.flash_offset, rom_load_buffer, mem_config.flash_size);

    // Restore interrupts
    restore_interrupts(ints);

    printf("Flash programming complete\n");

    // Verify
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + mem_config.flash_offset);
    if (memcmp(flash_ptr, rom_load_buffer, mem_config.flash_size) != 0) {
        printf("Flash verification FAILED\n");
        return false;
    }

    printf("Flash verification OK\n");

    // Copy ROM to RAM shadow for fast execution
    printf("Copying ROM to RAM shadow for fast access...\n");
    memcpy(rom_shadow, rom_load_buffer, mem_config.flash_size);
    printf("ROM shadow copy complete (%u bytes)\n", (unsigned int)mem_config.flash_size);

    return true;
}

// Initialize ROM from flash on startup
void memory_init_rom_from_flash(void) {
    const uint8_t *src_addr = (const uint8_t *)(XIP_BASE + mem_config.flash_offset);
    uint8_t       *dest_addr = (uint8_t *)rom_shadow;
    uint16_t       bytes_loaded = 0;

    for (uint16_t address = mem_config.rom_base;
         address < mem_config.rom_base + mem_config.rom_size;
         address += ENTRY_PAGE_SIZE) {
        memory_type_t type = memory_get_mapping_type(address);
        if (type == MEM_TYPE_ROM) {
            memcpy(dest_addr, src_addr, ENTRY_PAGE_SIZE);
            bytes_loaded += ENTRY_PAGE_SIZE;
            printf("Restored ROM page at $%04X\n", address);
        } else {
            memset(dest_addr, 0xFF, ENTRY_PAGE_SIZE);
        }
        src_addr += ENTRY_PAGE_SIZE;
        dest_addr += ENTRY_PAGE_SIZE;
    }

    printf("ROM restored from flash (%u/%u bytes)\n",
           bytes_loaded, (unsigned int)mem_config.flash_size);
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
    return &ram_shadow[CMOS_BASE];
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
                (unsigned long)mem_config.flash_offset, (unsigned int)mem_config.flash_size);
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

// Helper function to convert address to bitmap index
static inline uint8_t address_to_bitmap_index(uint16_t address) {
    // Translate address for missing A15 decode
    uint16_t physical_addr = address & ADDR_MASK_A15;
    // Calculate which 256-byte page this address falls in
    uint16_t page_offset = physical_addr - mem_config.rom_base;
    uint8_t  page_index = page_offset / ENTRY_PAGE_SIZE;
    return page_index;
}

// Helper function to set/clear bit in bitmap
static inline void set_bitmap_bit(uint8_t *bitmap, uint8_t index, bool value) {
    uint8_t byte_index = index / 8;
    uint8_t bit_index = index % 8;
    if (value) {
        bitmap[byte_index] |= (1 << bit_index);
    } else {
        bitmap[byte_index] &= ~(1 << bit_index);
    }
}

// Helper function to get bit from bitmap
static inline bool get_bitmap_bit(const uint8_t *bitmap, uint8_t index) {
    uint8_t byte_index = index / 8;
    uint8_t bit_index = index % 8;
    return (bitmap[byte_index] & (1 << bit_index)) != 0;
}

// Set ROM mapping for specific address (256-byte page)
void memory_set_rom_mapping(uint16_t address, bool mapped) {
    uint8_t page_index = address_to_bitmap_index(address);

    if (page_index >= (mem_config.rom_size / ENTRY_PAGE_SIZE)) {
        printf("Address $%04X outside ROM range for mapping (index %u)\n", address, page_index);
        return;
    }

    // Update bitmap
    set_bitmap_bit(rom_mapping_bitmap, page_index, mapped);

    // Update memory map
    uint16_t physical_addr = address & ADDR_MASK_A15;
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(physical_addr);

    if (mapped) {
        // Map this page as ROM
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&rom_shadow[0] + (physical_addr - mem_config.rom_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_ROM;
        memory_map[table_index] = table_entry;
        memory_map[table_index + HIGH_ALIAS_TABLE_OFFSET] = table_entry;

        printf("Mapped ROM page at $%04X (page %u)\n", physical_addr & ~0xFF, page_index);
    } else {
        // Unmap this page (route to bus)
        memory_map[table_index] = ENTRY_UNMAPPED_BUS;
        memory_map[table_index + HIGH_ALIAS_TABLE_OFFSET] = ENTRY_UNMAPPED_BUS;
    }
}

// Update ROM mapping from bitmap (called during initialization)
void memory_update_rom_mapping_from_bitmap(void) {
    uint16_t total_pages = mem_config.rom_size / ENTRY_PAGE_SIZE;

    for (uint16_t page_index = 0; page_index < total_pages; page_index++) {
        uint16_t address = mem_config.rom_base + (page_index * ENTRY_PAGE_SIZE);
        bool     should_be_mapped = get_bitmap_bit(rom_mapping_bitmap, page_index);

        if (should_be_mapped) {
            memory_set_rom_mapping(address, true);
        } else {
            memory_set_rom_mapping(address, false);
        }
    }
}

// Save ROM mapping bitmap to flash
void memory_save_rom_mapping_to_flash(void) {
    // Erase and write mapping bitmap to flash
    flash_range_erase(FLASH_MAPPING_OFFSET, 256);  // Erase 256 bytes (one sector)
    flash_range_program(FLASH_MAPPING_OFFSET, rom_mapping_bitmap, sizeof(rom_mapping_bitmap));
    printf("ROM mapping bitmap saved to flash\n");
}

// Load ROM mapping bitmap from flash
void memory_load_rom_mapping_from_flash(void) {
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + FLASH_MAPPING_OFFSET);

    // Check if mapping data exists (not all 0xFF)
    bool has_data = false;
    for (uint32_t i = 0; i < sizeof(rom_mapping_bitmap); i++) {
        if (flash_ptr[i] != 0xFF) {
            has_data = true;
            break;
        }
    }

    if (has_data) {
        memcpy(rom_mapping_bitmap, flash_ptr, sizeof(rom_mapping_bitmap));
        printf("ROM mapping bitmap loaded from flash\n");
    } else {
        memset(rom_mapping_bitmap, 0, sizeof(rom_mapping_bitmap));
        printf("No ROM mapping data found in flash, starting with all pages unmapped\n");
    }
}

// Clear all ROM mapping (set all pages to unmapped)
void memory_clear_rom_mapping(void) {
    memset(rom_mapping_bitmap, 0, sizeof(rom_mapping_bitmap));
    memory_update_rom_mapping_from_bitmap();
    memory_save_rom_mapping_to_flash();
    printf("All ROM pages unmapped\n");
}

void memory_read_cmos_from_bus(void) {
    eclock_start();
    uint16_t address = mem_config.cmos_base;
    for (uint16_t i = 0; i < mem_config.cmos_size; i++) {
        uint8_t value = bus_read_cycle(address);
        ram_shadow[address++] = value;
    }
    eclock_stop();
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

/**
 * Check if an address is currently mapped (not unmapped)
 * Returns true if the address is mapped to ROM, RAM, or CMOS
 */
bool memory_is_address_mapped(uint16_t address) {
    uint8_t  table_index = ADDR_TO_TABLE_INDEX(address);
    uint32_t table_entry = memory_map[table_index];
    return !(table_entry & ENTRY_UNMAPPED);
}
