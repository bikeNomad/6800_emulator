/**
 * Intel HEX Format Parser Implementation
 */

#include "ihex_parser.h"
#include "interrupts.h"
#include "memory.h"
#include <ctype.h>
#include <stdio.h>
#include <string.h>

// Convert hex character to value
static uint8_t hex_to_value(char c)
{
	if (c >= '0' && c <= '9') {
		return c - '0';
	}
	if (c >= 'A' && c <= 'F') {
		return c - 'A' + 10;
	}
	if (c >= 'a' && c <= 'f') {
		return c - 'a' + 10;
	}
	return 0;
}

// Parse two hex characters to byte
static uint8_t parse_hex_byte(const char *str)
{
	return (hex_to_value(str[0]) << 4) | hex_to_value(str[1]);
}

// Parse a single Intel HEX record line
ihex_result_t ihex_parse_line(const char *line, ihex_record_t *record)
{
	// Check for start code ':'
	if (line[0] != ':') {
		return IHEX_ERROR_FORMAT;
	}

	// Parse byte count
	record->byte_count = parse_hex_byte(&line[1]);

	// Parse address (big-endian)
	record->address = (parse_hex_byte(&line[3]) << 8) | parse_hex_byte(&line[5]);

	// Parse record type
	record->record_type = parse_hex_byte(&line[7]);

	// Parse data bytes
	const char *data_ptr = &line[9];
	for (int i = 0; i < record->byte_count; i++) {
		record->data[i] = parse_hex_byte(data_ptr);
		data_ptr += 2;
	}

	// Parse checksum
	record->checksum = parse_hex_byte(data_ptr);

	// Verify checksum
	uint8_t sum = record->byte_count;
	sum += (record->address >> 8) & 0xFF;
	sum += record->address & 0xFF;
	sum += record->record_type;
	for (int i = 0; i < record->byte_count; i++) {
		sum += record->data[i];
	}
	sum += record->checksum;

	if (sum != 0) {
		printf("Checksum error: expected 0, got %02X\n", sum);
		return IHEX_ERROR_CHECKSUM;
	}

	return IHEX_OK;
}

#define ENTRY_ADDRESS_MASK 0xFF00

// Load complete Intel HEX data into memory
bool ihex_load_data(const char *hex_data, uint32_t length)
{
	const char *ptr = hex_data;
	const char *end = hex_data + length;
	uint32_t base_address = 0; // Extended address for 32-bit addressing
	uint32_t bytes_loaded = 0;
	bool eof_reached = false;

	printf("Loading Intel HEX data...\n");

	while (ptr < end && !eof_reached) {
		// Skip whitespace
		while (ptr < end && isspace((unsigned char)*ptr)) {
			ptr++;
		}

		if (ptr >= end) {
			break;
		}

		// Find end of line
		const char *line_end = ptr;
		while (line_end < end && *line_end != '\r' && *line_end != '\n') {
			line_end++;
		}

		// Parse line
		ihex_record_t record;
		char line_buffer[1024];
		size_t line_len = line_end - ptr;
		if (line_len >= sizeof(line_buffer)) {
			printf("Line too long\n");
			return false;
		}

		memcpy(line_buffer, ptr, line_len);
		line_buffer[line_len] = '\0';

		ihex_result_t result = ihex_parse_line(line_buffer, &record);

		if (result == IHEX_OK) {
			uint32_t full_address;
			switch (record.record_type) {
			case IHEX_TYPE_DATA:
				full_address = base_address + record.address;
				if (!memory_load_hex_data(full_address, record.data,
							  record.byte_count)) {
					printf("Failed to load data at address $%04lX\n",
					       (unsigned long)full_address);
					return false;
				}

				printf("Record address %08lx length %d\n", full_address,
				       record.byte_count);

				uint start_addr = full_address;
				uint end_addr = (full_address + record.byte_count - 1);

				// Mark all pages that contain this data as mapped
				uint start_page = start_addr & ENTRY_ADDRESS_MASK;
				uint end_page = end_addr & ENTRY_ADDRESS_MASK;

				for (uint page_addr = start_page; page_addr <= end_page;
				     page_addr += ENTRY_PAGE_SIZE) {
					memory_type_t type = memory_get_type((uint16_t)page_addr);
					if (type == MEM_TYPE_UNMAPPED) {
						printf("Mapping ROM page from hex $%04X\n",
						       (uint16_t)page_addr);
						map_as_rom((uint16_t)page_addr);
					}
				}

				bytes_loaded += record.byte_count;
				break;

			case IHEX_TYPE_EOF:
				printf("End of HEX file reached\n");
				eof_reached = true;
				break;

			case IHEX_TYPE_EXLINEAR:
				// Extended linear address (upper 16 bits)
				if (record.byte_count == 2) {
					base_address = ((uint32_t)record.data[0] << 24) |
						       ((uint32_t)record.data[1] << 16);
					printf("Extended address: $%08lX\n",
					       (unsigned long)base_address);
				}
				break;

			case IHEX_TYPE_EXSEG:
				// Extended segment address
				if (record.byte_count == 2) {
					base_address =
						(((uint32_t)record.data[0] << 8) | record.data[1])
						<< 4;
					printf("Extended segment: $%08lX\n",
					       (unsigned long)base_address);
				}
				break;

			default:
				printf("Unsupported record type: %02X\n", record.record_type);
				break;
			}
		} else {
			printf("Parse error: %d\n", result);
			return false;
		}

		ptr = line_end;
	}

	printf("Loaded %lu bytes from HEX data\n", (unsigned long)bytes_loaded);

	// Finalize by writing to flash (ROM or CMOS)
	if (bytes_loaded > 0) {
		printf("Finalizing hex load...\n");
		bool success = memory_finalize_load();
		if (success) {
			// Save the updated mapping to flash
			memory_save_memory_map_to_flash();

			// Automatically reset CPU to load reset vector from new ROM
			printf("Resetting CPU to load reset vector...\n");
			interrupt_service_reset();
		}
		return success;
	}

	return true;
}
