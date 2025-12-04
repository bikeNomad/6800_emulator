/**
 * MC6800 Bus Interface Implementation
 */

#include "bus.h"
#include "clock.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include <stdio.h>

// GPIO masks
#define DATA_MASK    0x000000FF  // GPIO 0-7

// Address bus GPIO mapping constants
// A0-A1 (bits 0-1) → GPIO 8-9
// A10-A14 (bits 10-14) → GPIO 10-14
#define ADDR_GPIO_MASK  0x7F00  // GPIO 8-14 mask (0b0111_1111_0000_0000)

// Initialize bus interface
void bus_init(void) {
    // Configure data bus (GPIO 0-7) as inputs initially
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);  // Weak pull-ups for floating data bus
    }

    // Configure address bus (7 pins: GPIO 8-14 for A0,A1,A10-A14)
    for (int i = 8; i <= 14; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }

    // Configure control signals
    gpio_init(GPIO_VMA);
    gpio_set_dir(GPIO_VMA, GPIO_OUT);
    gpio_put(GPIO_VMA, 0);  // VMA inactive

    gpio_init(GPIO_RW);
    gpio_set_dir(GPIO_RW, GPIO_OUT);
    gpio_put(GPIO_RW, 1);  // Default to read

    // Configure interrupt inputs with pull-ups (active low)
    gpio_init(GPIO_IRQ);
    gpio_set_dir(GPIO_IRQ, GPIO_IN);
    gpio_pull_up(GPIO_IRQ);

    gpio_init(GPIO_NMI);
    gpio_set_dir(GPIO_NMI, GPIO_IN);
    gpio_pull_up(GPIO_NMI);

    gpio_init(GPIO_RESET);
    gpio_set_dir(GPIO_RESET, GPIO_IN);
    gpio_pull_up(GPIO_RESET);

    printf("Bus interface initialized for %s\n", BOARD_NAME);
    printf("  Data:  GPIO %d-%d\n", GPIO_DATA_BASE, GPIO_DATA_BASE + 7);
    printf("  Addr:  GPIO 8-14 -> MC6800 A{0,1,10-14} (%d lines, %d addresses)\n",
           ADDR_LINES, ADDR_SPACE_SIZE);
    printf("  Addr mask: 0x%04X (non-contiguous address space)\n", ADDR_MASK);
    printf("  VMA:   GPIO %d\n", GPIO_VMA);
    printf("  R/W:   GPIO %d\n", GPIO_RW);
    printf("  /IRQ:  GPIO %d\n", GPIO_IRQ);
    printf("  /NMI:  GPIO %d\n", GPIO_NMI);
    printf("  /RESET: GPIO %d\n", GPIO_RESET);
}

// Perform one read bus cycle
uint8_t bus_read_cycle(uint16_t address) {
    // Wait for E clock low (beginning of cycle)
    bus_sync();

    // Set data bus to input mode
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_set_dir(i, GPIO_IN);
    }

    // Mask address to configured bits
    address &= ADDR_MASK;

    // Drive address bus using mask and shift (much faster than bit-by-bit)
    // A0-A1 (bits 0-1) → GPIO 8-9: shift left by 8
    // A10-A14 (bits 10-14) → GPIO 10-14: already aligned!
    uint32_t gpio_value = ((address & 0x0003) << 8) | (address & 0x7C00);
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);

    // Assert VMA and R/W (read = 1)
    gpio_put(GPIO_VMA, 1);
    gpio_put(GPIO_RW, 1);

    // Wait for E clock high (data valid time)
    eclock_wait_high();

    // Read data bus using bulk read
    uint32_t all_gpios = gpio_get_all();
    uint8_t data = all_gpios & DATA_MASK;

    // Wait for E clock low (end of cycle)
    eclock_wait_low();

    // De-assert VMA
    gpio_put(GPIO_VMA, 0);

    return data;
}

// Perform one write bus cycle
void bus_write_cycle(uint16_t address, uint8_t data) {
    // Wait for E clock low (beginning of cycle)
    bus_sync();

    // Set data bus to output mode
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_set_dir(i, GPIO_OUT);
    }

    // Mask address to configured bits
    address &= ADDR_MASK;

    // Drive address bus using mask and shift (much faster than bit-by-bit)
    // A0-A1 (bits 0-1) → GPIO 8-9: shift left by 8
    // A10-A14 (bits 10-14) → GPIO 10-14: already aligned!
    uint32_t gpio_value = ((address & 0x0003) << 8) | (address & 0x7C00);
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);

    // Drive data bus
    for (int i = 0; i < 8; i++) {
        gpio_put(GPIO_DATA_BASE + i, (data >> i) & 1);
    }

    // Assert VMA and set R/W (write = 0)
    gpio_put(GPIO_VMA, 1);
    gpio_put(GPIO_RW, 0);

    // Wait for E clock high (data latches)
    eclock_wait_high();

    // Wait for E clock low (end of cycle)
    eclock_wait_low();

    // De-assert VMA and return R/W to read
    gpio_put(GPIO_VMA, 0);
    gpio_put(GPIO_RW, 1);

    // Set data bus back to input mode
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_set_dir(i, GPIO_IN);
    }
}

// Wait for next E clock edge
void bus_sync(void) {
    eclock_wait_low();
}

// Read interrupt request lines (active low)
bool bus_read_irq(void) {
    return !gpio_get(GPIO_IRQ);  // Active low, so invert
}

bool bus_read_nmi(void) {
    return !gpio_get(GPIO_NMI);  // Active low, so invert
}

bool bus_read_reset(void) {
    return !gpio_get(GPIO_RESET);  // Active low, so invert
}
