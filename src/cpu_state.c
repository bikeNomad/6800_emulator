/**
 * MC6800 CPU State Implementation
 */

#include "cpu_state.h"
#include "memory.h"
#include "clock.h"
#include "interrupts.h"
#include "board_config.h"
#include "hardware/gpio.h"
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
    cpu.running = true;
    cpu.halted = false;
    eclock_start();  // Start E clock PIO (also resets PIO counter)
    printf("CPU started: PC=$%04X\n", cpu.pc);
}

// Halt CPU execution
void cpu_halt(void) {
    cpu.halted = true;
    eclock_stop();  // Stop E clock PIO

    // Turn off all LEDs when halted
    led_all_off();

    printf("CPU halted at PC=$%04X\n", cpu.pc);
}

// Note: cpu_get_flag, cpu_set_flag, cpu_update_nz, cpu_update_nzv,
// cpu_push, cpu_pull, cpu_push16, cpu_pull16 are now inline in cpu_state.h
