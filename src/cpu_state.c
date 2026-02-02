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
 * MC6800 CPU State Implementation
 */

#include "cpu_state.h"
#include "board_config.h"
#include "bus.h"
#include "clock.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
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
	cpu.stopped_at_breakpoint = false;

	// Turn off all LEDs at startup
	led_all_off();

	if (memory_is_address_mapped(VECTOR_RESET)) {
		// ROM is loaded, perform reset to load vector into PC
		printf("CPU initialized\n");
	} else {
		printf("CPU initialized: PC=$%04X SP=$%04X CCR=$%02X (no ROM)\n", cpu.pc, cpu.sp,
		       cpu.ccr);
	}
}

// Check if CPU is running
bool cpu_is_running(void)
{
	return cpu.running && !cpu.halted;
}

// Start CPU execution
void cpu_start(void)
{
	// Check if /RESET is asserted (bus_read_reset() returns true when LOW)
	if (bus_read_reset()) {
		printf("WARNING: Cannot start CPU while /RESET is asserted\n");
		return; // Don't start
	}

	cpu.running = true;
	cpu.halted = false;
	eclock_start(); // Start E clock PIO (also resets PIO counter)
	printf("CPU started: PC=$%04X\n", cpu.pc);
}

// Halt CPU execution
void cpu_halt(void)
{
	cpu.halted = true;
	__mem_fence_release(); // Memory barrier - ensure Core 0 sees halt flag
			       // immediately
	eclock_stop();         // Stop E clock PIO

	// Turn off all LEDs when halted
	led_all_off();

	printf("CPU halted at PC=$%04X\n", cpu.pc);
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

// Note: cpu_get_flag, cpu_set_flag, cpu_update_nz, cpu_update_nzv,
// cpu_push, cpu_pull, cpu_push16, cpu_pull16 are now inline in cpu_state.h

void cpu_print_state(printf_func_t printf_func)
{
	printf_func("  PC: $%04X  (%s)\r\n", cpu.last_opcode_address,
		    instruction_get_mnemonic(memory_read_rom_shadow(cpu.last_opcode_address)));
	printf_func("  A:  $%02X\r\n", cpu.a);
	printf_func("  B:  $%02X\r\n", cpu.b);
	printf_func("  X:  $%04X\r\n", cpu.x);
	printf_func("  SP: $%04X\r\n", cpu.sp);
	printf_func("  CCR: $%02X [%c%c%c%c%c%c]\r\n", cpu.ccr, cpu_get_flag(CCR_H) ? 'H' : '-',
		    cpu_get_flag(CCR_I) ? 'I' : '-', cpu_get_flag(CCR_N) ? 'N' : '-',
		    cpu_get_flag(CCR_Z) ? 'Z' : '-', cpu_get_flag(CCR_V) ? 'V' : '-',
		    cpu_get_flag(CCR_C) ? 'C' : '-');
}