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
    // Initialize Pico SDK
    // NOTE: UART moved to GPIO 24 (TX) and GPIO 25 (RX) to avoid conflict with data bus
    stdio_init_all();

    // Small delay for USB to enumerate
    sleep_ms(1000);

    printf("\n\n========================================\n");
    printf("MC6800 Emulator Starting...\n");
    printf("Version 1.0\n");
    printf("Target: RP2350 (Pico 2 W)\n");
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
    eclock_start();

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
