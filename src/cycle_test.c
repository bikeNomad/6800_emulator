/**
 * MC6800 Instruction Cycle Count Testing
 */

#include "cycle_test.h"
#include "cpu_state.h"
#include "memory.h"
#include "clock.h"
#include "instructions.h"
#include "usb_cdc.h"
#include "hardware/clocks.h"
#include <stdio.h>
#include <string.h>

// System clock frequency (RP2350 default)
static uint32_t sys_clk_freq = 0;

// E clock frequency (3.579545 MHz / 4 = 0.894886 MHz)
#define E_CLOCK_HZ 894886.25f

// DWT (Data Watchpoint and Trace) registers for Cortex-M33
#define DWT_CTRL   (*(volatile uint32_t *)0xE0001000)
#define DWT_CYCCNT (*(volatile uint32_t *)0xE0001004)
#define DEM_CR     (*(volatile uint32_t *)0xE000EDFC)

// Initialize timing system
static void timing_init(void) {
    if (sys_clk_freq == 0) {
        sys_clk_freq = clock_get_hz(clk_sys);
        usb_cdc_printf("System clock: %lu Hz\r\n", (unsigned long)sys_clk_freq);
        usb_cdc_printf("E clock: %.2f Hz\r\n", E_CLOCK_HZ);
        usb_cdc_printf("Ratio: %.2f sys cycles per E cycle\r\n", (float)sys_clk_freq / E_CLOCK_HZ);
    }
}

// Read cycle counter
static inline uint32_t get_cycle_counter(void) {
    return DWT_CYCCNT;
}

// Enable DWT cycle counter
static void enable_cycle_counter(void) {
    // Enable trace system (DEMCR)
    DEM_CR |= (1 << 24);  // TRCENA - enable trace and debug blocks

    // Reset cycle counter
    DWT_CYCCNT = 0;

    // Enable cycle counter (DWT_CTRL)
    DWT_CTRL |= (1 << 0);  // CYCCNTENA - enable cycle counter
}

// Set up test environment for instruction execution
static void setup_test_environment(uint8_t opcode) {
    // Set up test data in memory
    // Direct addressing test data at $10
    memory_write(0x0010, 0x42);
    memory_write(0x0011, 0x55);

    // Extended addressing test data at $1000
    memory_write(0x1000, 0xAA);
    memory_write(0x1001, 0xBB);

    // Test data for indexed addressing (base $0100 + offset)
    for (uint16_t i = 0; i < 256; i++) {
        memory_write(0x0100 + i, 0x11 + i);
    }

    // Set up CPU registers with test values
    cpu.a = 0x12;
    cpu.b = 0x34;
    cpu.x = 0x0100;
    cpu.sp = 0x01FE;  // Leave room for stack operations
    cpu.pc = 0x0200;  // Test code at $0200

    // Set up flags for testing branches
    // Default: all flags clear
    cpu.ccr = 0x00;

    // For branch instructions, set flags to make branches take
    // (to test the taken path)
    if ((opcode >= 0x20 && opcode <= 0x2F) || opcode == 0x8D) {
        // Set flags to make conditional branches take
        switch (opcode) {
            case 0x22: // BHI - needs C=0, Z=0
            case 0x24: // BCC - needs C=0
            case 0x26: // BNE - needs Z=0
            case 0x28: // BVC - needs V=0
            case 0x2A: // BPL - needs N=0
            case 0x2C: // BGE - needs N=V
            case 0x2E: // BGT - needs Z=0 and N=V
                // Flags already clear
                break;
            case 0x23: // BLS - needs C=1 or Z=1
            case 0x25: // BCS - needs C=1
                cpu_set_flag(CCR_C, true);
                break;
            case 0x27: // BEQ - needs Z=1
                cpu_set_flag(CCR_Z, true);
                break;
            case 0x29: // BVS - needs V=1
                cpu_set_flag(CCR_V, true);
                break;
            case 0x2B: // BMI - needs N=1
                cpu_set_flag(CCR_N, true);
                break;
            case 0x2D: // BLT - needs N!=V
                cpu_set_flag(CCR_N, true);
                break;
            case 0x2F: // BLE - needs Z=1 or N!=V
                cpu_set_flag(CCR_Z, true);
                break;
        }
    }

    // Write instruction bytes to memory at PC
    memory_write(cpu.pc, opcode);

    // Write operand bytes (most instructions need at most 2 operand bytes)
    memory_write(cpu.pc + 1, 0x10);  // Direct address / immediate value / offset
    memory_write(cpu.pc + 2, 0x00);  // Extended address high byte
    memory_write(cpu.pc + 3, 0x10);  // Extended address low byte ($1000)

    // For relative branches, use small offset to stay in valid memory
    if ((opcode >= 0x20 && opcode <= 0x2F) || opcode == 0x8D) {
        memory_write(cpu.pc + 1, 0x05);  // Branch forward 5 bytes
    }
}

