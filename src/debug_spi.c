/**
 * Debug SPI Output Implementation
 */

#include "debug_spi.h"
#include "cpu_state.h"
#include "board_config.h"
#include "hardware/spi.h"
#include "hardware/gpio.h"
#include <stdio.h>

// SPI instance
#define DEBUG_SPI_INST spi0
#define SPI_BAUDRATE (40 * 1000 * 1000)  // 40 MHz

static bool debug_enabled = false;  // Disabled by default (enable via USB "debug on")

// Initialize debug SPI
void debug_spi_init(void) {
    // Initialize SPI
    spi_init(DEBUG_SPI_INST, SPI_BAUDRATE);
    spi_set_format(DEBUG_SPI_INST, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // Configure GPIO for SPI
    gpio_set_function(GPIO_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI_CS, GPIO_FUNC_SPI);

    printf("Debug SPI initialized on GPIO %d (SCK), %d (MOSI)\n",
           GPIO_SPI_SCK, GPIO_SPI_MOSI);
}

// Log instruction execution
void debug_spi_log(void) {
    if (!debug_enabled) {
        return;
    }

    // Prepare 4-byte debug packet:
    // Word 0: PC (high byte, low byte)
    // Word 1: CPU CCR 
    uint8_t packet[4];

    packet[0] = (cpu.pc >> 8) & 0xFF;
    packet[1] = cpu.pc & 0xFF;
    packet[2] = 0;  // Reserved
    packet[3] = cpu.ccr;

    // Send packet via SPI
    spi_write16_blocking(DEBUG_SPI_INST, (uint16_t *)packet, 2);
}

// Enable/disable debug output
void debug_spi_enable(bool enable) {
    debug_enabled = enable;
}

// Get debug output status
bool debug_spi_is_enabled(void) {
    return debug_enabled;
}
