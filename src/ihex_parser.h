/**
 * Intel HEX Format Parser
 * Parses Intel HEX records for EPROM loading
 */

#ifndef IHEX_PARSER_H
#define IHEX_PARSER_H

#include <stdbool.h>
#include <stdint.h>

// Intel HEX record types
#define IHEX_TYPE_DATA        0x00 // Data record
#define IHEX_TYPE_EOF         0x01 // End of file
#define IHEX_TYPE_EXSEG       0x02 // Extended segment address
#define IHEX_TYPE_STARTSEG    0x03 // Start segment address
#define IHEX_TYPE_EXLINEAR    0x04 // Extended linear address
#define IHEX_TYPE_STARTLINEAR 0x05 // Start linear address

// Parse result
typedef enum {
	IHEX_OK = 0,            // Parsed successfully
	IHEX_ERROR_FORMAT,      // Invalid format
	IHEX_ERROR_CHECKSUM,    // Checksum mismatch
	IHEX_ERROR_RECORD_TYPE, // Unsupported record type
	IHEX_ERROR_EOF          // End of file reached
} ihex_result_t;

// Parsed record structure
typedef struct {
	uint8_t byte_count;  // Number of data bytes
	uint16_t address;    // Address for this record
	uint8_t record_type; // Record type
	uint8_t data[256];   // Data bytes (max 255)
	uint8_t checksum;    // Checksum byte
} ihex_record_t;

// Parse a single Intel HEX record line
ihex_result_t ihex_parse_line(const char *line, ihex_record_t *record);

// Load complete Intel HEX data into memory
bool ihex_load_data(const char *hex_data, uint32_t length);

#endif // IHEX_PARSER_H
