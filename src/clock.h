/**
 * MC6800 E Clock Generation
 * Uses PIO to generate precise 0.894886 MHz E clock
 */

#ifndef CLOCK_H
#define CLOCK_H

#include <stdint.h>
#include <stdbool.h>
#include "board_config.h"

// E clock output pin is defined in board_config.h as GPIO_ECLOCK

// Initialize E clock PIO
void eclock_init(void);

// Start E clock generation
void eclock_start(void);

// Stop E clock generation
void eclock_stop(void);

// Wait for E clock rising edge
void eclock_wait_high(void);

// Wait for E clock falling edge
void eclock_wait_low(void);

// Get cycle count (for timing validation)
uint32_t eclock_get_count(void);

#endif // CLOCK_H
