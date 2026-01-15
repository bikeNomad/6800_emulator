/**
 * Memory Fingerprinting and Auto-Configuration
 * (Functionality moved to memory_map.c)
 */

#include "memory_fingerprint.h"
#include "memory.h"
#include "memory_map.h"

// Test pattern for RAM/CMOS detection
static const uint8_t TEST_DATA[] = "This is a test of the bus";
#define TEST_DATA_SIZE (sizeof(TEST_DATA) - 1)

// PIA register offsets
#define PRA_OFFSET    0
#define DDRA_OFFSET   0
#define CRA_OFFSET    1
#define PRB_OFFSET    2
#define DDRB_OFFSET   2
#define CRB_OFFSET    3
#define SELECT_PR_BIT 0x04 // Bit in CRx to access PRx instead of DDRx

// Result of scanning a memory page
// Page classification types
typedef enum {
	PAGE_UNMAPPED,        // Default/unknown
	PAGE_EMPTY,           // All 0xFF or all 0x00
	PAGE_ROM,             // Read-only, consistent data
	PAGE_RAM,             // Read/write, test passes
	PAGE_PIA,             // 6820/6821 PIA detected
	PAGE_WMS_CMOS,        // Williams Sys 3-7 CMOS (high nybble = 0xF)
	PAGE_BALLY_CMOS,      // Bally CMOS (low nybble = 0xF)
	PAGE_BALLY_ZERO_PAGE, // Early Bally 0x00-0xFF
} page_type_t;

// Scan results storage - combined struct
typedef struct page_results_t {
	page_type_t scan;      // Raw scan result
	page_type_t coalesced; // Coalesced result for System 11
} page_results_t;

static page_results_t results[NUM_PAGES]; // One per 256-byte page
// Startup status (for displaying warnings via USB CDC after boot)
static sanity_result_t startup_status = SANITY_OK;

page_type_t fingerprint_page(uint16_t address); // Determine page type at address
void coalesce_regions(architecture_type_t arch,
		      uint decoded_bits); // Coalesce adjacent regions of same type
architecture_type_t
recognize_architecture(uint *p_decoded_bits); // Determine system architecture from scan results
void build_memory_map_from_scan(architecture_type_t arch, uint decoded_bits,
				printf_func_t printf_func);
int count_decoded_address_bits(void);
bool copy_rom_contents_from_bus(architecture_type_t arch,
				printf_func_t printf_func); // Copy ROM data from bus to memory

//--------------------------------------------------------------------+
// Helper Functions
//--------------------------------------------------------------------+

static const char *page_type_to_string(page_type_t type)
{
	switch (type) {
	case PAGE_EMPTY:
		return "EMPTY";
	case PAGE_ROM:
		return "ROM";
	case PAGE_RAM:
		return "RAM";
	case PAGE_PIA:
		return "PIA";
	case PAGE_UNMAPPED:
		return "UNMAPPED";
	case PAGE_BALLY_ZERO_PAGE:
		return "BALLY ZERO PAGE";
	case PAGE_WMS_CMOS:
		return "WMS CMOS (4-bit)";
	case PAGE_BALLY_CMOS:
		return "BALLY CMOS (4-bit)";
	default:
		return "UNKNOWN";
	}
}

const char *architecture_name(architecture_type_t arch)
{
	switch (arch) {
	case ARCH_EARLY_BALLY:
		return "Early Bally or Stern";
	case ARCH_WILLIAMS_SYS3_6:
		return "Williams System 3-6";
	case ARCH_WILLIAMS_SYS7:
		return "Williams System 7";
	case ARCH_WILLIAMS_SYS11:
		return "Williams System 11";
	case ARCH_UNKNOWN:
		return "Unknown";
	default:
		return "Unknown";
	}
}

