/**
 * MC6800 Memory Subsystem Implementation
 */

#include "memory.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "interrupts.h"
#include "board_config.h"
#include "debug_spi.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include "pico.h"
#include <stdio.h>
#include <string.h>

// LED control helper for NED_SYS7 board (active low - 0=on, 1=off)
// GPIO 37-39 are LED pins, use gpio_put_masked64 for atomic updates (supporting >32 GPIOs)
#if BOARD_TYPE == BOARD_NED_SYS7
// LED GPIO values for masked writes: bit positions correspond to GPIO numbers
#define LED_MASK ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) | (1ULL << GPIO_LED_UNMAPPED))
#define LED_ROM_ON ((0ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) | (1ULL << GPIO_LED_UNMAPPED))  // ROM=0, others=1
#define LED_RAM_ON ((1ULL << GPIO_LED_ROM) | (0ULL << GPIO_LED_RAM) | (1ULL << GPIO_LED_UNMAPPED))  // RAM=0, others=1
#define LED_UNMAPPED_ON ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) | (0ULL << GPIO_LED_UNMAPPED))  // UNMAPPED=0, others=1
#define LED_ALL_OFF ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) | (1ULL << GPIO_LED_UNMAPPED))  // All LEDs off (all=1)

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

// Memory configuration
memory_config_t mem_config;

// Shadow copies for diagnostics and initialization
static uint8_t ram_shadow[MAX_RAM_SIZE];
uint8_t rom_shadow[MAX_ROM_SIZE];  // Fast RAM copy of ROM for execution
static uint8_t rom_load_buffer[MAX_ROM_SIZE];  // Buffer for loading before flash write
static uint8_t cmos_load_buffer[FLASH_SECTOR_SIZE];  // Buffer for CMOS flash operations
static uint64_t cmos_last_write_time = 0;  // Timestamp for deferred save
static bool cmos_autosave_enabled = true;  // Can be disabled during timing-critical operations

// Initialize memory subsystem
void memory_init(void) {
    memset(&mem_config, 0, sizeof(mem_config));
    memset(ram_shadow, 0, sizeof(ram_shadow));
    memset(rom_shadow, 0xFF, sizeof(rom_shadow));
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));
    memset(cmos_load_buffer, 0xFF, sizeof(cmos_load_buffer));

    // Default configuration (A15 not decoded)
    mem_config.rom_base = 0x4000;
    mem_config.rom_size = 0x4000;  // 16KB (4000-7FFF, aliased at C000-FFFF)
    mem_config.ram_base = 0x0000;
    mem_config.ram_size = 0x1400;  // 5KB (0000-13FF)
    mem_config.flash_offset = FLASH_TARGET_OFFSET;
    mem_config.flash_size = mem_config.rom_size;
    mem_config.cmos_base = CMOS_BASE;
    mem_config.cmos_size = CMOS_SIZE;
    mem_config.cmos_flash_offset = CMOS_FLASH_OFFSET;
    mem_config.cmos_dirty = false;
    mem_config.configured = true;  // Use defaults

    printf("Memory initialized: ROM=$%04X-$%04X RAM=$%04X-$%04X\n",
           mem_config.rom_base, mem_config.rom_base + mem_config.rom_size - 1,
           mem_config.ram_base, mem_config.ram_base + mem_config.ram_size - 1);
    printf("  ROM aliasing: A15 not decoded, $%04X-$%04X aliases at $%04X-$%04X\n",
           mem_config.rom_base, mem_config.rom_base + mem_config.rom_size - 1,
           mem_config.rom_base | 0x8000, (mem_config.rom_base | 0x8000) + mem_config.rom_size - 1);
    printf("  Vectors at $FFF8-$FFFF access physical $7FF8-$7FFF\n");
    printf("  RAM mirroring: $0000-$00FF mirrored at $1000-$10FF\n");
    printf("  CMOS RAM: $%04X-$%04X (persistent in flash)\n",
           mem_config.cmos_base, mem_config.cmos_base + mem_config.cmos_size - 1);
    printf("  Unmapped addresses route to physical bus\n");

    // Restore ROM and CMOS from flash
    memory_init_rom_from_flash();
    memory_init_cmos_from_flash();
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

    printf("ROM configured: $%04X-$%04X (%d bytes)\n",
           base, base + size - 1, size);
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

    printf("RAM configured: $%04X-$%04X (%d bytes)\n",
           base, base + size - 1, size);
}

