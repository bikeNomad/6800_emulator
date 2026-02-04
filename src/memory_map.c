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

// Memory map array
uint32_t memory_map[NUM_PAGES];

memory_type_t memory_get_type(uint16_t address)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint32_t table_entry = memory_map[table_index];
	uint8_t flags = table_entry & ENTRY_FLAG_MASK;
	switch (flags) {
	case ENTRY_MAPPED_ROM:
		return MEM_TYPE_ROM;
	case ENTRY_MAPPED_RAM:
	case ENTRY_MAPPED_CMOS:
		return MEM_TYPE_RAM;
	default:
		return MEM_TYPE_UNMAPPED;
	}
}

uint16_t memory_get_unaliased_address(uint16_t address)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint32_t table_entry = memory_map[table_index];
	uint8_t offset = ADDR_TO_TABLE_OFFSET(address);
	uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
	uint8_t flags = table_entry & ENTRY_FLAG_MASK;
	if (flags == ENTRY_MAPPED_RAM || flags == ENTRY_MAPPED_CMOS) { // RAM?
		return mem_config.ram_base + (base_address + offset - (uint32_t)&ram_shadow[0]);
	} else if (flags == ENTRY_MAPPED_ROM) { // ROM?
		return mem_config.rom_base + (base_address + offset - (uint32_t)&rom_shadow[0]);
	} else {
		return address;
	}
}

uint8_t *memory_get_shadow_address(uint16_t address)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint32_t table_entry = memory_map[table_index];
	if (table_entry & ENTRY_UNMAPPED) {
		return NULL;
	}
	uint8_t offset = ADDR_TO_TABLE_OFFSET(address);
	uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
	return (uint8_t *)(uintptr_t)(base_address + offset);
}

// Initialize memory map per vanilla memory_config
// (no aliasing)
void memory_initialize_map(void)
{
	// Default configuration to SYS11
	mem_config.rom_base = MIN_ROM_ADDRESS; // 0x4000
	mem_config.rom_size = MAX_ROM_SIZE;    // 48KB (4000-FFFF)
	mem_config.ram_base = 0x0000;
	mem_config.ram_size = 0x0800; // 2KB (0000-07FF)
	mem_config.decoded_bits = 16; // full 64KB address space

	memory_update_map();
}

// Update memory map per mem_config's RAM and ROM ranges
void memory_update_map(void)
{
	// Set all entries to unmapped initially
	for (uint16_t i = 0; i < NUM_PAGES; i++) {
		memory_map[i] = ENTRY_UNMAPPED_BUS;
	}

	// Set RAM entries (RAM is always mapped)
	for (uint32_t address = mem_config.ram_base;
	     address < mem_config.ram_base + mem_config.ram_size; address += ENTRY_PAGE_SIZE) {
		map_as_ram(address);
	}

	// Set ROM entries (ROM is always mapped)
	for (uint32_t address = mem_config.rom_base;
	     address < mem_config.rom_base + mem_config.rom_size; address += ENTRY_PAGE_SIZE) {
		map_as_rom(address);
	}
}

uint8_t __time_critical_func(memory_read_fast)(uint16_t address)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint8_t offset = ADDR_TO_TABLE_OFFSET(address);
	uint32_t table_entry = memory_map[table_index];
	if (table_entry & ENTRY_UNMAPPED) { // unmapped
		return bus_read_cycle(address);
	}
	eclock_accumulate(1); // Track cycle, don't wait
	uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
	uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
	return *shadow_address;
}

void __time_critical_func(memory_write_fast)(uint16_t address, uint8_t data)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint8_t offset = ADDR_TO_TABLE_OFFSET(address);
	uint32_t table_entry = memory_map[table_index];
	if (table_entry & ENTRY_UNMAPPED) { // unmapped
		bus_write_cycle(address, data);
		return;
	}
	if (!(table_entry & ENTRY_WRITABLE)) { // Ignore writes to ROM
		eclock_accumulate(1);          // Track cycle, don't wait
		return;
	}
	eclock_accumulate(1); // Track cycle, don't wait
	uint32_t base_address = table_entry & ENTRY_ADDR_MASK;
	uint8_t *shadow_address = (uint8_t *)(uintptr_t)(base_address + offset);
	*shadow_address = data;
}

// Print a summary of the various memory ranges defined in the memory_map
void memory_print_summary(printf_func_t printf_func)
{
	printf_func("Memory Map Summary:\r\n");
	printf_func("  ROM: $%04X-$%04X (%d bytes)\r\n", mem_config.rom_base,
		    mem_config.rom_base + mem_config.rom_size - 1, mem_config.rom_size);
	printf_func("  RAM: $%04X-$%04X (%d bytes)\r\n", mem_config.ram_base,
		    mem_config.ram_base + mem_config.ram_size - 1, mem_config.ram_size);
	printf_func("  Flash offset: 0x%08lX, size: %u bytes\r\n",
		    (unsigned long)FLASH_TARGET_OFFSET, (unsigned int)mem_config.rom_size);

	// Count mapped vs unmapped pages
	uint16_t mapped_pages = 0;
	uint16_t rom_pages = 0;
	uint16_t ram_pages = 0;
	uint16_t unmapped_pages = 0;

	uint total_pages = NUM_PAGES >> (16 - mem_config.decoded_bits);

	for (uint16_t i = 0; i < total_pages; i++) {
		memory_type_t type = memory_get_type(i * ENTRY_PAGE_SIZE);
		if (type == MEM_TYPE_UNMAPPED) {
			unmapped_pages++;
		} else {
			mapped_pages++;
			if (type == MEM_TYPE_RAM) {
				ram_pages++;
			} else if (type == MEM_TYPE_ROM) {
				rom_pages++;
			}
		}
	}

	printf_func("  Memory map: %u total pages (%u mapped pages (%u ROM, %u RAM), %u unmapped "
		    "pages)\r\n",
		    total_pages, mapped_pages, rom_pages, ram_pages, unmapped_pages);
}