void print_raw_scan_results(printf_func_t printf_func)
{
	uint16_t start = 0;
	uint8_t last_type = results[0].scan;

	for (uint page = 1; page <= NUM_PAGES; page++) {
		uint8_t current_type =
			(page < NUM_PAGES) ? results[page].scan : 255; // 255 = sentinel

		if (current_type != last_type || page == NUM_PAGES) {
			uint16_t end = (page << 8) - 1;
			const char *type_str = page_type_to_string(last_type);
			printf_func("  $%04X-$%04X: %s\r\n", start, end, type_str);

			start = page << 8;
			last_type = current_type;
		}
	}
}

void print_scan_results(printf_func_t printf_func)
{
	uint16_t start = 0;
	uint8_t last_type = results[0].coalesced;

	for (uint page = 1; page <= NUM_PAGES; page++) {
		uint8_t current_type =
			(page < NUM_PAGES) ? results[page].coalesced : 255; // 255 = sentinel

		if (current_type != last_type || page == NUM_PAGES) {
			uint16_t end = (page << 8) - 1;
			const char *type_str = page_type_to_string(last_type);
			printf_func("  $%04X-$%04X: %s\r\n", start, end, type_str);

			start = page << 8;
			last_type = current_type;
		}
	}
}

//--------------------------------------------------------------------+
// Detection Functions
//--------------------------------------------------------------------+

static bool detect_empty(uint16_t address)
{
	uint8_t block_data[ENTRY_PAGE_SIZE];

	// Read entire page
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		block_data[i] = bus_read_cycle(address + i);
	}

	// Check if all 0xFF
	bool all_ff = true;
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		if (block_data[i] != 0xFF) {
			all_ff = false;
			break;
		}
	}
	if (all_ff) {
		return false;
	}

	// Check if all 0x00
	bool all_00 = true;
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		if (block_data[i] != 0x00) {
			all_00 = false;
			break;
		}
	}
	if (all_00) {
		return false;
	}

	return true;
}

/* Check the range from address to address+TEST_DATA_SIZE to
 * see if it looks like RAM. Return true if so.
 * Requires E clock to be running.
 */
static bool detect_ram(uint16_t address)
{
	uint8_t original_data[TEST_DATA_SIZE];
	uint8_t readback_data[TEST_DATA_SIZE];

	// Read original data
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		original_data[i] = bus_read_cycle(address + i);
	}

	// Write test pattern
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		bus_write_cycle(address + i, TEST_DATA[i]);
	}

	// Read back
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		readback_data[i] = bus_read_cycle(address + i);
	}

	// Restore original data
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		bus_write_cycle(address + i, original_data[i]);
	}

	// Check if test pattern matched
	return memcmp(readback_data, TEST_DATA, TEST_DATA_SIZE) == 0;
}

static bool detect_bally_cmos(uint16_t address)
{
	uint8_t original_data[ENTRY_PAGE_SIZE];

	// Read original data and check for CMOS pattern (low nybble = 0xF)
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		original_data[i] = bus_read_cycle(address + i);
		if ((original_data[i] & 0x0F) != 0x0F) {
			return false;
		}
	}

	// Write test data (low nybbles only)
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		bus_write_cycle(address + i, TEST_DATA[i]);
	}

	// Read back and check high nybbles
	bool matches = true;
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		uint8_t readback = bus_read_cycle(address + i);
		if ((readback & 0xF0) != (TEST_DATA[i] & 0xF0)) {
			matches = false;
			break;
		}
	}

	// Restore original data
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		bus_write_cycle(address + i, original_data[i]);
	}

	return matches;
}

static bool detect_sys3_7_cmos(uint16_t address)
{
	uint8_t original_data[ENTRY_PAGE_SIZE];

	// Read original data and check for CMOS pattern (high nybble = 0xF)
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		original_data[i] = bus_read_cycle(address + i);
		if ((original_data[i] & 0xF0) != 0xF0) {
			return false;
		}
	}

	// Write test data (low nybbles only)
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		bus_write_cycle(address + i, TEST_DATA[i]);
	}

	// Read back and check low nybbles
	bool matches = true;
	for (uint16_t i = 0; i < TEST_DATA_SIZE; i++) {
		uint8_t readback = bus_read_cycle(address + i);
		if ((readback & 0x0F) != (TEST_DATA[i] & 0x0F)) {
			matches = false;
			break;
		}
	}

	// Restore original data
	for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
		bus_write_cycle(address + i, original_data[i]);
	}

	return matches;
}