// Get memory type for address
memory_type_t __time_critical_func(memory_get_type)(uint16_t address) {
    // Translate address for missing A15 decode
    uint16_t physical_addr = address & ADDR_MASK_A15;

    // Check ROM range (using translated address)
    if (physical_addr >= mem_config.rom_base &&
        physical_addr < mem_config.rom_base + mem_config.rom_size) {
        return MEM_TYPE_ROM;
    }

    // Check RAM range (uses original address - RAM is at 0x0000-0x13FF)
    if (address >= mem_config.ram_base &&
        address < mem_config.ram_base + mem_config.ram_size) {
            if (address >= mem_config.cmos_base &&
                address < mem_config.cmos_base + mem_config.cmos_size) {
                    return MEM_TYPE_UNMAPPED;
            }
        return MEM_TYPE_RAM;
    }

    // Unmapped (peripheral) address - routes to physical bus
    return MEM_TYPE_UNMAPPED;
}

// Fast-path read (no GPIO polling for ROM/RAM)
uint8_t __time_critical_func(memory_read_fast)(uint16_t address) {
    memory_type_t type = memory_get_type(address);

    // Read from ROM shadow (RAM copy) - fast path
    if (type == MEM_TYPE_ROM) {
        eclock_accumulate(1);  // Track cycle, don't wait
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_rom();
#endif
        return memory_read_rom_shadow(address);
    }

    // Read from RAM shadow (with mirroring) - fast path
    if (type == MEM_TYPE_RAM) {
        eclock_accumulate(1);  // Track cycle, don't wait
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_ram();
#endif

        uint16_t ram_offset = address - mem_config.ram_base;

        // System 7 RAM mirroring: $1000-$10FF mirrors $0000-$00FF
        if (address >= 0x1000 && address <= 0x10FF) {
            ram_offset = address - 0x1000;
        }

        return (ram_offset < mem_config.ram_size) ? ram_shadow[ram_offset] : 0xFF;
    }

    // Unmapped - route to physical bus directly (avoid recursion)
    // E clock management is handled by the caller (e.g., usb_cdc.c)
#if BOARD_TYPE == BOARD_NED_SYS7
    led_set_unmapped();
#endif
    return bus_read_cycle(address);
}

// Fast-path write (no GPIO polling for ROM/RAM)
void __time_critical_func(memory_write_fast)(uint16_t address, uint8_t value) {
    memory_type_t type = memory_get_type(address);

    // Write to RAM shadow (with mirroring) - fast path
    if (type == MEM_TYPE_RAM) {
        eclock_accumulate(1);  // Track cycle, don't wait
#if BOARD_TYPE == BOARD_NED_SYS7
        led_set_ram();
#endif

        uint16_t ram_offset = address - mem_config.ram_base;

        // System 7 RAM mirroring: $1000-$10FF mirrors $0000-$00FF
        if (address >= 0x1000 && address <= 0x10FF) {
            ram_offset = address - 0x1000;
        }

        if (ram_offset < mem_config.ram_size) {
            ram_shadow[ram_offset] = value;

            // Check if this is a write to CMOS region - mark for auto-save
            if (address >= mem_config.cmos_base &&
                address < mem_config.cmos_base + mem_config.cmos_size) {
                // Only call time_us_64() if transitioning from clean to dirty
                // (avoids expensive timer read on every stack write)
                if (!mem_config.cmos_dirty) {
                    cmos_last_write_time = time_us_64();
                    mem_config.cmos_dirty = true;
                }
            }
        }
        return;
    }

    // ROM writes are ignored but still count cycle
    if (type == MEM_TYPE_ROM) {
        eclock_accumulate(1);  // Ignored write, still count cycle
        return;
    }

    // Unmapped - route to physical bus directly (avoid recursion)
    // E clock management is handled by the caller (e.g., usb_cdc.c)
#if BOARD_TYPE == BOARD_NED_SYS7
    led_set_unmapped();
#endif
    bus_write_cycle(address, value);
}

