/**
 * Debug SPI Output
 * Outputs execution trace via SPI for debugging
 */

#ifndef DEBUG_SPI_H
#define DEBUG_SPI_H

#include <stdint.h>
#include <stdbool.h>

// Initialize debug SPI
void debug_spi_init(void);

// Log instruction execution
void debug_spi_log(void);

// Enable/disable debug output
void debug_spi_enable(bool enable);

#endif // DEBUG_SPI_H
