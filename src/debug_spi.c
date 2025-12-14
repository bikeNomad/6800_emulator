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
#define SPI_BAUDRATE (30 * 1000 * 1000)  // 30 MHz

static bool debug_enabled = false;  // Disabled by default (enable via USB "debug on")
static uint8_t last_data_bus = 0;
static bool last_rw = true;

// Initialize debug SPI
void debug_spi_init(void) {
    // Initialize SPI
    spi_init(DEBUG_SPI_INST, SPI_BAUDRATE);

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
    // Byte 0: PC high byte
    // Byte 1: PC low byte
    // Byte 2: R/W flag (bit 7) + reserved
    // Byte 3: Data bus value
    uint8_t packet[4];

    packet[0] = (cpu.pc >> 8) & 0xFF;
    packet[1] = cpu.pc & 0xFF;
    packet[2] = (last_rw ? 0x80 : 0x00);
    packet[3] = last_data_bus;

    // Send packet via SPI
    spi_write_blocking(DEBUG_SPI_INST, packet, 4);
}

// Enable/disable debug output
void debug_spi_enable(bool enable) {
    debug_enabled = enable;
}
