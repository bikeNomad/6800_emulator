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

	// Breakpoint system
	uint16_t breakpoints[MAX_BREAKPOINTS];
	uint8_t breakpoint_count;
	bool stopped_at_breakpoint; // True when halted at breakpoint, skip next
				    // breakpoint check
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

// Breakpoint functions
bool cpu_add_breakpoint(uint16_t address);
bool cpu_remove_breakpoint(uint16_t address);
void cpu_clear_breakpoints(void);
uint8_t cpu_get_breakpoint_count(void);
const uint16_t *cpu_get_breakpoints(void);

static inline bool cpu_check_breakpoint(uint16_t address)
{
	for (uint8_t i = 0; i < cpu.breakpoint_count; i++) {
		if (cpu.breakpoints[i] == address) {
			return true;
		}
	}
	return false;
}

// Get CCR flag (inline for performance)
static inline bool cpu_get_flag(uint8_t flag)
{
	return (cpu.ccr & flag) != 0;
}

// Set CCR flag (inline for performance)
static inline void cpu_set_flag(uint8_t flag, bool value)
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
static inline void cpu_update_nz(uint8_t result)
{
	cpu_set_flag(CCR_Z, result == 0);
	cpu_set_flag(CCR_N, (result & 0x80) != 0);
}

// Update N, Z, V flags for arithmetic operations (inline for performance)
static inline void cpu_update_nzv(uint8_t result, uint8_t operand1, uint8_t operand2,
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
static inline void cpu_push(uint8_t value)
{
	memory_write_fast(cpu.sp, value); // Fast-path for stack RAM
	cpu.sp--;
}

static inline uint8_t cpu_pull(void)
{
	cpu.sp++;
	return memory_read_fast(cpu.sp); // Fast-path for stack RAM
}

static inline void cpu_push16(uint16_t value)
{
	cpu_push(value & 0xFF);        // Low byte first
	cpu_push((value >> 8) & 0xFF); // High byte second
}

static inline uint16_t cpu_pull16(void)
{
	uint16_t high = cpu_pull();
	uint16_t low = cpu_pull();
	return (high << 8) | low;
}

// Push registers onto stack (7 cycles total)
static inline void cpu_stack_registers(void)
{
	cpu_push16(cpu.pc); // Push PC
	cpu_push16(cpu.x);  // Push X
	cpu_push(cpu.a);    // Push A
	cpu_push(cpu.b);    // Push B
	cpu_push(cpu.ccr);  // Push CCR
}

// Set PC from vector (2 cycles total)
static inline void cpu_load_pc_from_vector(uint16_t address)
{
	uint8_t pch = memory_read_fast(address);
	uint8_t pcl = memory_read_fast(address + 1);
	cpu.pc = (pch << 8) | pcl;
}

void cpu_print_state(printf_func_t printf_func);

#endif // CPU_STATE_H
