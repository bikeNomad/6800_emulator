/**
 * MC6800 Interrupt Handling Implementation
 */

#include "interrupts.h"
#include "cpu_state.h"
#include "bus.h"
#include "memory.h"
#include "clock.h"
#include "board_config.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include <stdio.h>

// Previous interrupt line states (for edge detection)
static bool last_irq_state = false;
static bool last_nmi_state = false;
static bool last_reset_state = false;

// Initialize interrupt handling
void interrupts_init(void) {
    // Read initial states
    last_irq_state = bus_read_irq();
    last_nmi_state = bus_read_nmi();
    last_reset_state = bus_read_reset();

    DEBUG_INT_PRINTF("Interrupt handling initialized\n");
    DEBUG_INT_PRINTF("  IRQ vector: $%04X\n", VECTOR_IRQ);
    DEBUG_INT_PRINTF("  NMI vector: $%04X\n", VECTOR_NMI);
    DEBUG_INT_PRINTF("  RST vector: $%04X\n", VECTOR_RESET);
}

// Check for pending interrupts
void __time_critical_func(interrupt_check)(void) {
    bool irq = bus_read_irq();
    bool nmi = bus_read_nmi();
    bool reset = bus_read_reset();

    // RESET has highest priority (edge-triggered)
    if (reset && !last_reset_state) {
        last_reset_state = reset;
        interrupt_service_reset();
        return;
    }
    last_reset_state = reset;

    // NMI second priority (edge-triggered, falling edge)
    if (nmi && !last_nmi_state) {
        last_nmi_state = nmi;
        cpu.nmi_pending = true;
    }
    last_nmi_state = nmi;

    // Service NMI if pending and not already in interrupt
    if (cpu.nmi_pending) {
        interrupt_service_nmi();
        cpu.nmi_pending = false;
        return;
    }

    // IRQ lowest priority (level-triggered, maskable)
    if (irq && !cpu_get_flag(CCR_I)) {
        interrupt_service_irq();
    }
}

// Service RESET interrupt
void interrupt_service_reset(void) {
    DEBUG_INT_PRINTF("\n*** RESET ***\n");

    bool clock_was_running = eclock_is_running();
    if (clock_was_running) {
        eclock_stop();
    }

    led_all_off();

    // Load PC from reset vector
    uint8_t pch = memory_read_fast(VECTOR_RESET);
    uint8_t pcl = memory_read_fast(VECTOR_RESET + 1);
    cpu.pc = (pch << 8) | pcl;

    // Reset CPU state per datasheet
    cpu.a = 0;
    cpu.b = 0;
    cpu.x = 0x0000;
    cpu.sp = 0x0000;  // Stack pointer will be initialized by reset routine
    cpu.ccr = CCR_FIXED | CCR_I;  // Interrupts masked
    cpu.wai_state = false;  // Clear WAI state

    cpu.halted = true;
    cpu.running = false;

    cpu.instruction_count = 0;  // Reset instruction counter
    clock_reset_counters();

    DEBUG_INT_PRINTF("Reset vector: $%04X\n", cpu.pc);

    // stay halted while /RESET still LOW 
    while (bus_read_reset()) {
        tight_loop_contents();
    }

    // /RESET is HIGH - automatically start execution (MC6800 behavior)
    cpu.running = true;
    cpu.halted = false;
    if (clock_was_running) {
        eclock_start();
    }

    DEBUG_INT_PRINTF("CPU started after RESET release\n");

    // Memory barrier - ensure Core 0 sees updated flags immediately
    __mem_fence_release();
}

// Service NMI interrupt
void interrupt_service_nmi(void) {
    DEBUG_INT_PRINTF("*** NMI at PC=$%04X ***\n", cpu.pc);

    // If coming from WAI, registers are already on stack
    if (!cpu.wai_state) {
        // Push registers onto stack (12 cycles total)
        cpu_push16(cpu.pc);    // Push PC
        cpu_push16(cpu.x);     // Push X
        cpu_push(cpu.a);       // Push A
        cpu_push(cpu.b);       // Push B
        cpu_push(cpu.ccr);     // Push CCR
    }

    // Clear WAI state if we were waiting
    cpu.wai_state = false;

    // Set interrupt mask
    cpu_set_flag(CCR_I, true);

    // Load PC from NMI vector
    uint8_t pch = memory_read_fast(VECTOR_NMI);
    uint8_t pcl = memory_read_fast(VECTOR_NMI + 1);
    cpu.pc = (pch << 8) | pcl;

    DEBUG_INT_PRINTF("NMI vector: $%04X\n", cpu.pc);
}

// Service IRQ interrupt
void interrupt_service_irq(void) {
    DEBUG_INT_PRINTF("*** IRQ at PC=$%04X ***\n", cpu.pc);

    // If coming from WAI, registers are already on stack
    if (!cpu.wai_state) {
        // Push registers onto stack (12 cycles total)
        cpu_push16(cpu.pc);    // Push PC
        cpu_push16(cpu.x);     // Push X
        cpu_push(cpu.a);       // Push A
        cpu_push(cpu.b);       // Push B
        cpu_push(cpu.ccr);     // Push CCR
    }

    // Clear WAI state if we were waiting
    cpu.wai_state = false;

    // Set interrupt mask
    cpu_set_flag(CCR_I, true);

    // Load PC from IRQ vector
    uint8_t pch = memory_read_fast(VECTOR_IRQ);
    uint8_t pcl = memory_read_fast(VECTOR_IRQ + 1);
    cpu.pc = (pch << 8) | pcl;

    DEBUG_INT_PRINTF("IRQ vector: $%04X\n", cpu.pc);
}
