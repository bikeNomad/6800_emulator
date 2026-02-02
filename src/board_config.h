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
 * Board Configuration
 * Configuration for Ned's System 7 Board (RP2350B with 48 GPIO)
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Board type definition
#define BOARD_NED_SYS7 1 // Ned's System 7 Board (RP2350B, 48 GPIO)

// Select active board (can be overridden by CMake)
#ifndef BOARD_TYPE
#define BOARD_TYPE BOARD_NED_SYS7
#endif

// Board configuration
#if BOARD_TYPE == BOARD_NED_SYS7
// Ned's System 7 Board - Full GPIO (48 pins)
// Full 16-bit address bus (64KB address space)
// A0-A15 → GPIO 8-23 (contiguous mapping)

#define BOARD_NAME          "Ned's System 7 Board"
#define ADDR_LINES          16
#define ADDR_MASK           0xFFFF   // 64KB address space
#define ADDR_SPACE_SIZE     65536    // 2^16 = 64KB addresses
#define ADDR_GPIO_MASK      0xFFFF00 // GPIO 8-23 for address bus

// Control signal pins
#define GPIO_ECLOCK         24       // E clock output (PIO, directly drives pin 37)
#define GPIO_ECLOCK_IN      31       // E clock input for external clock (SPARE_IN)
#define GPIO_VMA            25       // VMA (Valid Memory Address) output
#define GPIO_RW             26       // R/W (Read/Write) output

// SPI debug pins
#define GPIO_SPI_CS         33
#define GPIO_SPI_SCK        34
#define GPIO_SPI_MOSI       35
#define GPIO_SPI_MISO       36

// UART debug pins defined in CMakeLists.txt
// #define GPIO_UART_TX 40 // TX
// #define GPIO_UART_RX 41 // RX

// LED indicators (active low)
#define GPIO_LED_ROM        37 // (GREEN) Indicates ROM access
#define GPIO_LED_RAM        38 // (RED) Indicates RAM/CMOS access
#define GPIO_LED_UNMAPPED   39 // (YELLOW) Indicates unmapped/bus access

// PSRAM chip select (shared QSPI interface)
#define GPIO_PSRAM_CS       47 // PSRAM chip select

// Test pin for timing diagnostics (available on NED_SYS7 only)
// Must be <32 for PIO use
#define GPIO_TIMING_TEST    30 // Test pin for timing measurements
#define TIMING_TEST_ENABLED 1

#else
#error "Invalid BOARD_TYPE defined. Must be BOARD_NED_SYS7"
#endif

// Common GPIO pin definitions (same across all boards)
#define GPIO_DATA_BASE 0 // GPIO 0-7: Data bus (bi-directional)
#define GPIO_ADDR_BASE 8 // GPIO 8+: Address bus (starts at GPIO 8)

// Interrupt/Control inputs (same on all boards)
#define GPIO_IRQ   27 // /IRQ input (active low)
#define GPIO_NMI   28 // /NMI input (active low)
#define GPIO_RESET 29 // /RESET input (active low)

// UART debug pins (defined by CMake based on board type)
#ifndef PICO_DEFAULT_UART_TX_PIN
#error "PICO_DEFAULT_UART_TX_PIN not defined by CMake"
#endif
#ifndef PICO_DEFAULT_UART_RX_PIN
#error "PICO_DEFAULT_UART_RX_PIN not defined by CMake"
#endif
#define GPIO_UART_TX PICO_DEFAULT_UART_TX_PIN
#define GPIO_UART_RX PICO_DEFAULT_UART_RX_PIN

// Address bus limits
#define ADDR_LINE_COUNT ADDR_LINES
#define MAX_ADDRESS     ((1u << ADDR_LINES) - 1)

// PIO configuration for bus_cycle.pio, bus.c, clock.pio, clock.c
// PIO state machines for bus cycles (SM0=eclock, SM1=read/write, SM2=sync)
#define ECLK_PIO pio0
#define E_SM     0 // E clock generation
#define SYNC_SM  1

#define BUS_PIO  pio1
#define CYCLE_SM 0 // read/write cycle

#endif // BOARD_CONFIG_H