// Test a single instruction's cycle count
void cycle_test_instruction(uint8_t opcode) {
    // Save current CPU state
    cpu_state_t saved_cpu = cpu;

    // Set up test environment
    setup_test_environment(opcode);

    // Sync any accumulated cycles from test setup before measurement
    eclock_sync_instruction();

    // Warm-up: Execute the instruction once to prime caches and branch predictor
    cpu_state_t warmup_cpu = cpu;
    instruction_execute();

    // Restore state for actual measurement
    cpu = warmup_cpu;
    eclock_sync_instruction();

    // Get starting counts
    uint32_t start_eclock = eclock_get_count();
    uint32_t start_sysclock = get_cycle_counter();

    // Execute the instruction (measured)
    instruction_execute();

    // Get ending counts
    uint32_t end_eclock = eclock_get_count();
    uint32_t end_sysclock = get_cycle_counter();

    uint32_t eclock_cycles = end_eclock - start_eclock;
    uint32_t sysclock_cycles = end_sysclock - start_sysclock;

    // Convert system clock cycles to E clock equivalent
    float sysclock_as_eclock = (float)sysclock_cycles / ((float)sys_clk_freq / E_CLOCK_HZ);

    // Get instruction mnemonic
    const char* mnemonic = instruction_get_mnemonic(opcode);

    // Print result with both timing measurements
    if (mnemonic && strcmp(mnemonic, "???") != 0) {
        usb_cdc_printf("$%02X %-8s : %lu E cycles | %lu sys cycles (%.2f E equiv) | diff: %+.2f\r\n",
               opcode, mnemonic,
               (unsigned long)eclock_cycles,
               (unsigned long)sysclock_cycles,
               sysclock_as_eclock,
               sysclock_as_eclock - (float)eclock_cycles);
    }

    // Restore CPU state
    cpu = saved_cpu;
}

// Test all implemented instructions
void cycle_test_all(void) {
    usb_cdc_send("\r\n");
    usb_cdc_send("========================================\r\n");
    usb_cdc_send("MC6800 Instruction Cycle Count Test\r\n");
    usb_cdc_send("========================================\r\n");
    usb_cdc_send("\r\n");

    // Initialize timing system
    timing_init();
    enable_cycle_counter();

    usb_cdc_send("\r\n");
    usb_cdc_send("Format: $XX MNEMONIC : E cycles | sys cycles (E equiv) | diff\r\n");
    usb_cdc_send("\r\n");

    // Test all 256 possible opcodes
    for (uint16_t opcode = 0; opcode < 256; opcode++) {
        const char* mnemonic = instruction_get_mnemonic((uint8_t)opcode);

        // Skip unimplemented instructions
        if (mnemonic && strcmp(mnemonic, "???") != 0) {
            cycle_test_instruction((uint8_t)opcode);
        }
    }

    usb_cdc_send("\r\n");
    usb_cdc_send("========================================\r\n");
    usb_cdc_send("Test Complete\r\n");
    usb_cdc_send("========================================\r\n");
    usb_cdc_send("\r\n");
}
