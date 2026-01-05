/**
 * MC6800 Interrupt Handling
 * Handles /IRQ, /NMI, and /RESET inputs
 */

#ifndef INTERRUPTS_H
#define INTERRUPTS_H

#include "emulator.h"

#if DEBUG_INTERRUPTS
#define DEBUG_INT_PRINTF(...) printf(__VA_ARGS__)
#else
#define DEBUG_INT_PRINTF(...) ((void)0)
#endif

// MC6800 interrupt vector addresses
#define VECTOR_RESET 0xFFFE // Reset vector (highest priority)
#define VECTOR_NMI 0xFFFC   // Non-maskable interrupt
#define VECTOR_SWI 0xFFFA   // Software interrupt
#define VECTOR_IRQ 0xFFF8   // Interrupt request (maskable)

typedef enum interrupt_t {
    INT_NONE,  // no interrupts detected
    INT_RESET,
    INT_NMI,
    INT_IRQ,
} interrupt_t;

// Initialize interrupt handling
void interrupts_init(void);

// Check for pending interrupts
interrupt_t interrupt_check(void);

// Service an interrupt (called by interrupt_check)
void interrupt_service_reset(void);
void interrupt_service_nmi(void);
void interrupt_service_irq(void);

#endif  // INTERRUPTS_H
