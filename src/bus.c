/**
 * MC6800 Bus Interface Implementation
 */

#include "bus.h"
#include "clock.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "bus_cycle.pio.h"
#include <stdio.h>

// GPIO masks
#define DATA_MASK    0x000000FF  // GPIO 0-7

// Address bus GPIO mapping is board-specific (see board_config.h for ADDR_GPIO_MASK)

// Initialize bus interface
void bus_init(void) {
    // Configure data bus (GPIO 0-7) as inputs initially
    // Set drive strength once here for efficiency (applies when pins become outputs)
    for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_12MA);  // Set once during init
        gpio_pull_up(i);  // Weak pull-ups for floating data bus
    }

    // Configure address bus (board-specific GPIO assignments)
#if BOARD_TYPE == BOARD_PICO2
    // PICO2: GPIO 8-14 for A0,A1,A10-A14 (7 pins, non-contiguous)
    for (int i = 8; i <= 14; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_put(i, 0);
    }
#else
    // NED_SYS7: GPIO 8-23 for full address bus (A0-A15)
    for (int i = 8; i <= 23; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_set_drive_strength(i, GPIO_DRIVE_STRENGTH_12MA);
        gpio_put(i, 0);
    }
#endif

    // Configure control signals
    gpio_init(GPIO_VMA);
    gpio_set_dir(GPIO_VMA, GPIO_OUT);
    gpio_set_drive_strength(GPIO_VMA, GPIO_DRIVE_STRENGTH_12MA);
    gpio_put(GPIO_VMA, 0);  // VMA inactive

    gpio_init(GPIO_RW);
    gpio_set_dir(GPIO_RW, GPIO_OUT);
    gpio_set_drive_strength(GPIO_RW, GPIO_DRIVE_STRENGTH_12MA);
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
#if BOARD_TYPE == BOARD_PICO2
    // PICO2 unused pins: 15, 20, 24, 25, 26
    const int unused_pins[] = {15, 20, 24, 25, 26};
    for (int i = 0; i < 5; i++) {
        gpio_init(unused_pins[i]);
        gpio_set_dir(unused_pins[i], GPIO_IN);
        gpio_pull_up(unused_pins[i]);
    }
#else
    // NED_SYS7: Initialize unused GPIO pins as inputs with pull-ups
    // Unused: 30-32 (after control signals), 35-36 (old UART pins), 42-47 (after LEDs/UART)
    // Allow for either 36 or 39 to be used for yellow LED.
    const int unused_pins[] = {30, 31, 32, 35, 36, 39, 42, 43, 44, 45, 46, 47};
    for (int i = 0; i < 11; i++) {
        gpio_init(unused_pins[i]);
        gpio_set_dir(unused_pins[i], GPIO_IN);
        gpio_pull_up(unused_pins[i]);
    }

    // NED_SYS7: Initialize LED indicators (active low, so HIGH = off)
    gpio_init(GPIO_LED_ROM);
    gpio_set_dir(GPIO_LED_ROM, GPIO_OUT);
    gpio_set_drive_strength(GPIO_LED_ROM, GPIO_DRIVE_STRENGTH_12MA);  // Increase brightness
    gpio_put(GPIO_LED_ROM, 1);  // Off

    gpio_init(GPIO_LED_RAM);
    gpio_set_dir(GPIO_LED_RAM, GPIO_OUT);
    gpio_set_drive_strength(GPIO_LED_RAM, GPIO_DRIVE_STRENGTH_12MA);  // Increase brightness
    gpio_put(GPIO_LED_RAM, 1);  // Off

    gpio_init(GPIO_LED_UNMAPPED);
    gpio_set_dir(GPIO_LED_UNMAPPED, GPIO_OUT);
    gpio_set_drive_strength(GPIO_LED_UNMAPPED, GPIO_DRIVE_STRENGTH_12MA);  // Increase brightness
    gpio_put(GPIO_LED_UNMAPPED, 1);  // Off
#endif

    printf("Bus interface initialized for %s\n", BOARD_NAME);
    printf("  Data:  GPIO %d-%d\n", GPIO_DATA_BASE, GPIO_DATA_BASE + 7);
#if BOARD_TYPE == BOARD_PICO2
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
#if TIMING_TEST_ENABLED
    // Set test pin high during external bus read cycle
    gpio_put(GPIO_TIMING_TEST, 1);
#endif

    // Wait for E clock low (beginning of cycle)
    bus_sync();

    // Set data bus to input mode (bulk operation)
    // Drive strength already configured in bus_init()
    gpio_set_dir_masked(DATA_MASK, 0);

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

#if TIMING_TEST_ENABLED
    // Set test pin low after external bus read cycle completes
    gpio_put(GPIO_TIMING_TEST, 0);
#endif

    return data;
}

