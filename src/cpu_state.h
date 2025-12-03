/**
 * MC6800 CPU State
 * Defines the processor registers and state
 */

#ifndef CPU_STATE_H
#define CPU_STATE_H

#include <stdint.h>
#include <stdbool.h>

// Condition Code Register (CCR) flag bits
#define CCR_C  0x01  // Carry
#define CCR_V  0x02  // Overflow
#define CCR_Z  0x04  // Zero
#define CCR_N  0x08  // Negative
#define CCR_I  0x10  // Interrupt mask
#define CCR_H  0x20  // Half-carry
#define CCR_FIXED 0xC0  // Bits 7-6 always set to 1

// MC6800 CPU state
typedef struct {
    uint16_t pc;      // Program counter
    uint8_t a;        // Accumulator A
    uint8_t b;        // Accumulator B
    uint16_t x;       // Index register
    uint16_t sp;      // Stack pointer
    uint8_t ccr;      // Condition code register
    bool halted;      // CPU halt state (WAI instruction or waiting for EPROM)
    bool running;     // CPU running state (set by USB RUN command)
    bool irq_pending; // IRQ request pending
    bool nmi_pending; // NMI request pending
    uint64_t instruction_count; // Total instructions executed since reset
} cpu_state_t;

// Global CPU state (defined in cpu_state.c)
extern cpu_state_t cpu;

// Initialize CPU state
void cpu_init(void);

// Check if CPU is running
bool cpu_is_running(void);

// Start CPU execution
void cpu_start(void);

// Halt CPU execution
void cpu_halt(void);

// Get CCR flag
bool cpu_get_flag(uint8_t flag);

// Set CCR flag
void cpu_set_flag(uint8_t flag, bool value);

// Update flags based on result
void cpu_update_nz(uint8_t result);
void cpu_update_nzv(uint8_t result, uint8_t operand1, uint8_t operand2, bool subtraction);

// Stack operations
void cpu_push(uint8_t value);
uint8_t cpu_pull(void);
void cpu_push16(uint16_t value);
uint16_t cpu_pull16(void);

#endif // CPU_STATE_H
