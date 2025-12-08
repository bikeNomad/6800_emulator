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

// Address bus GPIO mapping is board-specific (see board_config.h for ADDR_GPIO_MASK)

// Initialize bus interface
void bus_init(void) {
    // Configure data bus (GPIO 0-7) as inputs initially
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);  // Weak pull-ups for floating data bus
    }

    // Configure address bus (board-specific GPIO assignments)
#if defined(BOARD_PICO2)
    // PICO2: GPIO 8-14 for A0,A1,A10-A14 (7 pins, non-contiguous)
    for (int i = 8; i <= 14; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }
#else
    // NED_SYS7: GPIO 8-23 for full address bus (A0-A15)
    for (int i = 8; i <= 23; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }
#endif

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

    // Initialize unused GPIO pins as inputs with pull-ups to prevent floating
#if defined(BOARD_PICO2)
    // PICO2 unused pins: 15, 20, 23, 24, 25
    const int unused_pins[] = {15, 20, 23, 24, 25};
    for (int i = 0; i < 5; i++) {
        gpio_init(unused_pins[i]);
        gpio_set_dir(unused_pins[i], GPIO_IN);
        gpio_pull_up(unused_pins[i]);
    }
#else
    // NED_SYS7: Initialize LED indicators (active low, so HIGH = off)
    gpio_init(GPIO_LED_ROM);
    gpio_set_dir(GPIO_LED_ROM, GPIO_OUT);
    gpio_put(GPIO_LED_ROM, 1);  // Off

    gpio_init(GPIO_LED_RAM);
    gpio_set_dir(GPIO_LED_RAM, GPIO_OUT);
    gpio_put(GPIO_LED_RAM, 1);  // Off

    gpio_init(GPIO_LED_UNMAPPED);
    gpio_set_dir(GPIO_LED_UNMAPPED, GPIO_OUT);
    gpio_put(GPIO_LED_UNMAPPED, 1);  // Off

    // Initialize remaining unused high GPIO pins (37-48)
    for (int i = 37; i <= 48; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
    }
#endif

    printf("Bus interface initialized for %s\n", BOARD_NAME);
    printf("  Data:  GPIO %d-%d\n", GPIO_DATA_BASE, GPIO_DATA_BASE + 7);
#if defined(BOARD_PICO2)
    printf("  Addr:  GPIO 8-14 -> MC6800 A{0,1,10-14} (%d lines, %d addresses)\n",
           ADDR_LINES, ADDR_SPACE_SIZE);
    printf("  Addr mask: 0x%04X (non-contiguous address space)\n", ADDR_MASK);
#else
    printf("  Addr:  GPIO 8-23 -> MC6800 A0-A15 (%d lines, %d addresses)\n",
           ADDR_LINES, ADDR_SPACE_SIZE);
    printf("  Addr mask: 0x%04X (full address space)\n", ADDR_MASK);
#endif
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

    // Drive address bus using board-specific mapping
    drive_address_bus(address);

    // Assert VMA and R/W (read = 1)
    drive_control_read();

    // Wait for E clock high (data valid time)
    eclock_wait_high();

    // Read data bus using bulk read
    uint32_t all_gpios = gpio_get_all();
    uint8_t data = all_gpios & DATA_MASK;

    // Wait for E clock low (end of cycle)
    eclock_wait_low();

    // De-assert VMA (R/W stays high)
    deassert_vma();

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

    // Drive address bus using board-specific mapping
    drive_address_bus(address);

    // Drive data bus, VMA, and R/W
    drive_control_write(data);

    // Wait for E clock high (data latches)
    eclock_wait_high();

    // Wait for E clock low (end of cycle)
    eclock_wait_low();

    // De-assert VMA and return R/W to read
    deassert_vma();

    // Set data bus back to input mode
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_set_dir(i, GPIO_IN);
    }
}

// Wait for next E clock edge (with real-time tracking)
void bus_sync(void) {
    // Get real elapsed cycles from PIO
    uint32_t pio_cycles = eclock_get_pio_cycles();

    // Calculate difference (positive = ahead, negative = behind)
    int32_t diff = (int32_t)(eclock_get_count() - pio_cycles);

    if (diff > 0) {
        // Emulator is ahead of real time
        // Apply overage credit first
        diff -= cycle_overage;

        if (diff > 0) {
            // Still need to wait for additional cycles
            for (int32_t i = 0; i < diff; i++) {
                eclock_wait_low();
            }
            cycle_overage = 0;
        } else {
            // Overage covers the difference
            cycle_overage = -diff;
        }
    } else if (diff < 0) {
        // Emulator is behind real time - accumulate overage credit
        cycle_overage += (-diff);

        // Cap overage at reasonable limit to prevent overflow
        if (cycle_overage > 1000) {
            cycle_overage = 1000;
        }

        // Wait for at least the next E clock edge to stay synchronized
        eclock_wait_low();
    } else {
        // Exactly on time - wait for next edge
        eclock_wait_low();
    }
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