// Save memory config and map to flash
void memory_save_memory_map_to_flash(void)
{
	// flash_range_program requires sizes to be multiples of FLASH_PAGE_SIZE (256 bytes)
	// FLASH_PAGE_SIZE is defined in hardware/flash.h

	// Round up config size to page boundary
	uint32_t config_write_size =
		(FLASH_MEMORY_CONFIG_SIZE + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);
	uint32_t map_write_size =
		(FLASH_MEMORY_MAP_SIZE + FLASH_PAGE_SIZE - 1) & ~(FLASH_PAGE_SIZE - 1);

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
	       FLASH_MEMORY_CONFIG_SIZE, FLASH_MEMORY_MAP_SIZE, (unsigned long)config_write_size,
	       (unsigned long)map_write_size);
}

// Load memory config and map from flash
bool memory_load_memory_map_from_flash(void)
{
	bool config_valid = false;
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
		config_valid = true;
		printf("Debug: Loaded config - rom_base=$%04X rom_size=%u ram_base=$%04X "
		       "ram_size=%u\n",
		       loaded_config.rom_base, loaded_config.rom_size, loaded_config.ram_base,
		       loaded_config.ram_size);
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
			       (unsigned long)((uint32_t)loaded_config.rom_base +
					       loaded_config.rom_size));
			config_valid = false;
		}
		if ((uint32_t)loaded_config.ram_base + loaded_config.ram_size > 0x10000) {
			printf("Debug: ram address range check failed\n");
			config_valid = false;
		}

		if (config_valid) {
			memcpy(&mem_config, &loaded_config, FLASH_MEMORY_CONFIG_SIZE);
			memcpy(memory_map, map_flash_ptr, FLASH_MEMORY_MAP_SIZE);
			printf("Memory config and map loaded from flash (%u + %u bytes)\n",
			       FLASH_MEMORY_CONFIG_SIZE, FLASH_MEMORY_MAP_SIZE);
		}

		printf("Debug: config_valid=%d\n", config_valid);

		if (!config_valid) {
			printf("Invalid memory config data in flash, using default "
			       "initialization\n");
		}
	} else {
		printf("No memory config/map data found in flash, using default initialization\n");
	}

	return config_valid;
}

// Clear all mapping (set all pages to unmapped in memory map)
void memory_clear_map(void)
{
	// Set all ROM pages to unmapped
	for (uint table_index = 0; table_index < NUM_PAGES; table_index++) {
		memory_map[table_index] = ENTRY_UNMAPPED_BUS;
	}

	// Save updated memory map to flash
	memory_save_memory_map_to_flash();
	printf("All memory pages unmapped\n");
}

/**
 * Check if an address is currently mapped (not unmapped)
 * Returns true if the address is mapped to ROM or RAM
 */
bool memory_is_address_mapped(uint16_t address)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint32_t table_entry = memory_map[table_index];
	return !(table_entry & ENTRY_UNMAPPED);
}

// Return true if any of the pages in the given range are marked as
// unmapped
bool memory_range_has_unmapped(uint16_t address, uint16_t len)
{
	uint index = ADDR_TO_TABLE_INDEX(address);
	uint end_index = ADDR_TO_TABLE_INDEX((uint32_t)address + len - 1);
	while (index <= end_index) {
		uint32_t table_entry = memory_map[index];
		if (table_entry & ENTRY_UNMAPPED) {
			return true;
		}
		index++;
	}
	return false;
}

// Memory map building functions

void map_as_ram(uint16_t address)
{
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint32_t shadow_addr = (uint32_t)(uintptr_t)&ram_shadow[address - mem_config.ram_base];
	uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_RAM;
	memory_map[table_index] = table_entry;
}

void map_as_rom(uint16_t address)
{
	if (address < mem_config.rom_base ||
	    (uint32_t)address >= (uint32_t)mem_config.rom_base + mem_config.rom_size) {
		printf("map_as_rom: $%04X outside ROM range $%04X-$%04X\n", address,
		       mem_config.rom_base,
		       (uint16_t)(mem_config.rom_base + mem_config.rom_size - 1));
		return;
	}
	uint8_t table_index = ADDR_TO_TABLE_INDEX(address);
	uint32_t shadow_addr = (uint32_t)(uintptr_t)&rom_shadow[address - mem_config.rom_base];
	uint32_t table_entry = (shadow_addr & ENTRY_ADDR_MASK) | ENTRY_MAPPED_ROM;
	memory_map[table_index] = table_entry;
}
