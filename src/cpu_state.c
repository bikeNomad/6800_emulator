/**
 * MC6800 CPU State Implementation
 */

#include "cpu_state.h"
#include "memory.h"
#include "bus.h"
#include "clock.h"
#include "interrupts.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include <stdio.h>

// Global CPU state
cpu_state_t cpu;

// Initialize CPU state
void cpu_init(void) {
    // Initialize registers
    cpu.pc = 0x0000;
    cpu.a = 0x00;
    cpu.b = 0x00;
    cpu.x = 0x0000;
    cpu.sp = 0x0000;
    cpu.ccr = CCR_FIXED | CCR_I;  // Bits 7-6 always 1, interrupts masked
    cpu.halted = true;   // Start halted
    cpu.running = false;
    cpu.wai_state = false;  // Not waiting for interrupt
    cpu.irq_pending = false;
    cpu.nmi_pending = false;
    cpu.instruction_count = 0;

    // Initialize breakpoints
    cpu.breakpoint_count = 0;
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        cpu.breakpoints[i] = 0xFFFF;  // Invalid address
    }
    cpu.stopped_at_breakpoint = false;

    // Turn off all LEDs at startup
    led_all_off();

    // Check if ROM has been loaded (reset vector is not 0xFFFF)
    uint8_t vec_h = memory_read_fast(VECTOR_RESET);
    uint8_t vec_l = memory_read_fast(VECTOR_RESET + 1);
    uint16_t reset_vector = (vec_h << 8) | vec_l;

    if (reset_vector != 0xFFFF) {
        // ROM is loaded, perform reset to load vector into PC
        printf("CPU initialized: ROM detected, loading reset vector\n");
        interrupt_service_reset();
    } else {
        printf("CPU initialized: PC=$%04X SP=$%04X CCR=$%02X (no ROM)\n",
               cpu.pc, cpu.sp, cpu.ccr);
    }
}

// Check if CPU is running
bool cpu_is_running(void) {
    return cpu.running && !cpu.halted;
}

// Start CPU execution
void cpu_start(void) {
    // Check if /RESET is asserted (bus_read_reset() returns true when LOW)
    if (bus_read_reset()) {
        printf("WARNING: Cannot start CPU while /RESET is asserted\n");
        return;  // Don't start
    }

    cpu.running = true;
    cpu.halted = false;
    eclock_start();  // Start E clock PIO (also resets PIO counter)
    printf("CPU started: PC=$%04X\n", cpu.pc);
}

// Halt CPU execution
void cpu_halt(void) {
    cpu.halted = true;
    __mem_fence_release();  // Memory barrier - ensure Core 0 sees halt flag immediately
    eclock_stop();  // Stop E clock PIO

    // Turn off all LEDs when halted
    led_all_off();

    printf("CPU halted at PC=$%04X\n", cpu.pc);
}

// Breakpoint functions
bool cpu_add_breakpoint(uint16_t address) {
    if (cpu.breakpoint_count >= MAX_BREAKPOINTS) {
        return false;  // No space for more breakpoints
    }

    // Check if breakpoint already exists
    for (uint8_t i = 0; i < cpu.breakpoint_count; i++) {
        if (cpu.breakpoints[i] == address) {
            return false;  // Already exists
        }
    }

    // Add new breakpoint
    cpu.breakpoints[cpu.breakpoint_count++] = address;
    return true;
}

bool cpu_remove_breakpoint(uint16_t address) {
    for (uint8_t i = 0; i < cpu.breakpoint_count; i++) {
        if (cpu.breakpoints[i] == address) {
            // Shift remaining breakpoints down
            for (uint8_t j = i; j < cpu.breakpoint_count - 1; j++) {
                cpu.breakpoints[j] = cpu.breakpoints[j + 1];
            }
            cpu.breakpoint_count--;
            cpu.breakpoints[cpu.breakpoint_count] = 0xFFFF;  // Clear last slot
            return true;
        }
    }
    return false;  // Not found
}

void cpu_clear_breakpoints(void) {
    cpu.breakpoint_count = 0;
    for (int i = 0; i < MAX_BREAKPOINTS; i++) {
        cpu.breakpoints[i] = 0xFFFF;
    }
}

uint8_t cpu_get_breakpoint_count(void) {
    return cpu.breakpoint_count;
}

const uint16_t* cpu_get_breakpoints(void) {
    return cpu.breakpoints;
}

// Note: cpu_get_flag, cpu_set_flag, cpu_update_nz, cpu_update_nzv,
// cpu_push, cpu_pull, cpu_push16, cpu_pull16 are now inline in cpu_state.h