static bool detect_pia(uint16_t address)
{
	// Save state
	uint8_t pra_ddra = bus_read_cycle(address + PRA_OFFSET);
	uint8_t cra = bus_read_cycle(address + CRA_OFFSET);
	uint8_t prb_ddrb = bus_read_cycle(address + PRB_OFFSET);
	uint8_t crb = bus_read_cycle(address + CRB_OFFSET);

	uint8_t pra, ddra, prb, ddrb;

	// Get actual PR and DDR values
	if (cra & SELECT_PR_BIT) {
		pra = pra_ddra;
		bus_write_cycle(address + CRA_OFFSET, cra & ~SELECT_PR_BIT);
		ddra = bus_read_cycle(address + DDRA_OFFSET);
	} else {
		ddra = pra_ddra;
		bus_write_cycle(address + CRA_OFFSET, cra | SELECT_PR_BIT);
		pra = bus_read_cycle(address + PRA_OFFSET);
	}

	if (crb & SELECT_PR_BIT) {
		prb = prb_ddrb;
		bus_write_cycle(address + CRB_OFFSET, crb & ~SELECT_PR_BIT);
		ddrb = bus_read_cycle(address + DDRB_OFFSET);
	} else {
		ddrb = prb_ddrb;
		bus_write_cycle(address + CRB_OFFSET, crb | SELECT_PR_BIT);
		prb = bus_read_cycle(address + PRB_OFFSET);
	}

	bool is_pia = true;

	// Test: Set both DDRs to all-inputs (0x00)
	bus_write_cycle(address + CRA_OFFSET, 0x00);
	bus_write_cycle(address + DDRA_OFFSET, 0x00);
	uint8_t ddra_readback = bus_read_cycle(address + DDRA_OFFSET);

	bus_write_cycle(address + CRB_OFFSET, 0x00);
	bus_write_cycle(address + DDRB_OFFSET, 0x00);
	uint8_t ddrb_readback = bus_read_cycle(address + DDRB_OFFSET);

	if (ddra_readback != 0x00 || ddrb_readback != 0x00) {
		is_pia = false;
	}

	if (is_pia) {
		// Access PRs
		bus_write_cycle(address + CRA_OFFSET, SELECT_PR_BIT);
		bus_write_cycle(address + CRB_OFFSET, SELECT_PR_BIT);

		// Verify SELECT_PR_BIT is set
		uint8_t cra_now = bus_read_cycle(address + CRA_OFFSET);
		uint8_t crb_now = bus_read_cycle(address + CRB_OFFSET);

		if ((cra_now & SELECT_PR_BIT) == 0 || (crb_now & SELECT_PR_BIT) == 0) {
			is_pia = false;
		}
	}

	if (is_pia) {
		// Read PRs (should be stable since DDRs are inputs)
		uint8_t pra_now = bus_read_cycle(address + PRA_OFFSET);
		uint8_t prb_now = bus_read_cycle(address + PRB_OFFSET);

		// Try to write (should have no effect with all inputs)
		bus_write_cycle(address + PRA_OFFSET, 0xFF);
		bus_write_cycle(address + PRB_OFFSET, 0xFF);

		uint8_t pra_written = bus_read_cycle(address + PRA_OFFSET);
		uint8_t prb_written = bus_read_cycle(address + PRB_OFFSET);

		if (pra_now != pra_written || prb_now != prb_written) {
			is_pia = false;
		}
	}

	// Restore state - Access DDRs
	bus_write_cycle(address + CRA_OFFSET, cra & ~SELECT_PR_BIT);
	bus_write_cycle(address + CRB_OFFSET, crb & ~SELECT_PR_BIT);
	bus_write_cycle(address + DDRA_OFFSET, ddra);
	bus_write_cycle(address + DDRB_OFFSET, ddrb);

	// Access PRs
	bus_write_cycle(address + CRA_OFFSET, cra | SELECT_PR_BIT);
	bus_write_cycle(address + CRB_OFFSET, crb | SELECT_PR_BIT);
	bus_write_cycle(address + PRA_OFFSET, pra);
	bus_write_cycle(address + PRB_OFFSET, prb);

	// Restore CRs
	bus_write_cycle(address + CRA_OFFSET, cra);
	bus_write_cycle(address + CRB_OFFSET, crb);

	return is_pia;
}

