/**
 * MC6800 Emulator for RP2350
 * Main entry point and execution loop
 *
 * Dual-core architecture:
 * - Core 0: CPU emulation and main control
 * - Core 1: Dedicated USB CDC processing
 */

#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "hardware/vreg.h"
#include "hardware/regs/qmi.h"
#include "hardware/structs/qmi.h"


#include "cpu_state.h"
#include "memory.h"
#include "bus.h"
#include "clock.h"
#include "instructions.h"
#include "interrupts.h"
#include "usb_cdc.h"
#include "debug_spi.h"

// Global CPU state
extern cpu_state_t cpu;

// System clock speed in MHz (configurable at build time)
#ifndef SYS_CLOCK_MHZ
#define SYS_CLOCK_MHZ 266  // Default: 266MHz (optimized for 133MHz QSPI flash)
#endif

// QSPI flash interface speed divisor (configurable at build time)
#ifndef QSPI_CLOCK_DIVISOR
#define QSPI_CLOCK_DIVISOR 2  // Default: system clock / 2 (133MHz with 266MHz sys clock)
#endif

// Get current QSPI flash interface speed
uint32_t qspi_get_current_speed(void) {
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);
    return sys_clock_hz / QSPI_CLOCK_DIVISOR;
}

// Configure QSPI (XIP) clock divisor
// This must be called after set_sys_clock_khz() to apply the configured divisor
void qspi_configure_clock(void) {
    // On RP2350, the QSPI interface uses the QMI (Quad Memory Interface) peripheral
    // The clock divisor is configured in the QMI_M0_TIMING register
    // CLKDIV field controls the divisor: 0 = div by 1, 1 = div by 2, 2 = div by 3, etc.

    // Read current timing configuration
    uint32_t timing = qmi_hw->m[0].timing;

    // Clear the CLKDIV field (bits 16:23)
    timing &= ~(0xFF << QMI_M0_TIMING_CLKDIV_LSB);

    // Set new divisor (subtract 1 because 0 = div by 1, 1 = div by 2, etc.)
    uint32_t divisor_value = (QSPI_CLOCK_DIVISOR > 0) ? (QSPI_CLOCK_DIVISOR - 1) : 0;
    timing |= (divisor_value << QMI_M0_TIMING_CLKDIV_LSB);

    // Write back the timing configuration
    qmi_hw->m[0].timing = timing;
}

// Report QSPI flash interface speed configuration
void qspi_report_speed(void) {
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);

    // Read actual divisor from QMI hardware
    uint32_t timing = qmi_hw->m[0].timing;
    uint32_t clkdiv_field = (timing >> QMI_M0_TIMING_CLKDIV_LSB) & 0xFF;
    uint32_t actual_divisor = clkdiv_field + 1;  // Hardware uses 0-based divisor

    uint32_t qspi_target_hz = sys_clock_hz / QSPI_CLOCK_DIVISOR;
    uint32_t qspi_actual_hz = sys_clock_hz / actual_divisor;

    printf("QSPI bus speed: target %lu MHz (divisor: %d), actual %lu MHz (divisor: %lu)\n",
           qspi_target_hz / 1000000, QSPI_CLOCK_DIVISOR,
           qspi_actual_hz / 1000000, actual_divisor);
}

// Core 1: Dedicated USB CDC processing
void core1_entry() {
    printf("Core 1: USB CDC processing started\n");

    while (1) {
        // Dedicated USB CDC task processing on Core 1
        usb_cdc_task();

        // Small yield to prevent busy-waiting
        tight_loop_contents();
    }
}

