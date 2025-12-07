/**
 * MC6800 CPU State Implementation
 */

#include "cpu_state.h"
#include "memory.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include <stdio.h>

// Global CPU state
cpu_state_t cpu;

// Initialize CPU state
void cpu_init(void) {
    // Reset vector will be loaded after EPROM is programmed
    cpu.pc = 0x0000;
    cpu.a = 0x00;
    cpu.b = 0x00;
    cpu.x = 0x0000;
    cpu.sp = 0x0000;
    cpu.ccr = CCR_FIXED | CCR_I;  // Bits 7-6 always 1, interrupts masked
    cpu.halted = true;   // Start halted, waiting for EPROM
    cpu.running = false;
    cpu.irq_pending = false;
    cpu.nmi_pending = false;
    cpu.instruction_count = 0;

#if BOARD_TYPE == BOARD_NED_SYS7
    // Turn off all LEDs (active low, so HIGH = off)
    gpio_put(GPIO_LED_ROM, 1);
    gpio_put(GPIO_LED_RAM, 1);
    gpio_put(GPIO_LED_UNMAPPED, 1);
#endif

    printf("CPU initialized: PC=$%04X SP=$%04X CCR=$%02X\n",
           cpu.pc, cpu.sp, cpu.ccr);
}

// Check if CPU is running
bool cpu_is_running(void) {
    return cpu.running && !cpu.halted;
}

// Start CPU execution
void cpu_start(void) {
    cpu.running = true;
    cpu.halted = false;
    printf("CPU started: PC=$%04X\n", cpu.pc);
}

// Halt CPU execution
void cpu_halt(void) {
    cpu.halted = true;

#if BOARD_TYPE == BOARD_NED_SYS7
    // Turn off all LEDs (active low, so HIGH = off)
    gpio_put(GPIO_LED_ROM, 1);
    gpio_put(GPIO_LED_RAM, 1);
    gpio_put(GPIO_LED_UNMAPPED, 1);
#endif

    printf("CPU halted at PC=$%04X\n", cpu.pc);
}

// Note: cpu_get_flag, cpu_set_flag, cpu_update_nz, cpu_update_nzv,
// cpu_push, cpu_pull, cpu_push16, cpu_pull16 are now inline in cpu_state.h
