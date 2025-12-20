/**
 * MC6800 Bus Interface Implementation
 */

#include "bus.h"
#include "clock.h"
#include "bus_timing.h"
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "bus_cycle.pio.h"
#include <stdio.h>

// Address bus GPIO mapping is board-specific (see board_config.h for ADDR_GPIO_MASK)
#define WRITE_PINDIRS 0xFFFFFFFFUL
#define READ_PINDIRS 0xFFFFFF00UL
#define DATA_MASK 0x000000FFUL

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
    // Unused: 31-32 (after control signals), 35-36 (old UART pins), 42-47 (after LEDs/UART)
    // Allow for either 36 or 39 to be used for yellow LED.
    const int unused_pins[] = {31, 32, 35, 36, 39, 42, 43, 44, 45, 46, 47};
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

    printf("Bus interface initialized for %s\r\n", BOARD_NAME);
    printf("  Data:  GPIO %d-%d\r\n", GPIO_DATA_BASE, GPIO_DATA_BASE + 7);
#if BOARD_TYPE == BOARD_PICO2
    printf("  Addr:  GPIO 8-14 -> MC6800 A{0,1,10-14} (%d lines, %d addresses)\r\n",
           ADDR_LINES, ADDR_SPACE_SIZE);
    printf("  Addr mask: 0x%04X (non-contiguous address space)\r\n", ADDR_MASK);
#else
    printf("  Addr:  GPIO 8-23 -> MC6800 A0-A15 (%d lines, %d addresses)\r\n",
           ADDR_LINES, ADDR_SPACE_SIZE);
    printf("  Addr mask: 0x%04X (full address space)\r\n", ADDR_MASK);
#endif
    printf("  VMA:   GPIO %d\r\n", GPIO_VMA);
    printf("  R/W:   GPIO %d\r\n", GPIO_RW);
    printf("  /IRQ:  GPIO %d\r\n", GPIO_IRQ);
    printf("  /NMI:  GPIO %d\r\n", GPIO_NMI);
    printf("  /RESET: GPIO %d\r\n", GPIO_RESET);
#if TIMING_TEST_ENABLED
    printf("  Timing test enabled, pin %d\r\n", GPIO_TIMING_TEST);
#endif
}

// Perform one read bus cycle
uint8_t bus_read_cycle(uint16_t address) {
    uint8_t data = 0;

    // Wait for E clock low (beginning of cycle)
    bus_sync();

    // Use PIO-based bus cycle if available for precise timing
    if (bus_cycle_pio_is_enabled()) {
        data = bus_read_cycle_pio(address);
    } else {
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
        data = all_gpios & DATA_MASK;

        // Wait for E clock low (end of cycle)
        eclock_wait_low();

        // De-assert VMA (R/W stays high)
        deassert_vma();
    }

    return data;
}

// Perform one write bus cycle
void bus_write_cycle(uint16_t address, uint8_t data) {
    // Wait for E clock low (beginning of cycle)
    bus_sync();

    // Use PIO-based bus cycle if available for precise timing
    if (bus_cycle_pio_is_enabled()) {
        bus_write_cycle_pio(address, data);
    } else {
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

    }
}

// Wait for next E clock edge (with real-time tracking)
void __time_critical_func(bus_sync)(void) {
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
// PIO-Based Bus Cycle Implementation (Two Dedicated State Machines)
// ============================================================================

// PIO state machines for bus cycles (SM0=eclock, SM1=read/write, SM2=sync)
#define BUS_PIO         pio0
#define E_SM            0   // E clock generation
#define CYCLE_SM        1   // read/write cycle
#define SYNC_SM         2   // TODO: bus sync

 // PIO program offsets
static uint pio_cycle_offset = 0;
static uint pio_sync_offset = 0;

bool pio_bus_initialized = false;
bool pio_bus_enabled = false;

// Initialize PIO bus cycle state machines
void bus_cycle_pio_init(void) {
    if (pio_bus_initialized) {
        return;
    }

    // Load PIO program
    pio_cycle_offset = pio_add_program(BUS_PIO, &bus_cycle_program);

    // Initialize E clock state machine (SM0) - always running
    // Note: E clock program should already be initialized by eclock_init()

    // Initialize cycle state machine (SM1) - always ready
    bus_cycle_program_init(BUS_PIO, CYCLE_SM, pio_cycle_offset);

    pio_bus_initialized = true;

    printf("PIO bus cycles initialized (SM0=E, SM1=Read/Write)\r\n");
    printf("  Cycle program offset: %d\r\n", pio_cycle_offset);
    printf("  E clock: GPIO %d (WAIT source)\r\n", GPIO_ECLOCK);
    printf("  VMA:     GPIO %d (SIDE-SET bit 0)\r\n", GPIO_VMA);
    printf("  R/W:     GPIO %d (SIDE-SET bit 1)\r\n", GPIO_RW);

    // Print dynamic timing configuration
    print_timing_config();
}

// Enable/disable PIO bus cycles
void bus_cycle_pio_enable(bool enable) {
    bus_cycle_pio_init();
    if (enable && !pio_bus_enabled) {
        // Enable PIO bus cycles
        pio_sm_restart(BUS_PIO, CYCLE_SM);  // Reset SM1 state machine
        pio_sm_set_enabled(BUS_PIO, CYCLE_SM, true);
    } else if (!enable && pio_bus_enabled) {
        // Disable PIO bus cycles
        pio_sm_set_enabled(BUS_PIO, CYCLE_SM, false);
    }
    pio_bus_enabled = enable;
}

// Perform one read bus cycle using PIO
uint8_t __time_critical_func(bus_read_cycle_pio)(uint16_t address) {
    pio_sm_put_blocking(BUS_PIO, CYCLE_SM, READ_PINDIRS);
    pio_sm_put_blocking(BUS_PIO, CYCLE_SM, ((uint32_t)address) << 8UL);

    // Wait for data in RX FIFO (blocking)
    uint32_t data = pio_sm_get_blocking(BUS_PIO, CYCLE_SM);
    return (uint8_t)(data & 0xFFUL);
}

// Perform one write bus cycle using PIO
void __time_critical_func(bus_write_cycle_pio)(uint16_t address, uint8_t data) {
    pio_sm_put_blocking(BUS_PIO, CYCLE_SM, WRITE_PINDIRS);
    pio_sm_put_blocking(BUS_PIO, CYCLE_SM, (((uint32_t)address) << 8UL) | (uint32_t)data);
    busy_wait_us(2);
}