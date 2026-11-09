#pragma once
#include <stdbool.h>
#define VECTOR_RESET 0xFFFE
#define VECTOR_NMI 0xFFFC
#define VECTOR_SWI 0xFFFA
#define VECTOR_IRQ 0xFFF8
typedef enum interrupt_t { INT_NONE, INT_RESET, INT_NMI, INT_IRQ } interrupt_t;
extern volatile bool fake_reset_pin;
void interrupts_init(void);
interrupt_t interrupt_check(void);
void interrupt_service_reset(void);