// Perform one write bus cycle
void bus_write_cycle(uint16_t address, uint8_t data) {
#if TIMING_TEST_ENABLED
    // Set test pin high during external bus write cycle
    gpio_put(GPIO_TIMING_TEST, 1);
#endif

    // Wait for E clock low (beginning of cycle)
    bus_sync();

    // Set data bus to output mode (bulk operation)
    // Drive strength already configured in bus_init()
    gpio_set_dir_masked(DATA_MASK, DATA_MASK);

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

    // Set data bus back to input mode (bulk operation)
    gpio_set_dir_masked(DATA_MASK, 0);

#if TIMING_TEST_ENABLED
    // Set test pin low after external bus write cycle completes
    gpio_put(GPIO_TIMING_TEST, 0);
#endif
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

// ============================================================================
// PIO-Based Bus Cycle Implementation
// ============================================================================

// PIO state machine for bus cycles (SM1 on pio0, SM0 is E clock)
#define BUS_PIO         pio0
#define BUS_SM          1

// PIO program offsets
static uint pio_read_offset = 0;
static uint pio_write_offset = 0;
static bool pio_bus_initialized = false;
static bool pio_bus_enabled = false;

// Initialize PIO bus cycle state machine
void bus_cycle_pio_init(void) {
    if (pio_bus_initialized) {
        printf("PIO bus cycles already initialized\n");
        return;
    }

    // Load both PIO programs
    pio_read_offset = pio_add_program(BUS_PIO, &bus_read_cycle_program);
    pio_write_offset = pio_add_program(BUS_PIO, &bus_write_cycle_program);

    // Initialize with read cycle program (default state)
    bus_read_cycle_program_init(BUS_PIO, BUS_SM, pio_read_offset);

    pio_bus_initialized = true;
    pio_bus_enabled = true;

    printf("PIO bus cycles initialized (SM%d on pio0)\n", BUS_SM);
    printf("  Read program offset:  %d\n", pio_read_offset);
    printf("  Write program offset: %d\n", pio_write_offset);
    printf("  E clock: GPIO %d (WAIT source)\n", GPIO_ECLOCK);
    printf("  VMA:     GPIO %d (SIDE-SET bit 0)\n", GPIO_VMA);
    printf("  R/W:     GPIO %d (SIDE-SET bit 1)\n", GPIO_RW);
    printf("  Data setup delay: 40 cycles @ 266MHz = 150.4ns\n");
}

// Perform one read bus cycle using PIO
uint8_t bus_read_cycle_pio(uint16_t address) {
    // Software: Set up address bus and data direction
    gpio_set_dir_masked(DATA_MASK, 0);  // Data bus = input
    drive_address_bus(address);

    // Clear any stale data from RX FIFO
    while (!pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM)) {
        pio_sm_get(BUS_PIO, BUS_SM);
    }

    // Enable state machine (it will run through one cycle and push data)
    pio_sm_set_enabled(BUS_PIO, BUS_SM, true);

    // Wait for data in RX FIFO (blocking)
    while (pio_sm_is_rx_fifo_empty(BUS_PIO, BUS_SM)) {
        tight_loop_contents();
    }

    // Read data from FIFO
    uint32_t data = pio_sm_get(BUS_PIO, BUS_SM);

    return (uint8_t)(data & 0xFF);
}

// Perform one write bus cycle using PIO
void bus_write_cycle_pio(uint16_t address, uint8_t data) {
    // Software: Set up address bus, data bus, and direction
    gpio_set_dir_masked(DATA_MASK, DATA_MASK);  // Data bus = output
    drive_address_bus(address);
    gpio_put_masked(DATA_MASK, data);

    // Switch to write cycle program
    pio_sm_set_enabled(BUS_PIO, BUS_SM, false);
    pio_sm_clear_fifos(BUS_PIO, BUS_SM);
    pio_sm_restart(BUS_PIO, BUS_SM);
    pio_sm_exec(BUS_PIO, BUS_SM, pio_encode_jmp(pio_write_offset));
    pio_sm_set_enabled(BUS_PIO, BUS_SM, true);

    // Wait for write cycle to complete (approximately 1 E clock cycle)
    // The write cycle program will complete after one full E clock period
    busy_wait_us(2);  // Conservative wait (2µs > 1.117µs E period)

    // Switch back to read cycle program for next operation
    pio_sm_set_enabled(BUS_PIO, BUS_SM, false);
    pio_sm_clear_fifos(BUS_PIO, BUS_SM);
    pio_sm_restart(BUS_PIO, BUS_SM);
    pio_sm_exec(BUS_PIO, BUS_SM, pio_encode_jmp(pio_read_offset));

    // Set data bus back to input mode
    gpio_set_dir_masked(DATA_MASK, 0);
}

// Enable/disable PIO bus cycles
void bus_cycle_pio_enable(bool enable) {
    if (!pio_bus_initialized && enable) {
        printf("Error: PIO bus cycles not initialized\n");
        return;
    }

    pio_bus_enabled = enable;
    pio_sm_set_enabled(BUS_PIO, BUS_SM, enable);

    if (enable) {
        // Re-initialize to read cycle program
        pio_sm_clear_fifos(BUS_PIO, BUS_SM);
        pio_sm_restart(BUS_PIO, BUS_SM);
        pio_sm_exec(BUS_PIO, BUS_SM, pio_encode_jmp(pio_read_offset));
    }

    printf("PIO bus cycles %s\n", enable ? "enabled" : "disabled");
}

// Check if PIO bus cycles are enabled
bool bus_cycle_pio_is_enabled(void) {
    return pio_bus_enabled;
}
