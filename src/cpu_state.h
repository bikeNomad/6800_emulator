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
 * MC6800 CPU State
 * Defines the processor registers and state
 */

#ifndef CPU_STATE_H
#define CPU_STATE_H

#include "emulator.h"
#include "memory.h" // For memory_read_fast/write_fast in inline stack functions

// Condition Code Register (CCR) flag bits
#define CCR_C     0x01 // Carry
#define CCR_V     0x02 // Overflow
#define CCR_Z     0x04 // Zero
#define CCR_N     0x08 // Negative
#define CCR_I     0x10 // Interrupt mask
#define CCR_H     0x20 // Half-carry
#define CCR_FIXED 0xC0 // Bits 7-6 always set to 1

// MC6800 CPU state
typedef struct {
	uint16_t pc;                           // Program counter
	uint8_t a;                             // Accumulator A
	uint8_t b;                             // Accumulator B
	uint16_t x;                            // Index register
	uint16_t sp;                           // Stack pointer
	uint8_t ccr;                           // Condition code register
	bool halted;                           // CPU halt state (waiting for EPROM or manual halt)
	bool running;                          // CPU running state (set by USB RUN command)
	bool wai_state;                        // CPU waiting for interrupt (WAI instruction)
	uint64_t instruction_count;            // Total instructions executed since reset
	volatile uint16_t last_opcode_address; // address of last opcode fetched

	// Set by an instruction that stops the CPU (HCF, unimplemented opcode).
	// Consumed by the emulator state machine.
	bool stop_requested;
	uint8_t stop_reason; // emu_halt_reason_t
	uint8_t stop_opcode; // opcode that caused the stop

} cpu_state_t;

// Global CPU state (defined in cpu_state.c)
extern cpu_state_t cpu;

// Initialize CPU state
void cpu_init(void);

// Check if CPU is running
bool cpu_is_running(void);

// Get CCR flag (inline for performance)
static __force_inline bool cpu_get_flag(uint8_t flag)
{
	return (cpu.ccr & flag) != 0;
}

// Set CCR flag (inline for performance)
static __force_inline void cpu_set_flag(uint8_t flag, bool value)
{
	if (value) {
		cpu.ccr |= flag;
	} else {
		cpu.ccr &= ~flag;
	}
	// Ensure bits 7-6 always remain 1
	cpu.ccr |= CCR_FIXED;
}

// Update N and Z flags based on result (inline for performance)
static __force_inline void cpu_update_nz(uint8_t result)
{
	cpu_set_flag(CCR_Z, result == 0);
	cpu_set_flag(CCR_N, (result & 0x80) != 0);
}

// Update N, Z, V flags for arithmetic operations (inline for performance)
static __force_inline void cpu_update_nzv(uint8_t result, uint8_t operand1, uint8_t operand2,
					  bool subtraction)
{
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
static __force_inline void cpu_push(uint8_t value)
{
	memory_write_fast(cpu.sp, value); // Fast-path for stack RAM
	cpu.sp--;
}

static __force_inline uint8_t cpu_pull(void)
{
	cpu.sp++;
	return memory_read_fast(cpu.sp); // Fast-path for stack RAM
}

static __force_inline void cpu_push16(uint16_t value)
{
	cpu_push(value & 0xFF);        // Low byte first
	cpu_push((value >> 8) & 0xFF); // High byte second
}

static __force_inline uint16_t cpu_pull16(void)
{
	uint16_t high = cpu_pull();
	uint16_t low = cpu_pull();
	return (high << 8) | low;
}

// Push registers onto stack (7 cycles total)
static __force_inline void cpu_stack_registers(void)
{
	cpu_push16(cpu.pc); // Push PC
	cpu_push16(cpu.x);  // Push X
	cpu_push(cpu.a);    // Push A
	cpu_push(cpu.b);    // Push B
	cpu_push(cpu.ccr);  // Push CCR
}

// Set PC from vector (2 cycles total)
static __force_inline void cpu_load_pc_from_vector(uint16_t address)
{
	uint8_t pch = memory_read_fast(address);
	uint8_t pcl = memory_read_fast(address + 1);
	cpu.pc = (pch << 8) | pcl;
}

void cpu_print_state(printf_func_t printf_func);

// Mnemonic of the opcode at addr if it is in ROM or RAM (no bus cycle).
const char *cpu_mnemonic_at(uint16_t addr);

#endif // CPU_STATE_H