static bool detect_bally_page0(uint16_t address)
{
	// 128 bytes of RAM at 0
	// PIA at 0x90
	// PIA at 0x88
	// Stern: PIAs at 0xA0, 0xC0
	return ((address == 0) && detect_ram(address) && detect_pia(0x88) && detect_pia(0x90));
}

page_type_t fingerprint_page(uint16_t address)
{
	// Try detectors in order: PIA, RAM, CMOS, empty
	// PIA must be checked first to avoid false RAM detection
	if (detect_pia(address)) {
		return PAGE_PIA;
	}
	if (address == 0x0000 && detect_bally_page0(address)) {
		return PAGE_BALLY_ZERO_PAGE;
	}
	if (detect_ram(address)) {
		return PAGE_RAM;
	}
	if (detect_sys3_7_cmos(address)) {
		return PAGE_WMS_CMOS;
	}
	if (detect_bally_cmos(address)) {
		return PAGE_BALLY_CMOS;
	}
	if (detect_empty(address)) {
		return PAGE_EMPTY;
	}
	// Default to ROM for anything else
	return PAGE_ROM;
}

//--------------------------------------------------------------------+
// Architecture Recognition
//--------------------------------------------------------------------+

architecture_type_t recognize_architecture(uint *p_decoded_bits)
{
	int decoded_bits = count_decoded_address_bits();
	*p_decoded_bits = decoded_bits;

	// Early Bally/Stern
	if (decoded_bits < 16 && results[0x00].scan == PAGE_BALLY_ZERO_PAGE &&
	    results[0x02].scan == PAGE_BALLY_CMOS) {
		return ARCH_EARLY_BALLY;
	}

	// Count CMOS at specific addresses (System 3-7 signature)
	const uint8_t expected_cmos_pages[] = {0x01, 0x05, 0x09, 0x0D};
	int cmos_count = 0;
	for (uint i = 0; i < sizeof(expected_cmos_pages); i++) {
		uint8_t page = expected_cmos_pages[i];
		if (results[page].scan == PAGE_WMS_CMOS) {
			cmos_count++;
		}
	}

	// Count RAM pages at start (System 11 signature)
	int ram_count = 0;
	for (int i = 0; i < 0x08; i++) {
		if (results[i].scan == PAGE_RAM) {
			ram_count++;
		}
	}

	// Count PIA pages in expected places (System 3-7, 9/11)
	const uint8_t expected_pia_pages[] = {0x21, 0x22, 0x24, 0x28, 0x2c, 0x30, 0x34, 0x40};
	uint8_t pia_count = 0;
	for (uint i = 0; i < sizeof(expected_pia_pages); i++) {
		uint8_t page = expected_pia_pages[i];
		if (results[page].scan == PAGE_PIA) {
			pia_count++;
		}
	}

	printf("Recognize architecture: Bits:%d, RAM:%d, PIA:%d, CMOS:%d\r\n", decoded_bits,
	       ram_count, pia_count, cmos_count);

	// System 3-7: CMOS pattern, 15 bits decoded
	if (cmos_count == 4 && results[0x01].scan == PAGE_WMS_CMOS && decoded_bits == 15) {
		if (pia_count == 5) {
			return ARCH_WILLIAMS_SYS7;
		} else {
			// System 3:
			// System 4:
			// System 6:
			return ARCH_WILLIAMS_SYS3_6;
		}
	}

	// System 9/11: 2K RAM at start, no 4-bit CMOS
	if (cmos_count == 0 && ram_count == 8) {
		// System 9: A15 not decoded, only 4 PIAs
		// ROM at $C000-$FFFF or $E000-$FFFF
		// Check for aliasing and PIA count (ROMs could be missing)
		if (pia_count == 4 && decoded_bits == 15) {
			return ARCH_WILLIAMS_SYS9;
		}
		// System 11: 2K Contiguous RAM at start, no CMOS pattern, A15 decoded
		if (pia_count == 6 && decoded_bits == 16) {
			return ARCH_WILLIAMS_SYS11;
		}
	}

	return ARCH_UNKNOWN;
}

