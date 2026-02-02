/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

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
#include "memory_map.h"
#include "pico.h"
#include "pico/time.h"
#include <stdio.h>
#include <string.h>

// LED control for NED_SYS7 board (active low - 0=on, 1=off)
// GPIO 37-39 are LED pins, use gpio_put_masked64 for atomic updates (>32 GPIOs)
#if BOARD_TYPE == BOARD_NED_SYS7
#define LED_MASK    ((1ULL << GPIO_LED_ROM) | (1ULL << GPIO_LED_RAM) | (1ULL << GPIO_LED_UNMAPPED))
#define LED_ALL_OFF (LED_MASK) // All LEDs off (all bits = 1)
#endif

// Public function to turn off all LEDs (for use outside memory.c)
void led_all_off(void)
{
#if BOARD_TYPE == BOARD_NED_SYS7
	gpio_put_masked64(LED_MASK, LED_ALL_OFF);
#endif
}

memory_config_t mem_config;

// Shadow copies for diagnostics and initialization
uint8_t ram_shadow[MAX_RAM_SIZE]
	__attribute__((aligned(ENTRY_PAGE_SIZE))); // Exported for fingerprint.c
uint8_t rom_shadow[MAX_ROM_SIZE]
	__attribute__((aligned(ENTRY_PAGE_SIZE))); // Fast RAM copy of ROM for execution
static uint8_t rom_load_buffer[MAX_ROM_SIZE];      // Buffer for loading before flash write

// Initialize memory subsystem
void memory_init(void)
{
	memset(&mem_config, 0, sizeof(mem_config));
	memset(ram_shadow, 0, sizeof(ram_shadow));
	memset(rom_shadow, 0xFF, sizeof(rom_shadow));
	memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));

	// Try to load complete memory map and memory config from flash
	bool has_valid_map = memory_load_memory_map_from_flash();

	if (!has_valid_map) {
		printf("No valid memory map found. Using defaults.\n");
		printf("Run 'scan_memory' command to auto-configure.\n");
		memory_initialize_map();
	} else {
		printf("Loaded memory map from flash\n");
		// Restore ROM from flash if present
		memory_init_rom_from_flash();
		memory_read_ram_from_bus();
	}

	printf("Memory initialized: ROM=$%04X-$%04X RAM=$%04X-$%04X\n", mem_config.rom_base,
	       mem_config.rom_base + mem_config.rom_size - 1, mem_config.ram_base,
	       mem_config.ram_base + mem_config.ram_size - 1);
}

// Get ROM configuration
void memory_get_rom_config(uint16_t *base, uint16_t *size)
{
	*base = mem_config.rom_base;
	*size = mem_config.rom_size;
}

// Get RAM configuration
void memory_get_ram_config(uint16_t *base, uint16_t *size)
{
	*base = mem_config.ram_base;
	*size = mem_config.ram_size;
}

// Configure ROM region
void memory_configure_rom(uint16_t base, uint16_t size)
{
	if (size > MAX_ROM_SIZE) {
		printf("ROM size %d exceeds maximum %d\n", size, MAX_ROM_SIZE);
		return;
	}

	mem_config.rom_base = base;
	mem_config.rom_size = size;

	memory_update_map();

	// Save updated memory map to flash
	memory_save_memory_map_to_flash();

	printf("ROM configured: $%04X-$%04X (%d bytes)\n", base, base + size - 1, size);
}

// Configure RAM region
void memory_configure_ram(uint16_t base, uint16_t size)
{
	if (size > MAX_RAM_SIZE) {
		printf("RAM size %d exceeds maximum %d\n", size, MAX_RAM_SIZE);
		return;
	}

	mem_config.ram_base = base;
	mem_config.ram_size = size;

	// Clear RAM shadow
	memset(ram_shadow, 0, size);

	memory_update_map();

	// Save updated memory map to flash
	memory_save_memory_map_to_flash();

	memory_read_ram_from_bus();

	printf("RAM configured: $%04X-$%04X (%d bytes)\n", base, base + size - 1, size);
}

// Load Intel HEX data into ROM load buffer or into RAM shadow
// Return true if ROM was loaded, false if out of bounds or more than one type
bool memory_load_data(uint16_t address, const uint8_t *data, uint16_t length)
{
	uint16_t start_addr = memory_get_unaliased_address(address);
	uint16_t end_addr = (uint16_t)((uint32_t)start_addr + length - 1);
	memory_type_t start_type = memory_get_type(start_addr);
	memory_type_t end_type = memory_get_type(end_addr);

	if (start_type == MEM_TYPE_ROM && end_type == MEM_TYPE_ROM) {
		uint16_t rom_offset = start_addr - mem_config.rom_base;
		while (length > 0) {
			rom_load_buffer[rom_offset] = *data;
			data++;
			length--;
			rom_offset++;
		}
		return true;
	}

	if ((start_type == MEM_TYPE_RAM) && (end_type == MEM_TYPE_RAM)) {
		uint16_t ram_offset = start_addr - mem_config.ram_base;
		while (length > 0) {
			ram_shadow[ram_offset] = *data;
			data++;
			length--;
			ram_offset++;
		}
		return false;
	}

	return false;
}

// Finalize EPROM load (write buffer to flash)
bool memory_finalize_load(void)
{
	printf("Writing %u bytes to flash at offset 0x%08lX...\n",
	       (unsigned int)mem_config.rom_size, (unsigned long)FLASH_TARGET_OFFSET);

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
void memory_init_rom_from_flash(void)
{
	const uint8_t *flash_base = (const uint8_t *)(XIP_BASE + FLASH_TARGET_OFFSET);

	// Simply load ALL ROM data from flash contiguously - the flash contains
	// all pages from rom_base to rom_base+rom_size stored contiguously
	memcpy(rom_shadow, flash_base, mem_config.rom_size);

	printf("ROM restored from flash (%u bytes)\n", mem_config.rom_size);
}

// Clear ROM load buffer (for copy_roms command)
void memory_clear_rom_load_buffer(void)
{
	memset(rom_load_buffer, 0xFF, sizeof(rom_load_buffer));
}

// Load ram_shadow from bus
void memory_read_ram_from_bus(void)
{
	bus_read_block_with_eclock(mem_config.ram_base, mem_config.ram_size, ram_shadow);
	printf("RAM read from bus (%u bytes)\n", mem_config.ram_size);
}
