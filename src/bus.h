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

// GPIO pin assignments are defined in board_config.h

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

#endif // BUS_H
