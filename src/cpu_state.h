/**
 * MC6800 CPU State
 * Defines the processor registers and state
 */

#ifndef CPU_STATE_H
#define CPU_STATE_H

#include <stdint.h>
#include <stdbool.h>
#include "memory.h"  // For memory_read_fast/write_fast in inline stack functions

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
    bool halted;      // CPU halt state (waiting for EPROM or manual halt)
    bool running;     // CPU running state (set by USB RUN command)
    bool wai_state;   // CPU waiting for interrupt (WAI instruction)
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

// Get CCR flag (inline for performance)
static inline bool cpu_get_flag(uint8_t flag) {
    return (cpu.ccr & flag) != 0;
}

// Set CCR flag (inline for performance)
static inline void cpu_set_flag(uint8_t flag, bool value) {
    if (value) {
        cpu.ccr |= flag;
    } else {
        cpu.ccr &= ~flag;
    }
    // Ensure bits 7-6 always remain 1
    cpu.ccr |= CCR_FIXED;
}

// Update N and Z flags based on result (inline for performance)
static inline void cpu_update_nz(uint8_t result) {
    cpu_set_flag(CCR_Z, result == 0);
    cpu_set_flag(CCR_N, (result & 0x80) != 0);
}

// Update N, Z, V flags for arithmetic operations (inline for performance)
static inline void cpu_update_nzv(uint8_t result, uint8_t operand1, uint8_t operand2, bool subtraction) {
    cpu_update_nz(result);

    // Calculate overflow
    // For addition: V = (A^R) & (B^R) & 0x80
    // For subtraction: V = (A^B) & (A^R) & 0x80
    bool overflow;
    if (subtraction) {
        overflow = ((operand1 ^ operand2) & (operand1 ^ result) & 0x80) != 0;
    } else {
        overflow = ((operand1 ^ result) & (operand2 ^ result) & 0x80) != 0;
    }
    cpu_set_flag(CCR_V, overflow);
}

// Stack operations (inline for performance)
static inline void cpu_push(uint8_t value) {
    memory_write_fast(cpu.sp, value);  // Fast-path for stack RAM
    cpu.sp--;
}

static inline uint8_t cpu_pull(void) {
    cpu.sp++;
    return memory_read_fast(cpu.sp);  // Fast-path for stack RAM
}

static inline void cpu_push16(uint16_t value) {
    cpu_push(value & 0xFF);        // Low byte first
    cpu_push((value >> 8) & 0xFF); // High byte second
}

static inline uint16_t cpu_pull16(void) {
    uint16_t high = cpu_pull();
    uint16_t low = cpu_pull();
    return (high << 8) | low;
}

#endif // CPU_STATE_H
