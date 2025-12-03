/**
 * Board Configuration
 * Supports multiple RP2350 development boards with different GPIO counts
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Board type definitions
#define BOARD_PICO2       1  // Raspberry Pi Pico 2 (RP2350A, 26 GPIO)
#define BOARD_WAVESHARE   2  // Waveshare RP2350B-Plus-W (RP2350B, 48 GPIO)

// Select active board (can be overridden by CMake)
#ifndef BOARD_TYPE
  #define BOARD_TYPE BOARD_PICO2  // Default to Pico 2 for initial testing
#endif

// Board-specific configurations
#if BOARD_TYPE == BOARD_PICO2
  // Raspberry Pi Pico 2 - Limited GPIO (26 pins)
  // Address bus reduced to 13 bits (8KB address space)

  #define BOARD_NAME "Raspberry Pi Pico 2"
  #define ADDR_LINES 13
  #define ADDR_MASK 0x1FFF  // 8KB address space

  // Control signal pins (adjusted for fewer GPIOs)
  #define GPIO_VMA 21
  #define GPIO_RW 22

  // SPI debug pins
  #define GPIO_SPI_SCK 18
  #define GPIO_SPI_MOSI 19

#elif BOARD_TYPE == BOARD_WAVESHARE
  // Waveshare RP2350B-Plus-W - Full GPIO (48 pins)
  // Full 16-bit address bus (64KB address space)

  #define BOARD_NAME "Waveshare RP2350B-Plus-W"
  #define ADDR_LINES 16
  #define ADDR_MASK 0xFFFF  // 64KB address space

  // Control signal pins
  #define GPIO_VMA 24
  #define GPIO_RW 25

  // SPI debug pins
  #define GPIO_SPI_SCK 30
  #define GPIO_SPI_MOSI 31

#else
  #error "Invalid BOARD_TYPE defined. Must be BOARD_PICO2 or BOARD_WAVESHARE"
#endif

// Common GPIO pin definitions (same across all boards)
#define GPIO_DATA_BASE  0   // GPIO 0-7: Data bus (bi-directional)
#define GPIO_ADDR_BASE  8   // GPIO 8+: Address bus (starts at GPIO 8)

// Interrupt/Control inputs (same on all boards)
#define GPIO_IRQ    26  // /IRQ input (active low)
#define GPIO_NMI    27  // /NMI input (active low)
#define GPIO_RESET  28  // /RESET input (active low)

// E clock output
#define GPIO_ECLOCK 29  // E clock output (PIO)

// Address bus limits
#define ADDR_LINE_COUNT ADDR_LINES
#define MAX_ADDRESS ((1u << ADDR_LINES) - 1)

#endif // BOARD_CONFIG_H