// Called from "scan_memory" USB command
bool memory_scan_and_build_map(printf_func_t printf_func)
{
	bool retval = true;

	printf_func("Starting memory scan...\r\n");

	// Start E clock for bus operations
	bool eclock_was_running = eclock_is_running();
	if (!eclock_was_running) {
		eclock_start();
	}

	// Scan all NUM_PAGES pages
	printf_func("Scanning 256 pages...\r\n");
	for (uint16_t page = 0; page < NUM_PAGES; page++) {
		uint16_t addr = TABLE_INDEX_TO_ADDR(page);
		results[page].scan = fingerprint_page(addr);
		sleep_ms(10);
	}
	printf_func("Scan complete\r\n");

	// Recognize architecture
	uint decoded_bits;
	architecture_type_t arch = recognize_architecture(&decoded_bits);
	printf_func("Architecture: %s (%u bits decoded)\r\n", architecture_name(arch),
		    decoded_bits);

	// Coalesce regions for better presentation
	coalesce_regions(arch, decoded_bits);
	printf_func("regions coalesced. Raw:\r\n");
	print_raw_scan_results(printf_func);

	printf_func("Coalesced");
	print_scan_results(printf_func);

	build_memory_map_from_scan(arch, decoded_bits, printf_func);
	printf_func("memory map built\r\n");
	memory_print_summary(printf_func);

	// Copy ROM contents from bus to flash (use coalesced results)
	if (!copy_rom_contents_from_bus(arch, printf_func)) {
		printf_func("Warning: Failed to copy ROM contents\r\n");
		retval = false;
	} else {
		printf_func("ROM contents copied from bus\r\n");
	}

	// Save memory map and config to flash
	memory_save_memory_map_to_flash();
	printf_func("Memory map saved to flash\r\n");

	memory_read_ram_from_bus();
	printf_func("RAM read from bus\r\n");

	// Stop E clock if we started it
	if (!eclock_was_running) {
		eclock_stop();
	}

	return retval;
}

sanity_result_t memory_sanity_check(void)
{
	// Check if we have a valid saved map
	bool has_valid_map = false;
	for (uint i = 0; i < NUM_PAGES; i++) {
		if (memory_map[i] != ENTRY_UNMAPPED_BUS) {
			has_valid_map = true;
			break;
		}
	}

	if (!has_valid_map) {
		return SANITY_NO_SAVED_MAP;
	}

	// Initialize E clock for bus operations
	eclock_init();

	// Initialize PIO bus cycles for bus operations
	bus_cycle_pio_init();

	// Start E clock for bus operations
	bool eclock_was_running = eclock_is_running();
	if (!eclock_was_running) {
		eclock_start();
	}

	sanity_result_t result = SANITY_OK;

	// Check RAM pages (sample every 1KB)
	for (uint16_t addr = mem_config.ram_base;
	     addr < mem_config.ram_base + mem_config.ram_size && addr < 0x8000; addr += 0x400) {
		page_type_t actual = fingerprint_page(addr);
		if (actual != PAGE_RAM && actual != PAGE_WMS_CMOS) {
			printf("RAM mismatch at $%04X: expected RAM, got %s\n", addr,
			       page_type_to_string(actual));
			result = SANITY_RAM_MISMATCH;
			break;
		}
	}

	// Check ROM pages (sample every 4KB) - EMPTY or ROM is OK, RAM is error
	if (result == SANITY_OK && mem_config.rom_size > 0) {
		for (uint16_t addr = mem_config.rom_base;
		     addr < mem_config.rom_base + mem_config.rom_size && addr < 0x8000;
		     addr += 0x1000) {
			page_type_t actual = fingerprint_page(addr);
			if (actual == PAGE_RAM) {
				printf("ROM region mismatch at $%04X: found RAM instead\n", addr);
				result = SANITY_ROM_UNEXPECTED;
				break;
			}
		}
	}

	// Stop E clock if we started it
	if (!eclock_was_running) {
		eclock_stop();
	}

	return result;
}

