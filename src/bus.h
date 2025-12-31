/**
 * MC6800 Bus Interface
 * Controls GPIO for address/data/control signals
 * Implements cycle-accurate bus operations synchronized to E clock
 */

#ifndef BUS_H
#define BUS_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"
#include "hardware/gpio.h"
#include "clock.h"

// GPIO pin assignments are defined in board_config.h

// ============================================================================
// Board-Specific Address to GPIO Mapping Functions
// ============================================================================
// These functions handle the conversion of MC6800 address bus to GPIO pins.
// Different boards have different GPIO layouts due to pin constraints.
// ============================================================================

#if BOARD_TYPE == BOARD_PICO2
// ----------------------------------------------------------------------------
// Raspberry Pi Pico 2 W - Non-contiguous mapping (7 address lines)
// A0-A1 → GPIO 8-9, A10-A14 → GPIO 10-14
// This saves GPIO pins since only PIAs are on physical bus
// ----------------------------------------------------------------------------

static inline uint32_t addr_to_gpio_mask(uint16_t address) {
    // Apply address mask (only bits 0,1,10-14 used)
    address &= ADDR_MASK;  // 0x7C03

    // Map non-contiguous address bits to contiguous GPIO pins:
    // A0-A1 (bits 0-1) → GPIO 8-9 (shift left by 8)
    // A10-A14 (bits 10-14) → GPIO 10-14 (already aligned at bit 10)
    uint32_t gpio_value = ((address & 0x0003) << 8) |  // A0-A1 → GPIO 8-9
                          (address & 0x7C00);           // A10-A14 → GPIO 10-14
    return gpio_value;
}

#elif BOARD_TYPE == BOARD_NED_SYS7
// ----------------------------------------------------------------------------
// Ned's System 7 Board - Full 16-bit address bus (16 address lines)
// A0-A15 → GPIO 8-23 (contiguous mapping)
// ----------------------------------------------------------------------------

static inline uint32_t addr_to_gpio_mask(uint16_t address) {
    // Apply address mask (all 16 bits used)
    address &= ADDR_MASK;  // 0xFFFF

    // Map contiguous address bits to GPIO pins:
    // A0-A15 (bits 0-15) → GPIO 8-23 (shift left by 8)
    uint32_t gpio_value = (uint32_t)address << 8;
    return gpio_value;
}

#else
#error "Unknown board type - must define BOARD_PICO2 or BOARD_NED_SYS7"
#endif

// Initialize bus interface (configure GPIO)
void bus_init(void);

// Read interrupt request lines (active low)
static inline bool bus_read_irq(void) {
    return !gpio_get(GPIO_IRQ);  // Active low, so invert
}

static inline bool bus_read_nmi(void) {
    return !gpio_get(GPIO_NMI);  // Active low, so invert
}

static inline bool bus_read_reset(void) {
    return !gpio_get(GPIO_RESET);  // Active low, so invert
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
static inline uint8_t bus_read_cycle(uint16_t address) {
    bus_sync();
    return bus_read_cycle_pio(address);
}

// Perform one write bus cycle (address + data -> bus)
// Synchronized to E clock
static inline void bus_write_cycle(uint16_t address, uint8_t data) {
    bus_sync();
    bus_write_cycle_pio(address, data);
}


#endif // BUS_H
