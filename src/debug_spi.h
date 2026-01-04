/**
 * Debug SPI Output
 * Outputs execution trace via SPI for debugging
 */

#ifndef DEBUG_SPI_H
#define DEBUG_SPI_H

#include <stdbool.h>
#include <stdint.h>

// Initialize debug SPI
void debug_spi_init(void);

// Log instruction execution
void debug_spi_log(void);

// Log external bus access (unmapped addresses)
void debug_spi_log_bus(uint16_t address, bool is_read, uint8_t data);

// Enable/disable debug output
void debug_spi_enable(bool enable);

// Get debug output status
bool debug_spi_is_enabled(void);

#endif // DEBUG_SPI_H
