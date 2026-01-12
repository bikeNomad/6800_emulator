/**
 * MC6800 Bus Interface
 * Controls GPIO for address/data/control signals
 * Implements cycle-accurate bus operations synchronized to E clock
 */
#pragma once

#include "board_config.h"
#include "clock.h"
#include "hardware/gpio.h"
#include <stdbool.h>
#include <stdint.h>

// GPIO pin assignments are defined in board_config.h

// Initialize bus interface (configure GPIO)
void bus_init(void);

// Read interrupt request lines (active low)
static inline bool bus_read_irq(void)
{
	return !gpio_get(GPIO_IRQ); // Active low, so invert
}

static inline bool bus_read_nmi(void)
{
	return !gpio_get(GPIO_NMI); // Active low, so invert
}

static inline bool bus_read_reset(void)
{
	return !gpio_get(GPIO_RESET); // Active low, so invert
}

// ============================================================================
// PIO-Based Bus Cycle Functions (precise hardware timing)
// ============================================================================

extern bool pio_bus_initialized;

// Initialize PIO state machine for bus cycles
// Must be called after bus_init() and eclock_init()
void bus_cycle_pio_init(void);

// Perform one read bus cycle using PIO
// PIO handles E clock synchronization and data capture
uint8_t bus_read_cycle_pio(uint16_t address);

// Perform one write bus cycle using PIO
// PIO handles E clock synchronization
void bus_write_cycle_pio(uint16_t address, uint8_t data);

// Perform one read bus cycle (address -> data)
// Synchronized to E clock
static inline uint8_t bus_read_cycle(uint16_t address)
{
	bus_sync();
	return bus_read_cycle_pio(address);
}

// Perform one write bus cycle (address + data -> bus)
// Synchronized to E clock
static inline void bus_write_cycle(uint16_t address, uint8_t data)
{
	bus_sync();
	bus_write_cycle_pio(address, data);
}