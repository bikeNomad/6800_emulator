/**
 * MC6800 Memory Subsystem Implementation
 */

#include "memory.h"
#include "bus.h"
#include "hardware/flash.h"
#include "hardware/sync.h"
#include <stdio.h>
#include <string.h>

// Flash storage for ROM (at the end of program flash)
#define FLASH_TARGET_OFFSET (1024 * 1024)  // 1MB offset (adjust based on program size)
#define MAX_ROM_SIZE (32 * 1024)  // 32KB max ROM
#define MAX_RAM_SIZE 1024  // 1KB max RAM

// Memory configuration
static memory_config_t mem_config;

// Shadow copies for diagnostics and initialization
static uint8_t ram_shadow[MAX_RAM_SIZE];
static uint8_t rom_load_buffer[MAX_ROM_SIZE];  // Buffer for loading before flash write

// Initialize memory subsystem
void memory_init(void) {
    memset(&mem_config, 0, sizeof(mem_config));
    memset(ram_shadow, 0, sizeof(ram_shadow));
    memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));

    // Default configuration (typical Williams pinball)
    mem_config.rom_base = 0xE000;
    mem_config.rom_size = 0x2000;  // 8KB
    mem_config.ram_base = 0x0000;
    mem_config.ram_size = 0x0200;  // 512 bytes
    mem_config.flash_offset = FLASH_TARGET_OFFSET;
    mem_config.flash_size = mem_config.rom_size;
    mem_config.configured = false;

    printf("Memory initialized: ROM=$%04X-$%04X RAM=$%04X-$%04X\n",
           mem_config.rom_base, mem_config.rom_base + mem_config.rom_size - 1,
           mem_config.ram_base, mem_config.ram_base + mem_config.ram_size - 1);
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
memory_type_t memory_get_type(uint16_t address) {
    // Check ROM range
    if (address >= mem_config.rom_base &&
        address < mem_config.rom_base + mem_config.rom_size) {
        return MEM_TYPE_ROM;
    }

    // Check RAM range
    if (address >= mem_config.ram_base &&
        address < mem_config.ram_base + mem_config.ram_size) {
        return MEM_TYPE_RAM;
    }

    // Unmapped (peripheral) address
    return MEM_TYPE_UNMAPPED;
}

// Read byte from address via bus
// CRITICAL: Must go through physical bus for watchdog
uint8_t memory_read(uint16_t address) {
    // Perform bus read cycle
    uint8_t value = bus_read_cycle(address);

    // Update shadow copy for diagnostics
    memory_type_t type = memory_get_type(address);
    if (type == MEM_TYPE_RAM) {
        uint16_t ram_offset = address - mem_config.ram_base;
        if (ram_offset < mem_config.ram_size) {
            ram_shadow[ram_offset] = value;
        }
    }

    return value;
}

// Write byte to address via bus
// CRITICAL: Must go through physical bus for watchdog
void memory_write(uint16_t address, uint8_t value) {
    memory_type_t type = memory_get_type(address);

    // Update shadow copy
    if (type == MEM_TYPE_RAM) {
        uint16_t ram_offset = address - mem_config.ram_base;
        if (ram_offset < mem_config.ram_size) {
            ram_shadow[ram_offset] = value;
        }
    }

    // Perform bus write cycle (even for ROM attempts - they'll just be ignored by hardware)
    bus_write_cycle(address, value);
}

// Load Intel HEX data into ROM load buffer
bool memory_load_hex_data(uint16_t address, const uint8_t *data, uint16_t length) {
    // Check if address is in ROM range
    if (address < mem_config.rom_base ||
        address >= mem_config.rom_base + mem_config.rom_size) {
        printf("HEX address $%04X outside ROM range\n", address);
        return false;
    }

    uint16_t rom_offset = address - mem_config.rom_base;
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
    return true;
}

// Get ROM shadow for diagnostics
const uint8_t* memory_get_rom_shadow(void) {
    // Return pointer to flash storage
    return (const uint8_t *)(XIP_BASE + mem_config.flash_offset);
}

// Get RAM shadow for diagnostics
const uint8_t* memory_get_ram_shadow(void) {
    return ram_shadow;
}