int main() {
    // Set system clock based on build-time configuration
    // Default is 300MHz for better emulation performance (>2x speedup over default 150MHz)
    // RP2350 can safely run at 300MHz with proper voltage
    vreg_set_voltage(VREG_VOLTAGE_1_25);
    sleep_ms(10);
    set_sys_clock_khz(SYS_CLOCK_MHZ * 1000, true);

    // NOTE: Do NOT call qspi_configure_clock() here!
    // Modifying QSPI timing while executing from XIP flash causes a crash.
    // The Pico SDK's set_sys_clock_khz() already handles QSPI timing appropriately.

    // Report QSPI bus speed configuration (read-only, safe to call)
    qspi_report_speed();

    // Initialize Pico SDK
    // NOTE: UART moved to GPIO 24 (TX) and GPIO 25 (RX) to avoid conflict with data bus
    stdio_init_all();

    // Small delay for USB to enumerate
    sleep_ms(1000);

    uint32_t sys_clock_hz = clock_get_hz(clk_sys);

    printf("\n\n========================================\n");
    printf("MC6800 Emulator Starting...\n");
    printf("Version 1.0\n");
    printf("Target: RP2350 (Pico 2 W)\n");
    printf("System Clock: %lu MHz\n", sys_clock_hz / 1000000);
    printf("UART Debug Output Active\n");
    printf("========================================\n\n");

    // Initialize all subsystems
    printf("Initializing USB CDC...\n");
    usb_cdc_init();

    printf("Initializing memory subsystem...\n");
    memory_init();

    printf("Initializing bus interface...\n");
    bus_init();

    printf("Initializing E clock...\n");
    eclock_init();
    // Note: E clock will be started by cpu_start() when user runs 'run' command

    printf("Initializing PIO bus cycles...\n");
    bus_cycle_pio_init();

    printf("Initializing debug SPI...\n");
    debug_spi_init();

    printf("Initializing CPU state...\n");
    cpu_init();

    printf("Initializing interrupt handling...\n");
    interrupts_init();

    printf("\nMC6800 Emulator Ready\n");
    fflush(stdout);

    printf("Launching Core 1 for USB CDC processing...\n");
    fflush(stdout);
    sleep_ms(100);

    // Launch Core 1 for dedicated USB CDC processing
    multicore_launch_core1(core1_entry);
    sleep_ms(100);

#if COUNT_INSTRUCTIONS
    printf("Initializing instruction counting...\n");
    instruction_count_initialize();
#endif

    printf("Core 0: CPU emulation started\n");
    fflush(stdout);
    printf("Waiting for EPROM load via USB...\n\n");
    fflush(stdout);

    // Main execution loop (Core 0)
    while (1) {
        // Core 1 handles all USB CDC processing

        // Check for hardware /RESET assertion (active LOW)
        // bus_read_reset() returns true when /RESET is asserted (LOW)
        if (bus_read_reset()) {
            // /RESET is asserted - force halt if running
            if (cpu_is_running()) {
                cpu_halt();
                usb_cdc_printf("CPU halted by /RESET assertion\r\n");
            }
            // Stay halted while /RESET is LOW
            tight_loop_contents();
            continue;
        }

        // If CPU is running (not halted), execute instructions
        if (cpu_is_running()) {
            // Check for interrupt requests (always check, even during WAI)
            interrupt_check();

            // Only execute instructions if not in WAI state
            if (!cpu.wai_state) {
                // Check for breakpoints before executing instruction
                if (cpu_check_breakpoint(cpu.pc) && !cpu.stopped_at_breakpoint) {
                    // Breakpoint hit - halt CPU and set flag to skip check on next run
                    cpu_halt();
                    cpu.stopped_at_breakpoint = true;
                    usb_cdc_printf("CPU halted at breakpoint at PC=$%04X\r\n", cpu.pc);
                    continue;  // Skip instruction execution
                }

                // Execute one instruction (cycle-accurate)
                bus_sync();
                instruction_execute();

                // If we just executed an instruction while stopped at a breakpoint,
                // clear the flag so breakpoint checking resumes
                if (cpu.stopped_at_breakpoint) {
                    cpu.stopped_at_breakpoint = false;
                }

                // Log execution to debug SPI
                debug_spi_log();
            }
            // If in WAI state, we continue looping to check for interrupts
            // but don't execute instructions until an interrupt wakes us
        } else {
            // CPU halted, yield to reduce power consumption
            tight_loop_contents();
        }

        // Check if CMOS needs auto-save (deferred write after idle period)
        memory_check_cmos_autosave();
    }

    return 0;
}
