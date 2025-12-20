cmake_minimum_required(VERSION 3.13)

# Set the PICO_SDK_PATH if not already set
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} "/Users/ned/src/Micropython/micropython/lib/pico-sdk")
endif()

# Set board type to Pico 2 (RP2350) - MUST be before pico_sdk_import
set(PICO_BOARD pico2)

# Include the Pico SDK
include($ENV{PICO_SDK_PATH}/external/pico_sdk_import.cmake)

project(mc6800_emulator C CXX ASM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)

# Initialize the Pico SDK
pico_sdk_init()

# Board configuration (can be overridden from command line)
# Options: BOARD_PICO2 (default) or BOARD_WAVESHARE
if(NOT DEFINED BOARD_TYPE)
    set(BOARD_TYPE "BOARD_NED_SYS7")
endif()

message(STATUS "Building for board: ${BOARD_TYPE}")
message(STATUS "System clock: ${SYS_CLOCK_MHZ} MHz")

# Add executable
add_executable(mc6800_emulator
    src/main.c
    src/cpu_state.c
    src/memory.c
    src/bus.c
    src/clock.c
    src/instructions.c
    src/interrupts.c
    src/usb_cdc.c
    src/usb_descriptors.c
    src/ihex_parser.c
    src/debug_spi.c
    src/cycle_test.c
)

# Create the PIO headers for clock generation and bus cycles
pico_generate_pio_header(mc6800_emulator ${CMAKE_CURRENT_LIST_DIR}/src/clock.pio)
pico_generate_pio_header(mc6800_emulator ${CMAKE_CURRENT_LIST_DIR}/src/bus_cycle.pio)

# Add include directory for tusb_config.h
target_include_directories(mc6800_emulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(mc6800_emulator
    pico_stdlib
    pico_unique_id
    pico_multicore
    hardware_pio
    hardware_dma
    hardware_spi
    hardware_flash
    hardware_sync
    tinyusb_device
    tinyusb_board
)

# Enable UART for debug output (board-specific GPIO pins)
pico_enable_stdio_usb(mc6800_emulator 0)
pico_enable_stdio_uart(mc6800_emulator 1)

# Set UART pins based on board type (defined in board_config.h)
if(BOARD_TYPE STREQUAL "BOARD_PICO2")
    set(UART_TX_PIN 24)
    set(UART_RX_PIN 25)
    set(UART_INSTANCE 1)  # UART1 for Pico 2
elseif(BOARD_TYPE STREQUAL "BOARD_NED_SYS7")
    set(UART_TX_PIN 40)
    set(UART_RX_PIN 41)
    set(UART_INSTANCE 1)  # UART1 for NED_SYS7
else()
    message(FATAL_ERROR "Unknown BOARD_TYPE: ${BOARD_TYPE}")
endif()

message(STATUS "UART configuration: UART${UART_INSTANCE} on TX=GPIO${UART_TX_PIN}, RX=GPIO${UART_RX_PIN}")

# Create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(mc6800_emulator)

# System clock configuration (can be overridden from command line)
# Options: 150-300 MHz (safe range for RP2350 with proper voltage)
if(NOT DEFINED SYS_CLOCK_MHZ)
    set(SYS_CLOCK_MHZ 266)  # Default: 266MHz (optimized for 133MHz QSPI flash)
endif()

# QSPI clock divisor configuration (can be overridden from command line)
# Options: 1 (full speed), 2 (half speed), 3, 4, etc.
if(NOT DEFINED QSPI_CLOCK_DIVISOR)
    set(QSPI_CLOCK_DIVISOR 2)  # Default: system clock / 2 (133MHz with 266MHz sys clock)
endif()

message(STATUS "QSPI clock divisor: ${QSPI_CLOCK_DIVISOR}")

# Add compile definitions for board type and UART pins
target_compile_definitions(mc6800_emulator PRIVATE
    BOARD_TYPE=${BOARD_TYPE}
    PICO_DEFAULT_UART=${UART_INSTANCE}
    PICO_DEFAULT_UART_TX_PIN=${UART_TX_PIN}
    PICO_DEFAULT_UART_RX_PIN=${UART_RX_PIN}
    SYS_CLOCK_MHZ=${SYS_CLOCK_MHZ}
    QSPI_CLOCK_DIVISOR=${QSPI_CLOCK_DIVISOR}
    DEBUG_INTERRUPTS=0  # Set to 0 to disable interrupt debug output
)

# Add compile options for optimization and warnings
target_compile_options(mc6800_emulator PRIVATE
    -Wall
    -Wextra
    -Og
)
