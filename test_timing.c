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
 * Test program to verify dynamic timing calculation
 * This demonstrates that timing stays correct when SYS_CLOCK_MHZ changes
 */

#include <stdio.h>
#include <stdint.h>

// Mock clock_get_hz function for testing
static uint32_t mock_sys_clock_hz = 266000000; // Default 266MHz

uint32_t clock_get_hz(uint8_t clock_id) {
    return mock_sys_clock_hz;
}

// Include our timing calculation
#include "src/bus_timing.h"

void test_timing_at_different_clocks(void) {
    printf("Testing dynamic timing calculation:\n\n");

    // Test at different system clock frequencies
    uint32_t test_clocks[] = {
        125000000,  // 125 MHz
        200000000,  // 200 MHz
        266000000,  // 266 MHz (default)
        300000000   // 300 MHz
    };

    for (int i = 0; i < 4; i++) {
        mock_sys_clock_hz = test_clocks[i];

        printf("System Clock: %lu MHz\n", test_clocks[i] / 1000000);

        uint32_t setup_cycles = calculate_pio_cycles_for_ns(BUS_DATA_SETUP_TIME_NS);
        uint32_t hold_cycles = calculate_pio_cycles_for_ns(BUS_DATA_HOLD_TIME_NS);

        float cycle_time_ns = 1000.0f / (test_clocks[i] / 1000000.0f);
        float actual_setup_ns = setup_cycles * cycle_time_ns;
        float actual_hold_ns = hold_cycles * cycle_time_ns;

        printf("  Cycle Time: %.3f ns\n", cycle_time_ns);
        printf("  Setup Delay: %lu cycles = %.1f ns (target: %d ns)\n",
               (unsigned long)setup_cycles, actual_setup_ns, BUS_DATA_SETUP_TIME_NS);
        printf("  Hold Delay:  %lu cycles = %.1f ns (target: %d ns)\n",
               (unsigned long)hold_cycles, actual_hold_ns, BUS_DATA_HOLD_TIME_NS);

        // Verify timing is within acceptable range
        float tolerance = 5.0f; // 5ns tolerance
        if (actual_setup_ns >= (BUS_DATA_SETUP_TIME_NS - tolerance) &&
            actual_setup_ns <= (BUS_DATA_SETUP_TIME_NS + tolerance)) {
            printf("  ✓ Setup timing OK\n");
        } else {
            printf("  ✗ Setup timing FAIL\n");
        }

        if (actual_hold_ns >= (BUS_DATA_HOLD_TIME_NS - tolerance) &&
            actual_hold_ns <= (BUS_DATA_HOLD_TIME_NS + tolerance)) {
            printf("  ✓ Hold timing OK\n");
        } else {
            printf("  ✗ Hold timing FAIL\n");
        }

        printf("\n");
    }
}

int main(void) {
    test_timing_at_different_clocks();
    return 0;
}
