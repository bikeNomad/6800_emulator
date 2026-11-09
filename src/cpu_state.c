/**
 * MC6800 CPU State Implementation
 */

#include "cpu_state.h"
#include "board_config.h"
#include "bus.h"
#include "clock.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"
#include "memory_fingerprint.h"
#include "memory_map.h"
#include <stdio.h>

// Global CPU state
cpu_state_t cpu;

// Initialize CPU state
void cpu_init(void)
{
	// Initialize registers
	cpu.pc = cpu.last_opcode_address = 0x0000;
	cpu.a = 0x00;
	cpu.b = 0x00;
	cpu.x = 0x0000;
	cpu.sp = 0x0000;
	cpu.ccr = CCR_FIXED | CCR_I; // Bits 7-6 always 1, interrupts masked
	cpu.halted = true;           // Start halted
	cpu.running = false;
	cpu.wai_state = false; // Not waiting for interrupt
	cpu.instruction_count = 0;

	// Initialize breakpoints
	cpu.breakpoint_count = 0;
	for (int i = 0; i < MAX_BREAKPOINTS; i++) {
		cpu.breakpoints[i] = 0xFFFF; // Invalid address
	}
	cpu.stop_requested = false;
	cpu.stop_reason = 0;
	cpu.stop_opcode = 0;

	// Turn off all LEDs at startup
	led_all_off();
}

// Check if CPU is running
bool cpu_is_running(void)
{
	return cpu.running && !cpu.halted;
}

// Breakpoint functions
bool cpu_add_breakpoint(uint16_t address)
{
	if (cpu.breakpoint_count >= MAX_BREAKPOINTS) {
		return false; // No space for more breakpoints
	}

	// Check if breakpoint already exists
	for (uint8_t i = 0; i < cpu.breakpoint_count; i++) {
		if (cpu.breakpoints[i] == address) {
			return false; // Already exists
		}
	}

	// Add new breakpoint
	cpu.breakpoints[cpu.breakpoint_count++] = address;
	return true;
}

bool cpu_remove_breakpoint(uint16_t address)
{
	for (uint8_t i = 0; i < cpu.breakpoint_count; i++) {
		if (cpu.breakpoints[i] == address) {
			// Shift remaining breakpoints down
			for (uint8_t j = i; j < cpu.breakpoint_count - 1; j++) {
				cpu.breakpoints[j] = cpu.breakpoints[j + 1];
			}
			cpu.breakpoint_count--;
			cpu.breakpoints[cpu.breakpoint_count] = 0xFFFF; // Clear last slot
			return true;
		}
	}
	return false; // Not found
}

void cpu_clear_breakpoints(void)
{
	cpu.breakpoint_count = 0;
	for (int i = 0; i < MAX_BREAKPOINTS; i++) {
		cpu.breakpoints[i] = 0xFFFF;
	}
}

uint8_t cpu_get_breakpoint_count(void)
{
	return cpu.breakpoint_count;
}

const uint16_t *cpu_get_breakpoints(void)
{
	return cpu.breakpoints;
}

// Mnemonic of the opcode at addr if it is in ROM or RAM (no bus cycle).
const char *cpu_mnemonic_at(uint16_t addr)
{
	memory_type_t type = memory_get_type(addr);
	if (type == MEM_TYPE_UNMAPPED) {
		return "?";
	}
	return instruction_get_mnemonic(memory_read_fast(addr));
}

// Note: cpu_get_flag, cpu_set_flag, cpu_update_nz, cpu_update_nzv,
// cpu_push, cpu_pull, cpu_push16, cpu_pull16 are now inline in cpu_state.h

// Print registers. Call only while the emulator core is parked.
void cpu_print_state(printf_func_t printf_func)
{
	// PC is the next instruction to execute; "Last" is the one just executed.
	// The web interface parses "PC: $xxxx", "A: $xx", etc.
	printf_func("  PC: $%04X  (%s)\r\n", cpu.pc, cpu_mnemonic_at(cpu.pc));
	printf_func("  Last: $%04X  (%s)\r\n", cpu.last_opcode_address,
		    cpu_mnemonic_at(cpu.last_opcode_address));
	printf_func("  A:  $%02X\r\n", cpu.a);
	printf_func("  B:  $%02X\r\n", cpu.b);
	printf_func("  X:  $%04X\r\n", cpu.x);
	printf_func("  SP: $%04X\r\n", cpu.sp);
	printf_func("  CCR: $%02X [%c%c%c%c%c%c]\r\n", cpu.ccr, cpu_get_flag(CCR_H) ? 'H' : '-',
		    cpu_get_flag(CCR_I) ? 'I' : '-', cpu_get_flag(CCR_N) ? 'N' : '-',
		    cpu_get_flag(CCR_Z) ? 'Z' : '-', cpu_get_flag(CCR_V) ? 'V' : '-',
		    cpu_get_flag(CCR_C) ? 'C' : '-');
}