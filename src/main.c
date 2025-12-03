/**
 * MC6800 Emulator for RP2350
 * Main entry point and execution loop
 */

#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
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

int main() {
    // Initialize Pico SDK
    stdio_init_all();

    // Small delay for USB to enumerate
    sleep_ms(1000);

    printf("\n\nMC6800 Emulator Starting...\n");
    printf("Version 1.0\n");
    printf("Target: RP2350\n\n");

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
    printf("Waiting for EPROM load via USB...\n\n");

    // Main execution loop
    while (1) {
        // Process USB commands (non-blocking)
        usb_cdc_task();

        // If CPU is running (not halted), execute instructions
        if (cpu_is_running()) {
            // Check for interrupt requests
            interrupt_check();

            // Execute one instruction (cycle-accurate)
            instruction_execute();

            // Log execution to debug SPI
            debug_spi_log();
        } else {
            // CPU halted, yield to USB processing
            sleep_us(100);
        }
    }

    return 0;
}
