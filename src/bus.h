/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * MC6800 Bus Interface
 * Controls GPIO for address/data/control signals
 * Implements cycle-accurate bus operations synchronized to E clock
 */
#pragma once

#include "board_config.h"
#include "clock.h"
#include "hardware/gpio.h"
#include "pico/mutex.h"
#include <stdbool.h>
#include <stdint.h>

// GPIO pin assignments are defined in board_config.h

// Initialize bus interface (configure GPIO, create mutex)
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

// Helper functions for bus operations with E clock management
void bus_read_block_with_eclock(uint16_t address, uint16_t length, uint8_t *buffer);
void bus_write_block_with_eclock(uint16_t address, const uint8_t *buffer, uint16_t length);