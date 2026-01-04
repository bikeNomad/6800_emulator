/**
 * Debug SPI Output Implementation
 */

#include "debug_spi.h"
#include "board_config.h"
#include "cpu_state.h"
#include "hardware/gpio.h"
#include "hardware/spi.h"
#include <stdio.h>

// SPI instance
#define DEBUG_SPI_INST spi0
#define SPI_BAUDRATE (40 * 1000 * 1000) // 40 MHz

static bool debug_enabled = false; // Disabled by default (enable via USB "debug on")

// Initialize debug SPI
void debug_spi_init(void) {
    // Initialize SPI
    spi_init(DEBUG_SPI_INST, SPI_BAUDRATE);
    spi_set_format(DEBUG_SPI_INST, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);

    // Configure GPIO for SPI
    gpio_set_function(GPIO_SPI_SCK, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(GPIO_SPI_CS, GPIO_FUNC_SPI);

    printf("Debug SPI initialized on GPIO %d (SCK), %d (MOSI)\n", GPIO_SPI_SCK, GPIO_SPI_MOSI);
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

    packet[1] = (cpu.pc >> 8) & 0xFF;
    packet[0] = cpu.pc & 0xFF;
    packet[3] = 0; // Reserved
    packet[2] = cpu.ccr;

    // Send packet via SPI
    spi_write16_blocking(DEBUG_SPI_INST, (uint16_t *)packet, 2);
}

// Log external bus access (unmapped addresses)
void debug_spi_log_bus(uint16_t address, bool is_read, uint8_t data) {
    if (!debug_enabled) {
        return;
    }

    // Prepare 4-byte bus access packet:
    // Word 0: Address (16 bits)
    // Word 1: R/W bit (bit 8) + Data byte (bits 0-7)
    //   - Read:  bit 8 = 1 (0x01xx)
    //   - Write: bit 8 = 0 (0x00xx)
    uint8_t packet[4];

    packet[1] = (address >> 8) & 0xFF; // Address high byte
    packet[0] = address & 0xFF;        // Address low byte
    packet[3] = is_read ? 0x01 : 0x00; // R/W flag in high byte
    packet[2] = data;                  // Data byte in low byte

    // Send packet via SPI
    spi_write16_blocking(DEBUG_SPI_INST, (uint16_t *)packet, 2);
}

// Enable/disable debug output
void debug_spi_enable(bool enable) { debug_enabled = enable; }

// Get debug output status
bool debug_spi_is_enabled(void) { return debug_enabled; }
