/**
 * MC6800 Memory Subsystem
 * Manages 64KB address space mapping to ROM (flash) and RAM
 * CRITICAL: All accesses go through physical bus for watchdog
 */

#ifndef MEMORY_H
#define MEMORY_H

#include <stdint.h>
#include <stdbool.h>

// Memory region types
typedef enum {
    MEM_TYPE_UNMAPPED,  // Unmapped (peripheral) address
    MEM_TYPE_ROM,       // ROM (EPROM) - read only
    MEM_TYPE_RAM        // RAM - read/write
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
} memory_config_t;

// Initialize memory subsystem
void memory_init(void);

// Configure memory regions (called from USB command)
void memory_configure_rom(uint16_t base, uint16_t size);
void memory_configure_ram(uint16_t base, uint16_t size);

// Get memory type for address
memory_type_t memory_get_type(uint16_t address);

// Read byte from address (via bus)
uint8_t memory_read(uint16_t address);

// Write byte to address (via bus)
void memory_write(uint16_t address, uint8_t value);

// Load Intel HEX data into flash shadow copy
bool memory_load_hex_data(uint16_t address, const uint8_t *data, uint16_t length);

// Finalize EPROM load (commit to flash)
bool memory_finalize_load(void);

// Get ROM data for diagnostics (direct access to shadow copy)
const uint8_t* memory_get_rom_shadow(void);

// Get RAM data for diagnostics (direct access to shadow copy)
const uint8_t* memory_get_ram_shadow(void);

#endif // MEMORY_H
