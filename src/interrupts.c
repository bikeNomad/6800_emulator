/**
 * MC6800 Interrupt Handling Implementation
 */

#include "interrupts.h"
#include "board_config.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "emulator.h"
#include "hardware/gpio.h"
#include "hardware/sync.h"
#include "memory.h"

// Previous interrupt line states (for edge detection)
static bool last_irq_state = false;
static bool last_nmi_state = false;
static bool last_reset_state = false;

// Initialize interrupt handling
void interrupts_init(void)
{
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
interrupt_t __time_critical_func(interrupt_check)(void)
{
	bool irq = bus_read_irq();
	bool nmi = bus_read_nmi();
	bool reset = bus_read_reset();

	// RESET has highest priority (level-triggered)
	if (reset) {
		// reset CPU state on leading edge
		if (!last_reset_state) {
			last_reset_state = reset;
			interrupt_service_reset();
		}
		return INT_RESET;
	}
	last_reset_state = false;

	// NMI second priority (edge-triggered, falling edge)
	if (nmi && !last_nmi_state) {
		last_nmi_state = nmi;
		interrupt_service_nmi();
		return INT_NMI;
	}
	last_nmi_state = nmi;

	// IRQ lowest priority (level-triggered, maskable)
	if (irq && !cpu_get_flag(CCR_I)) {
		interrupt_service_irq();
		return INT_IRQ;
	}

	return INT_NONE;
}

// Service RESET interrupt
void interrupt_service_reset(void)
{
	DEBUG_INT_PRINTF("\n*** RESET ***\n");

	led_all_off();

	// If the reset vector was unmapped, we need to read directly from the
	// ROMs. Which means that we need the E Clock to be running.

	bool clock_was_running = eclock_is_running();
	memory_type_t type = memory_get_type(VECTOR_RESET);
	if (type == MEM_TYPE_UNMAPPED) {
		if (!clock_was_running) {
			eclock_start();
		}
	}

	// Load PC from reset vector
	uint8_t pch = memory_read_fast(VECTOR_RESET);
	uint8_t pcl = memory_read_fast(VECTOR_RESET + 1);
	cpu.pc = (pch << 8) | pcl;

	if (eclock_is_running() && !clock_was_running) {
		eclock_stop();
	}

	// Reset CPU state per datasheet
	cpu.a = 0;
	cpu.b = 0;
	cpu.x = 0x0000;
	cpu.sp = 0x0000;             // Stack pointer will be initialized by reset routine
	cpu.ccr = CCR_FIXED | CCR_I; // Interrupts masked
	cpu.wai_state = false;       // Clear WAI state

	cpu.halted = true;
	cpu.running = false;

	cpu.instruction_count = 0; // Reset instruction counter
	clock_reset_counters();

	DEBUG_INT_PRINTF("Reset vector: $%04X\n", cpu.pc);
}

// Service NMI interrupt
void interrupt_service_nmi(void)
{
	DEBUG_INT_PRINTF("*** NMI at PC=$%04X ***\n", cpu.pc);

	// If coming from WAI, registers are already on stack
	if (!cpu.wai_state) {
		cpu_stack_registers();
	}
	cpu.wai_state = false;     // Clear WAI state if we were waiting
	cpu_set_flag(CCR_I, true); // Set interrupt mask
	cpu_load_pc_from_vector(VECTOR_NMI);

	DEBUG_INT_PRINTF("NMI vector: $%04X\n", cpu.pc);
}

// Service IRQ interrupt
void interrupt_service_irq(void)
{
	DEBUG_INT_PRINTF("*** IRQ at PC=$%04X ***\n", cpu.pc);

	// If coming from WAI, registers are already on stack
	if (!cpu.wai_state) {
		cpu_stack_registers();
	}

	cpu.wai_state = false; // Clear WAI state if we were waiting

	cpu_set_flag(CCR_I, true); // Set interrupt mask

	cpu_load_pc_from_vector(VECTOR_IRQ); // Load PC from IRQ vector

	DEBUG_INT_PRINTF("IRQ vector: $%04X\n", cpu.pc);
}
