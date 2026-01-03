/**
 * MC6800 Interrupt Handling
 * Handles /IRQ, /NMI, and /RESET inputs
 */

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

// Debug output control (set via CMakeLists.txt)
#ifndef DEBUG_INTERRUPTS
  #define DEBUG_INTERRUPTS 1
#endif

#if DEBUG_INTERRUPTS
  #define DEBUG_INT_PRINTF(...) printf(__VA_ARGS__)
#else
  #define DEBUG_INT_PRINTF(...) ((void)0)
#endif

// MC6800 interrupt vector addresses
#define VECTOR_RESET  0xFFFE  // Reset vector (highest priority)
#define VECTOR_NMI    0xFFFC  // Non-maskable interrupt
#define VECTOR_SWI    0xFFFA  // Software interrupt
#define VECTOR_IRQ    0xFFF8  // Interrupt request (maskable)

// Initialize interrupt handling
void interrupts_init(void);

// Check for pending interrupts
void interrupt_check(void);

// Service an interrupt (called by interrupt_check)
void interrupt_service_reset(void);
void interrupt_service_nmi(void);
void interrupt_service_irq(void);

#endif // INTERRUPTS_H
