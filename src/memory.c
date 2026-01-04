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

static inline void led_set_rom(void) { gpio_put_masked64(LED_MASK, LED_ROM_ON); }

static inline void led_set_ram(void) { gpio_put_masked64(LED_MASK, LED_RAM_ON); }

static inline void led_set_unmapped(void) { gpio_put_masked64(LED_MASK, LED_UNMAPPED_ON); }

static inline void led_all_off_inline(void) { gpio_put_masked64(LED_MASK, LED_ALL_OFF); }
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
#define ENTRY_PAGE_SIZE 256
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
uint8_t rom_shadow[MAX_ROM_SIZE]
    __attribute__((aligned(ENTRY_PAGE_SIZE)));            // Fast RAM copy of ROM for execution
static uint8_t rom_load_buffer[MAX_ROM_SIZE]; // Buffer for loading before flash write

void memory_initialize_map(void) {
    // Set all entries to unmapped
    for (uint16_t i = 0; i < MEMORY_TABLE_SIZE; i++) {
        memory_map[i] = ENTRY_UNMAPPED_BUS;
    }
    // Set ROM entries (including aliases)
    for (uint16_t address = mem_config.rom_base;
         address < mem_config.rom_base + mem_config.rom_size; address += ENTRY_PAGE_SIZE) {
        uint16_t phys_addr = address & ADDR_MASK_A15; // low alias
        uint8_t table_index = ADDR_TO_TABLE_INDEX(phys_addr);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&rom_shadow[0] + (phys_addr - mem_config.rom_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_ROM; // mapped ROM
        memory_map[table_index] = table_entry;
        memory_map[(table_index + HIGH_ALIAS_TABLE_OFFSET)] = table_entry; // high alias
    }
    // set RAM entries including aliases
    for (uint16_t address = mem_config.ram_base;
         address < mem_config.ram_base + mem_config.ram_size; address += ENTRY_PAGE_SIZE) {
        uint16_t phys_addr = address & ADDR_MASK_A15; // low alias
        uint8_t table_index = ADDR_TO_TABLE_INDEX(phys_addr);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&ram_shadow[0] + (phys_addr - mem_config.ram_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_RAM; // mapped RAM
        memory_map[table_index] = table_entry;
        memory_map[(table_index + HIGH_ALIAS_TABLE_OFFSET)] = table_entry; // high alias
        // System 7 RAM mirroring: $1000-$10FF mirrors $0000-$00FF
        if (phys_addr < 0x0100) {
            uint8_t mirror_table_index = ADDR_TO_TABLE_INDEX(phys_addr + 0x1000);
            memory_map[mirror_table_index] = table_entry;
            memory_map[mirror_table_index + HIGH_ALIAS_TABLE_OFFSET] = table_entry;
        }
    }
    // set CMOS entries including aliases
    for (uint16_t address = mem_config.cmos_base;
         address < mem_config.cmos_base + mem_config.cmos_size; address += ENTRY_PAGE_SIZE) {
        uint16_t phys_addr = address & ADDR_MASK_A15; // low alias
        uint8_t table_index = ADDR_TO_TABLE_INDEX(phys_addr);
        uint32_t shadow_addr =
            (uint32_t)(uintptr_t)&ram_shadow[0] + (phys_addr - mem_config.ram_base);
        uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_CMOS; // mapped CMOS
        memory_map[table_index] = table_entry;
        memory_map[(table_index + HIGH_ALIAS_TABLE_OFFSET)] = table_entry;
    }
}

// Initialize memory subsystem
void memory_init(void) {
    memset(&mem_config, 0, sizeof(mem_config));
    memset(ram_shadow, 0, sizeof(ram_shadow));
    memset(rom_shadow, 0xFF, sizeof(rom_shadow));
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));

    // Default configuration (A15 not decoded)
    mem_config.rom_base = 0x4000;
    mem_config.rom_size = 0x4000; // 16KB (4000-7FFF, aliased at C000-FFFF)
    mem_config.ram_base = 0x0000;
    mem_config.ram_size = 0x1400; // 5KB (0000-13FF)
    mem_config.flash_offset = FLASH_TARGET_OFFSET;
    mem_config.flash_size = mem_config.rom_size;
    mem_config.cmos_base = CMOS_BASE;
    mem_config.cmos_size = CMOS_SIZE;
    mem_config.configured = true; // Use defaults

    memory_initialize_map();

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
    printf("  Unmapped addresses route to physical bus\n");

    // Restore ROM from flash
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
    uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
    uint8_t offset = ADDR_TO_TABLE_OFFSET(address);
    uint32_t table_entry = memory_map[table_index];
    if (table_entry & ENTRY_UNMAPPED) { // unmapped
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
    eclock_accumulate(1); // Track cycle, don't wait
    uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
    uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
    return *shadow_address;
}

void __time_critical_func(memory_write_fast)(uint16_t address, uint8_t data) {
    uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
    uint8_t offset = ADDR_TO_TABLE_OFFSET(address);
    uint32_t table_entry = memory_map[table_index];
    if (table_entry & ENTRY_UNMAPPED) { // unmapped
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_unmapped();
#endif
        bus_write_cycle(address, data);
        return;
    }
    if (!(table_entry & ENTRY_WRITABLE)) { // Ignore writes to ROM
        eclock_accumulate(1); // Track cycle, don't wait
        return;
    }
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_ram();
#endif
    eclock_accumulate(1); // Track cycle, don't wait
    uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
    uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
    *shadow_address = data;
    if (table_entry & ENTRY_WRITE_THROUGH) { // CMOS write-through
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
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + mem_config.flash_offset);

    // Check if flash is erased (all 0xFF = new/empty flash)
    bool is_erased = true;
    for (uint16_t i = 0; i < mem_config.flash_size; i++) {
        if (flash_ptr[i] != 0xFF) {
            is_erased = false;
            break;
        }
    }

    if (is_erased) {
        // No ROM programmed yet
        printf("ROM not programmed (flash empty)\n");
    } else {
        // Restore ROM from flash to RAM shadow
        memcpy(rom_shadow, flash_ptr, mem_config.flash_size);
        printf("ROM restored from flash (%u bytes)\n", (unsigned int)mem_config.flash_size);
        // Note: Reset vector will be loaded by cpu_init() after this
    }
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
const uint8_t *memory_get_cmos_shadow(void) { return &ram_shadow[CMOS_BASE]; }

void memory_read_cmos_from_bus(void) {
    eclock_start();
    uint16_t address = mem_config.cmos_base;
    for (uint16_t i = 0; i < mem_config.cmos_size; i++) {
        uint8_t value = bus_read_cycle(address);
        ram_shadow[address++] = value;
    }
    eclock_stop();
}