//--------------------------------------------------------------------+
// Coalescing Logic
//--------------------------------------------------------------------+

// Return the address of the first page marked as ROM
bool first_rom_address(uint16_t *rom_start)
{
	for (uint page = 0; page < NUM_PAGES; page++) {
		if (results[page].scan == PAGE_ROM) {
			*rom_start = TABLE_INDEX_TO_ADDR(page);
			return true;
		}
	}
	return false; // No ROM found
}

// Return the last address of the last page marked as ROM
bool last_rom_address(uint16_t *rom_end)
{
	for (int page = 255; page >= 0; page--) {
		if (results[page].scan == PAGE_ROM) {
			*rom_end = (uint16_t)((uint32_t)TABLE_INDEX_TO_ADDR(page) +
					      ENTRY_PAGE_SIZE - 1);
			return true;
		}
	}
	return false; // No ROM found
}

// Return the start address of the first page marked as RAM or CMOS
bool first_ram_address(uint16_t *ram_start)
{
	for (uint page = 0; page < NUM_PAGES; page++) {
		if (results[page].scan == PAGE_RAM || results[page].scan == PAGE_WMS_CMOS) {
			*ram_start = TABLE_INDEX_TO_ADDR(page);
			return true;
		}
	}
	return false; // No RAM found
}

// Return the last address of the last page marked as RAM or CMOS
bool last_ram_address(uint16_t *ram_end)
{
	for (int page = 255; page >= 0; page--) {
		if (results[page].scan == PAGE_RAM || results[page].scan == PAGE_WMS_CMOS) {
			*ram_end = (uint16_t)((uint32_t)TABLE_INDEX_TO_ADDR(page) +
					      ENTRY_PAGE_SIZE - 1);
			return true;
		}
	}
	return false; // No ROM found
}

// Coalesce regions for cleaner memory map presentation
void coalesce_regions(architecture_type_t arch, uint decoded_bits)
{
	// Copy scan results to coalesced results initially
	for (uint i = 0; i < NUM_PAGES; i++) {
		results[i].coalesced = results[i].scan;
	}

	uint16_t rom_start, rom_end;
	if (!first_rom_address(&rom_start) || !last_rom_address(&rom_end)) {
		rom_start = 0xFFFF;
		rom_end = 0xFFFF;
	}

	// Mark all the pages between rom_start and rom_end as ROM
	for (uint32_t addr = rom_start; addr <= rom_end; addr += ENTRY_PAGE_SIZE) {
		uint16_t page = ADDR_TO_TABLE_INDEX(addr);
		results[page].coalesced = PAGE_ROM;
	}
}
//--------------------------------------------------------------------+
// Scan Results Access
//--------------------------------------------------------------------+

// Helper function needed by build_memory_map_from_scan
int count_decoded_address_bits(void)
{
	// Check for repeated sequences that indicate non-decoded address lines
	int decoded_bits = 15;
	int stride = 128;
	int pages = 2;

	while (decoded_bits > 8) {
		for (int i = 0; i < stride; i++) {
			for (int j = 0; j < pages; j++) {
				int page_start = j * stride;
				if (results[i + page_start].scan !=
				    results[i + page_start + stride].scan) {
					return decoded_bits + 1;
				}
			}
		}
		decoded_bits--;
		stride >>= 1;
		pages <<= 1;
	}
	return decoded_bits;
}

