/**
 * Board Configuration
 * Supports multiple RP2350 development boards with different GPIO counts
 */

#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H

// Board type definitions
#define BOARD_PICO2       1  // Raspberry Pi Pico 2 (RP2350A, 26 GPIO)
#define BOARD_NED_SYS7    2  // Ned's System 7 Board (RP2350, 48 GPIO)

// Select active board (can be overridden by CMake)
#ifndef BOARD_TYPE
  #define BOARD_TYPE BOARD_PICO2  // Default to Pico 2 for initial testing
#endif

// Board-specific configurations
#if BOARD_TYPE == BOARD_PICO2
  // Raspberry Pi Pico 2 W - Limited GPIO (26 pins)
  // Address bus uses only A0, A1, A10-A14 (7 pins for PIA access)
  // GPIO 16-17 reserved for UART debug

  #define BOARD_NAME "Raspberry Pi Pico 2 W"
  #define ADDR_LINES 7
  #define ADDR_MASK 0x7C03  // Bits 0,1,10-14: 0b0111_1100_0000_0011
  #define ADDR_SPACE_SIZE 128  // 2^7 = 128 addresses
  #define ADDR_GPIO_MASK 0x7F00  // GPIO 8-14 for address bus

  // Control signal pins (adjusted for fewer GPIOs)
  #define GPIO_VMA 21
  #define GPIO_RW 22

  // SPI debug pins
  #define GPIO_SPI_SCK 18
  #define GPIO_SPI_MOSI 19

#elif BOARD_TYPE == BOARD_NED_SYS7
  // Ned's System 7 Board - Full GPIO (48 pins)
  // Full 16-bit address bus (64KB address space)
  // A0-A15 → GPIO 8-23 (contiguous mapping)

  #define BOARD_NAME "Ned's System 7 Board"
  #define ADDR_LINES 16
  #define ADDR_MASK 0xFFFF  // 64KB address space
  #define ADDR_SPACE_SIZE 65536  // 2^16 = 64KB addresses
  #define ADDR_GPIO_MASK 0xFFFF00  // GPIO 8-23 for address bus

  // Control signal pins
  #define GPIO_VMA 24
  #define GPIO_RW 25

  // SPI debug pins
  #define GPIO_SPI_SCK 30
  #define GPIO_SPI_MOSI 31

  // LED indicators (active low)
  #define GPIO_LED_ROM 34      // Indicates ROM access
  #define GPIO_LED_RAM 35      // Indicates RAM/CMOS access
  #define GPIO_LED_UNMAPPED 36 // Indicates unmapped/bus access

#else
  #error "Invalid BOARD_TYPE defined. Must be BOARD_PICO2 or BOARD_NED_SYS7"
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

// UART debug pins (defined by CMake based on board type)
// BOARD_PICO2: GPIO 16-17 (available)
// BOARD_WAVESHARE: GPIO 32-33 (GPIO 16-17 used by address bus)
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
#define MAX_ADDRESS ((1u << ADDR_LINES) - 1)

#endif // BOARD_CONFIG_H
