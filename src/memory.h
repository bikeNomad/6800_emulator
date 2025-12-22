/**
 * MC6800 Memory Subsystem
 * Manages 64KB address space mapping to ROM (flash) and RAM
 * CRITICAL: All accesses go through physical bus for watchdog
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

// Address translation for missing A15 decode
#define ADDR_MASK_A15 0x7FFF  // Mask off A15

// Memory region types
typedef enum {
    MEM_TYPE_UNMAPPED,  // Unmapped (peripheral) address - routes to physical bus
    MEM_TYPE_ROM,       // ROM (EPROM) - read only from flash
    MEM_TYPE_RAM        // RAM - read/write from shadow
} memory_type_t;

// Memory configuration
typedef struct {
    uint32_t flash_offset;  // Offset in RP2350 flash for ROM shadow copy
    uint32_t flash_size;    // Size of ROM image
    uint16_t rom_base;      // Base address in MC6800 space (e.g., $E000)
    uint16_t rom_size;      // Size of ROM region
    uint16_t ram_base;      // Base address of RAM (e.g., $0000)
    uint16_t ram_size;      // Size of RAM (e.g., 512 bytes)
    bool configured;        // Configuration complete
    uint16_t cmos_base;     // Base address of CMOS RAM (0x0100)
    uint16_t cmos_size;     // Size of CMOS RAM (256 bytes)
    uint32_t cmos_flash_offset;  // Offset in flash for CMOS storage
    bool cmos_dirty;        // CMOS has unsaved changes
} memory_config_t;

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

// Get ROM data for diagnostics (direct access to shadow copy)
const uint8_t* memory_get_rom_shadow(void);

// Get RAM data for diagnostics (direct access to shadow copy)
const uint8_t* memory_get_ram_shadow(void);

// Load Intel HEX data into CMOS shadow copy
bool memory_load_cmos_data(uint16_t address, const uint8_t *data, uint16_t length);

// Save CMOS RAM to flash
bool memory_save_cmos(void);

// Initialize ROM from flash on startup
void memory_init_rom_from_flash(void);

// Initialize CMOS from flash on startup
void memory_init_cmos_from_flash(void);

// Check if CMOS needs auto-save (call periodically from main loop)
void memory_check_cmos_autosave(void);

// Enable/disable CMOS auto-save (disable during timing-critical operations)
void memory_set_cmos_autosave_enabled(bool enabled);

// Get CMOS data for diagnostics (direct access to shadow copy)
const uint8_t* memory_get_cmos_shadow(void);

// Turn off all LEDs (for NED_SYS7 board)
void led_all_off(void);

#endif // MEMORY_H