// Load Intel HEX data into ROM load buffer
bool memory_load_hex_data(uint16_t address, const uint8_t *data, uint16_t length) {
    // Check if address is in CMOS range - route to CMOS loader
    if (address >= mem_config.cmos_base &&
        address < mem_config.cmos_base + mem_config.cmos_size) {
        return memory_load_cmos_data(address, data, length);
    }

    // Translate address for missing A15 decode
    // (e.g., $D800 -> $5800, $FFF8 -> $7FF8)
    uint16_t physical_addr = address & ADDR_MASK_A15;

    // Check if address is in ROM range
    if (physical_addr < mem_config.rom_base ||
        physical_addr >= mem_config.rom_base + mem_config.rom_size) {
        printf("HEX address $%04X (physical $%04X) outside ROM/CMOS range\n",
               address, physical_addr);
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
    printf("Writing %u bytes to flash at offset 0x%08lX...\n",
           (unsigned int)mem_config.flash_size, (unsigned long)mem_config.flash_offset);

    // Disable interrupts during flash write
    uint32_t ints = save_and_disable_interrupts();

    // Erase flash sector(s)
    uint32_t erase_size = (mem_config.flash_size + FLASH_SECTOR_SIZE - 1) & ~(FLASH_SECTOR_SIZE - 1);
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

// Initialize CMOS from flash on startup
void memory_init_cmos_from_flash(void) {
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + mem_config.cmos_flash_offset);

    // Check if flash is erased (all 0xFF = new/empty flash)
    bool is_erased = true;
    for (uint16_t i = 0; i < CMOS_SIZE; i++) {
        if (flash_ptr[i] != 0xFF) {
            is_erased = false;
            break;
        }
    }

    if (is_erased) {
        // Initialize CMOS to zeros (default state)
        memset(&ram_shadow[CMOS_BASE], 0, CMOS_SIZE);
        printf("CMOS RAM initialized to zeros (flash empty)\n");
    } else {
        // Restore CMOS from flash
        memcpy(&ram_shadow[CMOS_BASE], flash_ptr, CMOS_SIZE);
        printf("CMOS RAM restored from flash\n");
    }

    mem_config.cmos_dirty = false;
    cmos_last_write_time = 0;
}

// Load Intel HEX data into CMOS shadow copy
bool memory_load_cmos_data(uint16_t address, const uint8_t *data, uint16_t length) {
    // Validate address range
    if (address < mem_config.cmos_base ||
        address >= mem_config.cmos_base + mem_config.cmos_size) {
        printf("CMOS address $%04X outside CMOS range\n", address);
        return false;
    }

    uint16_t cmos_offset = address - mem_config.cmos_base;
    if (cmos_offset + length > mem_config.cmos_size) {
        printf("CMOS data exceeds CMOS size\n");
        return false;
    }

    // Copy to both CMOS load buffer and RAM shadow for immediate use
    memcpy(&cmos_load_buffer[cmos_offset], data, length);
    memcpy(&ram_shadow[address], data, length);

    return true;
}

// Save CMOS RAM to flash
bool memory_save_cmos(void) {
    // Skip if no changes
    if (!mem_config.cmos_dirty) {
        return true;
    }

    printf("Saving CMOS RAM to flash...\n");

    // Copy CMOS from RAM shadow to load buffer
    memcpy(cmos_load_buffer, &ram_shadow[CMOS_BASE], CMOS_SIZE);

    // Fill rest of sector with 0xFF
    memset(&cmos_load_buffer[CMOS_SIZE], 0xFF, FLASH_SECTOR_SIZE - CMOS_SIZE);

    // Disable interrupts during flash write
    uint32_t ints = save_and_disable_interrupts();

    // Erase flash sector (4096 bytes)
    flash_range_erase(mem_config.cmos_flash_offset, FLASH_SECTOR_SIZE);

    // Program flash sector
    flash_range_program(mem_config.cmos_flash_offset, cmos_load_buffer, FLASH_SECTOR_SIZE);

    // Restore interrupts
    restore_interrupts(ints);

    // Verify
    const uint8_t *flash_ptr = (const uint8_t *)(XIP_BASE + mem_config.cmos_flash_offset);
    if (memcmp(flash_ptr, &ram_shadow[CMOS_BASE], CMOS_SIZE) != 0) {
        printf("CMOS flash verification FAILED\n");
        return false;
    }

    printf("CMOS saved successfully\n");
    mem_config.cmos_dirty = false;
    cmos_last_write_time = 0;

    return true;
}

bool memory_is_cmos_autosave_enabled(void) {
    return cmos_autosave_enabled;
}

bool memory_is_cmos_dirty(void) {
    return mem_config.cmos_dirty;
}

// Check if CMOS needs auto-save (call periodically from main loop)
void memory_check_cmos_autosave(void) {
    // Skip if auto-save is disabled (e.g., during timing-critical operations)
    if (!cmos_autosave_enabled) {
        return;
    }

    // Skip if no changes
    if (!mem_config.cmos_dirty) {
        return;
    }

    // Check if enough time has elapsed since last write
    uint64_t now = time_us_64();
    uint64_t elapsed_ms = (now - cmos_last_write_time) / 1000;

    if (elapsed_ms >= CMOS_AUTOSAVE_DELAY_MS) {
        printf("Auto-saving CMOS (idle for %llu ms)...\n", (unsigned long long)elapsed_ms);
        memory_save_cmos();
    }
}

// Enable/disable CMOS auto-save (disable during timing-critical operations)
void memory_set_cmos_autosave_enabled(bool enabled) {
    cmos_autosave_enabled = enabled;
}

// Get CMOS data for diagnostics (direct access to shadow copy)
const uint8_t* memory_get_cmos_shadow(void) {
    return &ram_shadow[CMOS_BASE];
}
