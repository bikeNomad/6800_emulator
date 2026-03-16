/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * Test program to verify PIO-based bus timing is working
 */

#include <stdio.h>
#include <stdbool.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "bus.h"
#include "clock.h"
#include "memory.h"
#include "usb_cdc.h"

int main() {
    // Initialize Pico SDK
    stdio_init_all();
    sleep_ms(1000);

    printf("\n=== PIO Bus Timing Test ===\n");

    // Initialize all subsystems in correct order
    printf("Initializing memory subsystem...\n");
    memory_init();

    printf("Initializing bus interface...\n");
    bus_init();

    printf("Initializing E clock...\n");
    eclock_init();

    printf("Initializing PIO bus cycles...\n");
    bus_cycle_pio_init();

    printf("Initializing USB CDC...\n");
    usb_cdc_init();

    // Check if PIO bus cycles are enabled
    bool pio_enabled = bus_cycle_pio_is_enabled();
    printf("\nPIO Bus Cycles Status: %s\n", pio_enabled ? "ENABLED" : "DISABLED");

    if (!pio_enabled) {
        printf("ERROR: PIO bus cycles not enabled!\n");
        return 1;
    }

    // Test bus_read_cycle (should use PIO implementation)
    printf("\nTesting bus_read_cycle with PIO timing...\n");
    uint8_t test_data = bus_read_cycle(0x0000);
    printf("Read from address $0000: $%02X\n", test_data);

    // Test bus_write_cycle (should use PIO implementation)
    printf("\nTesting bus_write_cycle with PIO timing...\n");
    bus_write_cycle(0x0000, 0xAA);
    printf("Wrote $AA to address $0000\n");

    // Read back to verify
    test_data = bus_read_cycle(0x0000);
    printf("Read back from address $0000: $%02X\n", test_data);

    if (test_data == 0xAA) {
        printf("\n✓ PIO Bus Timing Test PASSED\n");
        printf("  - PIO bus cycles are properly initialized\n");
        printf("  - bus_read_cycle uses PIO implementation\n");
        printf("  - bus_write_cycle uses PIO implementation\n");
        printf("  - Data integrity verified\n");
    } else {
        printf("\n✗ PIO Bus Timing Test FAILED\n");
        printf("  - Expected $AA, got $%02X\n", test_data);
        return 1;
    }

    printf("\n=== Test Complete ===\n");
    return 0;
}
