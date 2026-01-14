/**
 * MC6800 Bus Interface Implementation
 */

#include "emulator.h"
#include "bus.h"
#include "bus_cycle.pio.h"
#include "bus_timing.h"
#include "clock.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include "pico/mutex.h"

// Guards ownership of PIO bus and eclock subsystem.
recursive_mutex_t pio_bus_mutex;

// Address bus GPIO mapping is board-specific (see board_config.h for
// ADDR_GPIO_MASK)
#if BOARD_TYPE == BOARD_NED_SYS7
#define WRITE_PINDIRS 0xFFFFFFFFUL
#define READ_PINDIRS  0xFFFFFF00UL
static inline uint32_t address_to_gpio(uint16_t addr)
{
	return addr << 8;
}
#else // BOARD_PICO2
#define WRITE_PINDIRS 0x00000000UL
#define READ_PINDIRS  0xFFFFFF00UL
// We're only using 7 address lines (A0,A1,A10-A14), so mask out the rest
static inline uint32_t address_to_gpio(uint16_t addr)
{
	return addr << 8; // TODO
}
#endif
#define DATA_MASK 0x000000FFUL

// Initialize bus interface
void bus_init(void)
{
	// Configure data bus (GPIO 0-7) as inputs initially
	// Set drive strength once here for efficiency (applies when pins become
	// outputs)
	for (int i = GPIO_DATA_BASE; i < GPIO_DATA_BASE + 8; i++) {
		gpio_init(i);
		gpio_set_dir(i, GPIO_IN);
		gpio_set_drive_strength(i,
					GPIO_DRIVE_STRENGTH_12MA); // Set once during init
		gpio_pull_up(i); // Weak pull-ups for floating data bus
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
	gpio_put(GPIO_VMA, 0); // VMA inactive

	gpio_init(GPIO_RW);
	gpio_set_dir(GPIO_RW, GPIO_OUT);
	gpio_set_drive_strength(GPIO_RW, GPIO_DRIVE_STRENGTH_12MA);
	gpio_put(GPIO_RW, 1); // Default to read

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
	// Unused: 31-32 (after control signals), 35-36 (old UART pins), 42-47
	// (after LEDs/UART) Allow for either 36 or 39 to be used for yellow LED.
	const int unused_pins[] = {30, 31, 32, 35, 36, 39, 42, 43, 44, 45, 46, 47};
	for (int i = 0; i < 11; i++) {
		gpio_init(unused_pins[i]);
		gpio_set_dir(unused_pins[i], GPIO_IN);
		gpio_pull_up(unused_pins[i]);
	}

	// NED_SYS7: Initialize LED indicators (active low, so HIGH = off)
	gpio_init(GPIO_LED_ROM);
	gpio_set_dir(GPIO_LED_ROM, GPIO_OUT);
	gpio_set_drive_strength(GPIO_LED_ROM,
				GPIO_DRIVE_STRENGTH_12MA); // Increase brightness
	gpio_put(GPIO_LED_ROM, 1);                         // Off

	gpio_init(GPIO_LED_RAM);
	gpio_set_dir(GPIO_LED_RAM, GPIO_OUT);
	gpio_set_drive_strength(GPIO_LED_RAM,
				GPIO_DRIVE_STRENGTH_12MA); // Increase brightness
	gpio_put(GPIO_LED_RAM, 1);                         // Off

	gpio_init(GPIO_LED_UNMAPPED);
	gpio_set_dir(GPIO_LED_UNMAPPED, GPIO_OUT);
	gpio_set_drive_strength(GPIO_LED_UNMAPPED,
				GPIO_DRIVE_STRENGTH_12MA); // Increase brightness
	gpio_put(GPIO_LED_UNMAPPED, 1);                    // Off
#endif

	printf("Bus interface initialized for %s\r\n", BOARD_NAME);
	printf("  Data:  GPIO %d-%d\r\n", GPIO_DATA_BASE, GPIO_DATA_BASE + 7);
#if BOARD_TYPE == BOARD_PICO2
	printf("  Addr:  GPIO 8-14 -> MC6800 A{0,1,10-14} (%d lines, %d "
	       "addresses)\r\n",
	       ADDR_LINES, ADDR_SPACE_SIZE);
	printf("  Addr mask: 0x%04X (non-contiguous address space)\r\n", ADDR_MASK);
#else
	printf("  Addr:  GPIO 8-23 -> MC6800 A0-A15 (%d lines, %d addresses)\r\n", ADDR_LINES,
	       ADDR_SPACE_SIZE);
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

// ============================================================================
// PIO-Based Bus Cycle Implementation (Two Dedicated State Machines)
// ============================================================================

// PIO program offsets
static int pio_cycle_offset = 0;

// Initialize PIO bus cycle state machines
void bus_cycle_pio_init(void)
{
	// Initialize mutex
	recursive_mutex_init(&pio_bus_mutex);

	// Load PIO program
	pio_clear_instruction_memory(BUS_PIO);
	pio_cycle_offset = pio_add_program(BUS_PIO, &bus_cycle_program);
	if (pio_cycle_offset < 0) {
		printf("Error: Failed to load PIO bus cycle program\r");
		return;
	}

	// Initialize E clock state machine (SM0) - always running
	// Note: E clock program should already be initialized by eclock_init()

	// Initialize cycle state machine (SM1) - always ready
	bus_cycle_program_init(BUS_PIO, CYCLE_SM, pio_cycle_offset);

	printf("PIO bus cycles initialized (SM0=Read/Write)\r\n");
	printf("  Cycle program offset: %d\r\n", pio_cycle_offset);
	printf("  E clock: GPIO %d (WAIT source)\r\n", GPIO_ECLOCK);
	printf("  VMA:     GPIO %d (SIDE-SET bit 0)\r\n", GPIO_VMA);
	printf("  R/W:     GPIO %d (SIDE-SET bit 1)\r\n", GPIO_RW);

	// Print dynamic timing configuration
	print_timing_config();
}

// Enable/disable PIO bus cycles
// Perform one read bus cycle using PIO
uint8_t __time_critical_func(bus_read_cycle_pio)(uint16_t address)
{
	pio_sm_put_blocking(BUS_PIO, CYCLE_SM, READ_PINDIRS);
	pio_sm_put_blocking(BUS_PIO, CYCLE_SM, address_to_gpio(address));

	// Wait for data in RX FIFO (blocking)
	uint32_t data = pio_sm_get_blocking(BUS_PIO, CYCLE_SM);
	return (uint8_t)(data & DATA_MASK);
}

// Perform one write bus cycle using PIO
void __time_critical_func(bus_write_cycle_pio)(uint16_t address, uint8_t data)
{
	pio_sm_put_blocking(BUS_PIO, CYCLE_SM, WRITE_PINDIRS);
	pio_sm_put_blocking(BUS_PIO, CYCLE_SM, address_to_gpio(address) | (uint32_t)data);
}

// Helper functions for bus operations with E clock management
void __time_critical_func(bus_read_block_with_eclock)(uint16_t address, uint16_t length,
						      uint8_t *buffer)
{
	// Temporarily start E clock if needed
	bool was_running = eclock_is_running();
	if (!was_running) {
		eclock_start();
	}

	// Read block of data
	for (uint16_t i = 0; i < length; i++) {
		buffer[i] = bus_read_cycle(address + i);
		busy_wait_us(3);
	}

	// Stop E clock if we started it
	if (!was_running) {
		eclock_stop();
	}
}

void __time_critical_func(bus_write_block_with_eclock)(uint16_t address, const uint8_t *buffer,
						       uint16_t length)
{
	// Temporarily start E clock if needed
	bool was_running = eclock_is_running();
	if (!was_running) {
		eclock_start();
	}

	// Write block of data
	for (uint16_t i = 0; i < length; i++) {
		bus_write_cycle(address + i, buffer[i]);
		busy_wait_us(3);
	}

	// Stop E clock if we started it
	if (!was_running) {
		eclock_stop();
	}
}