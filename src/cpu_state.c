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

#if defined(BOARD_NED_SYS7)
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

#if defined(BOARD_NED_SYS7)
    // Turn off all LEDs (active low, so HIGH = off)
    gpio_put(GPIO_LED_ROM, 1);
    gpio_put(GPIO_LED_RAM, 1);
    gpio_put(GPIO_LED_UNMAPPED, 1);
#endif

    printf("CPU halted at PC=$%04X\n", cpu.pc);
}

// Get CCR flag
bool cpu_get_flag(uint8_t flag) {
    return (cpu.ccr & flag) != 0;
}

// Set CCR flag
void cpu_set_flag(uint8_t flag, bool value) {
    if (value) {
        cpu.ccr |= flag;
    } else {
        cpu.ccr &= ~flag;
    }
    // Ensure bits 7-6 always remain 1
    cpu.ccr |= CCR_FIXED;
}

// Update N and Z flags based on result
void cpu_update_nz(uint8_t result) {
    cpu_set_flag(CCR_Z, result == 0);
    cpu_set_flag(CCR_N, (result & 0x80) != 0);
}

// Update N, Z, V flags for arithmetic operations
void cpu_update_nzv(uint8_t result, uint8_t operand1, uint8_t operand2, bool subtraction) {
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

// Stack operations (push decrements SP, pull increments SP)
void cpu_push(uint8_t value) {
    memory_write(cpu.sp, value);
    cpu.sp--;
}

uint8_t cpu_pull(void) {
    cpu.sp++;
    return memory_read(cpu.sp);
}

void cpu_push16(uint16_t value) {
    cpu_push(value & 0xFF);        // Low byte first
    cpu_push((value >> 8) & 0xFF); // High byte second
}

uint16_t cpu_pull16(void) {
    uint16_t high = cpu_pull();
    uint16_t low = cpu_pull();
    return (high << 8) | low;
}
