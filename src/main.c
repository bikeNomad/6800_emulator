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
#define SYS_CLOCK_MHZ 300  // Default: 300MHz
#endif

// QSPI flash interface speed divisor (configurable at build time)
#ifndef QSPI_CLOCK_DIVISOR
#define QSPI_CLOCK_DIVISOR 3  // Default: system clock / 3 (100MHz with 300MHz sys clock)
#endif

// Get current QSPI flash interface speed
uint32_t qspi_get_current_speed(void) {
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);
    return sys_clock_hz / QSPI_CLOCK_DIVISOR;
}

// Report QSPI flash interface speed configuration
void qspi_report_speed(void) {
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);
    uint32_t qspi_freq_hz = sys_clock_hz / QSPI_CLOCK_DIVISOR;

    printf("QSPI bus speed: %lu MHz (system clock divisor: %d)\n",
           qspi_freq_hz / 1000000, QSPI_CLOCK_DIVISOR);
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

    // Report QSPI bus speed configuration
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

    printf("Core 0: CPU emulation started\n");
    fflush(stdout);
    printf("Waiting for EPROM load via USB...\n\n");
    fflush(stdout);

    // Main execution loop (Core 0)
    while (1) {
        // Core 1 handles all USB CDC processing

        // If CPU is running (not halted), execute instructions
        if (cpu_is_running()) {
            // Check for interrupt requests
            interrupt_check();

            // Execute one instruction (cycle-accurate)
            instruction_execute();

            // Log execution to debug SPI
            debug_spi_log();
        } else {
            // CPU halted, yield to reduce power consumption
            __wfi(); // Wait for interrupt
        }

        // Check if CMOS needs auto-save (deferred write after idle period)
        memory_check_cmos_autosave();
    }

    return 0;
}
