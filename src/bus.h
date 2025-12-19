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

static inline void drive_address_bus(uint16_t address) {
    uint32_t gpio_value = addr_to_gpio_mask(address);
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);  // 0x7F00 (GPIO 8-14)
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

static inline void drive_address_bus(uint16_t address) {
    uint32_t gpio_value = addr_to_gpio_mask(address);
    gpio_put_masked(ADDR_GPIO_MASK, gpio_value);  // 0xFFFF00 (GPIO 8-23)
}

#else
#error "Unknown board type - must define BOARD_PICO2 or BOARD_NED_SYS7"
#endif

// ----------------------------------------------------------------------------
// Control Signal Helpers (board-independent)
// ----------------------------------------------------------------------------

static inline void drive_control_read(void) {
    // VMA=1, R/W=1 (read)
    gpio_put_masked((1u << GPIO_VMA) | (1u << GPIO_RW), (1u << GPIO_VMA) | (1u << GPIO_RW));
}

static inline void drive_control_write(uint8_t data) {
    // Data + VMA=1, R/W=0 (write)
    uint32_t data_ctrl = data | (1u << GPIO_VMA) | (0u << GPIO_RW);
    gpio_put_masked(0xFF | (1u << GPIO_VMA) | (1u << GPIO_RW), data_ctrl);
}

static inline void deassert_vma(void) {
    // VMA=0, R/W=1 (inactive)
    gpio_put_masked((1u << GPIO_VMA) | (1u << GPIO_RW), (0u << GPIO_VMA) | (1u << GPIO_RW));
}

// Initialize bus interface (configure GPIO)
void bus_init(void);

// Perform one read bus cycle (address -> data)
// Synchronized to E clock
uint8_t bus_read_cycle(uint16_t address);

// Perform one write bus cycle (address + data -> bus)
// Synchronized to E clock
void bus_write_cycle(uint16_t address, uint8_t data);

// Wait for next E clock edge (for synchronization)
void bus_sync(void);

// Read control inputs
bool bus_read_irq(void);
bool bus_read_nmi(void);
bool bus_read_reset(void);

// ============================================================================
// PIO-Based Bus Cycle Functions (precise hardware timing)
// ============================================================================

extern bool pio_bus_initialized;

// Initialize PIO state machine for bus cycles
// Must be called after bus_init() and eclock_init()
void bus_cycle_pio_init(void);

// Perform one read bus cycle using PIO (precise 150ns data setup delay)
// Software sets up address, PIO handles E clock synchronization and data capture
uint8_t bus_read_cycle_pio(uint16_t address);

// Perform one write bus cycle using PIO (precise timing)
// Software sets up address and data, PIO handles E clock synchronization
void bus_write_cycle_pio(uint16_t address, uint8_t data);

// Enable/disable PIO bus cycles (for debugging/comparison)
void bus_cycle_pio_enable(bool enable);

// Check if PIO bus cycles are enabled
static inline bool bus_cycle_pio_is_enabled(void) {
    return pio_bus_initialized;
}


#endif // BUS_H
