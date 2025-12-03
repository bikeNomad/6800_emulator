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

// Initialize bus interface
void bus_init(void) {
    // Configure data bus (GPIO 0-7) as inputs initially
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);  // Weak pull-ups for floating data bus
    }

    // Configure address bus (board-specific number of lines)
    for (int i = GPIO_ADDR_BASE; i < GPIO_ADDR_BASE + ADDR_LINES; i++) {
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
    printf("  Addr:  GPIO %d-%d (%d bits, %dKB space)\n",
           GPIO_ADDR_BASE, GPIO_ADDR_BASE + ADDR_LINES - 1,
           ADDR_LINES, (1 << ADDR_LINES) / 1024);
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

    // Mask address to configured address lines
    address &= ADDR_MASK;

    // Drive address bus
    for (int i = 0; i < ADDR_LINES; i++) {
        gpio_put(GPIO_ADDR_BASE + i, (address >> i) & 1);
    }

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

    // Mask address to configured address lines
    address &= ADDR_MASK;

    // Drive address bus
    for (int i = 0; i < ADDR_LINES; i++) {
        gpio_put(GPIO_ADDR_BASE + i, (address >> i) & 1);
    }

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