// Williams System 3-7 does not decode A15, and duplicates the zero page of RAM at $1000
void apply_system7_rules(void)
{
	// Add $0000-$00FF → $1000-$10FF RAM mirror
	uint32_t page0_entry = memory_map[0x00];
	if ((page0_entry & ENTRY_FLAG_MASK) == ENTRY_MAPPED_RAM) {
		memory_map[0x10] = page0_entry;
	}

	// A15 not decoded: mirror low pages to high
	for (int i = 0; i < 128; i++) {
		memory_map[i + 128] = memory_map[i];
	}
}

void build_memory_map_from_scan(architecture_type_t arch, uint decoded_bits,
				printf_func_t printf_func)
{
	// Initialize all to unmapped
	for (uint i = 0; i < NUM_PAGES; i++) {
		memory_map[i] = ENTRY_UNMAPPED_BUS;
	}

	// Determine regions from scan
	uint32_t ram_start = 0xFFFF, ram_end = 0;
	uint32_t rom_start = 0xFFFF, rom_end = 0;

	if (decoded_bits == 16) {
		rom_end = 0xFFFF;
	}

	// First pass: determine regions
	// Only process first aliased section of scan
	mem_config.decoded_bits = decoded_bits;
	mem_config.architecture = arch;
	uint max_pages = NUM_PAGES;
	max_pages >>= (16 - decoded_bits);

	for (uint page = 0; page < max_pages; page++) {
		uint32_t addr = TABLE_INDEX_TO_ADDR(page);
		uint32_t end_addr = addr + 255;

		switch (results[page].scan) {
		case PAGE_RAM:
		case PAGE_WMS_CMOS:
			if (addr <= MAX_RAM_SIZE) {
				if (addr < ram_start) {
					ram_start = addr;
				}
				if (end_addr > ram_end) {
					ram_end = end_addr;
				}
			}
			break;
		case PAGE_ROM:
			if (addr >= MIN_ROM_ADDRESS) {
				if (addr < rom_start) {
					rom_start = addr;
				}
				if (end_addr > rom_end) {
					rom_end = end_addr;
				}
			}
			break;
		default:
			break;
		}
	}

	// Update mem_config
	if (ram_start != 0xFFFF) {
		mem_config.ram_base = ram_start;
		mem_config.ram_size = (ram_end - ram_start) + 1;
	}
	if (rom_start != 0xFFFF) {
		mem_config.rom_base = rom_start;
		mem_config.rom_size = (rom_end - rom_start) + 1;
	}

	// Second pass: build map entries
	for (uint page = 0; page < max_pages; page++) {
		uint32_t addr = TABLE_INDEX_TO_ADDR(page);

		switch (results[page].scan) {
		case PAGE_RAM:
			setup_ram_mapping(addr);
			break;
		case PAGE_ROM:
			if (addr >= MIN_ROM_ADDRESS) {
				setup_rom_mapping(addr);
			}
			break;
		case PAGE_EMPTY:
			// map EMPTY pages in ROM range as ROM
			if (addr >= rom_start && addr <= rom_end) {
				setup_rom_mapping(addr);
			}
			break;
		default:
			// PIA, UNMAPPED stay as ENTRY_UNMAPPED_BUS
			break;
		}
	}

	// Apply architecture-specific rules
	if (arch == ARCH_WILLIAMS_SYS7) {
		apply_system7_rules();
	} else if (arch == ARCH_WILLIAMS_SYS11) {
		// System 11 uses full A15 decode, no aliasing needed
		// ROM is continuous from $4000-$FFFF
	} else {
		// Unknown architecture - analyze actual address aliasing from scan results
		int decoded_bits = count_decoded_address_bits();
		printf_func("Address decoding: %d bits decoded\r\n", decoded_bits);

		if (decoded_bits < 16) {
			// Apply mirroring based on detected aliasing
			int mirror_size =
				1 << (16 - decoded_bits); // Size of repeating block in pages
			int num_blocks = NUM_PAGES / mirror_size;

			for (int block = 1; block < num_blocks; block++) {
				for (int i = 0; i < mirror_size; i++) {
					memory_map[block * mirror_size + i] = memory_map[i];
				}
			}
		}
		// If decoded_bits == 16, all address lines are decoded, no mirroring needed
	}
}

//--------------------------------------------------------------------+
// ROM Copying
//--------------------------------------------------------------------+

bool copy_rom_contents_from_bus(architecture_type_t arch, printf_func_t printf_func)
{
	// Clear ROM load buffer first
	memory_clear_rom_load_buffer();

	uint16_t pages_copied = 0;

	// Determine how many pages to scan based on architecture
	// For System 11, scan full 64KB address space since A15 is fully decoded
	// For other systems, only scan low address space to avoid aliases

	uint max_pages = (arch == ARCH_WILLIAMS_SYS11) ? NUM_PAGES : 128;

	// For System 11, copy all pages in ROM range since ROM is continuous
	// For other systems, only copy pages detected as ROM
	if (arch == ARCH_WILLIAMS_SYS11) {
		// Copy all pages in the configured ROM range
		for (uint32_t addr = mem_config.rom_base;
		     addr < (uint32_t)mem_config.rom_base + mem_config.rom_size;
		     addr += ENTRY_PAGE_SIZE) {

			// Check if this page is actually ROM (skip PIA, etc.)
			uint16_t page = ADDR_TO_TABLE_INDEX(addr);
			if (page < NUM_PAGES && results[page].coalesced != PAGE_ROM &&
			    results[page].coalesced != PAGE_EMPTY) {
				// Skip non-ROM pages (PIA, etc.) - leave as 0xFF in flash
				continue;
			}

			// Read 256-byte page from bus
			uint8_t page_buffer[ENTRY_PAGE_SIZE];
			for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
				page_buffer[i] = bus_read_cycle((uint16_t)addr + i);
				busy_wait_us(3);
			}

			// Load into ROM buffer using existing function
			if (memory_load_hex_data((uint16_t)addr, page_buffer, ENTRY_PAGE_SIZE)) {
				pages_copied++;
			} else {
				printf_func("Warning: Failed to load ROM page at $%04X\r\n",
					    (uint16_t)addr);
			}
		}
	} else {
		// For other architectures, only copy pages detected as ROM
		for (uint page = 0; page < max_pages; page++) {
			if (results[page].coalesced != PAGE_ROM) {
				continue;
			}

			uint16_t addr = TABLE_INDEX_TO_ADDR(page);

			// Only copy if address is in configured ROM range
			if (addr < mem_config.rom_base ||
			    addr >= mem_config.rom_base + mem_config.rom_size) {
				continue;
			}

			// Read 256-byte page from bus
			uint8_t page_buffer[ENTRY_PAGE_SIZE];
			for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
				page_buffer[i] = bus_read_cycle(addr + i);
				busy_wait_us(3);
			}

			// Load into ROM buffer using existing function
			if (memory_load_hex_data(addr, page_buffer, ENTRY_PAGE_SIZE)) {
				pages_copied++;
			} else {
				printf_func("Warning: Failed to load ROM page at $%04X\r\n", addr);
			}
		}
	}

	printf_func("Copied %u ROM pages from bus\r\n", pages_copied);

	// If we copied any ROM data, finalize the load
	if (pages_copied > 0) {
		if (!memory_finalize_load()) {
			printf_func("Error: Failed to finalize ROM load\r\n");
			return false;
		}
		printf_func("ROM contents saved to flash\r\n");
	}

	return true;
}

/**
 * Get startup status for displaying warnings via USB CDC
 */
sanity_result_t memory_get_startup_status(void)
{
	return startup_status;
}
