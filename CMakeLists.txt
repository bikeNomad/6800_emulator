cmake_minimum_required(VERSION 3.13)

# Set the PICO_SDK_PATH if not already set
if(NOT DEFINED ENV{PICO_SDK_PATH})
    set(ENV{PICO_SDK_PATH} "/Users/ned/src/Micropython/micropython/lib/pico-sdk")
endif()

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
    set(BOARD_TYPE "BOARD_PICO2")
endif()

message(STATUS "Building for board: ${BOARD_TYPE}")

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
    src/ihex_parser.c
    src/debug_spi.c
)

# Create the PIO header for clock generation
pico_generate_pio_header(mc6800_emulator ${CMAKE_CURRENT_LIST_DIR}/src/clock.pio)

# Link libraries
target_link_libraries(mc6800_emulator
    pico_stdlib
    hardware_pio
    hardware_dma
    hardware_spi
    hardware_flash
    hardware_sync
)

# Enable USB output, disable UART output
pico_enable_stdio_usb(mc6800_emulator 0)
pico_enable_stdio_uart(mc6800_emulator 1)

# Create map/bin/hex/uf2 file etc.
pico_add_extra_outputs(mc6800_emulator)

# Add compile definitions for board type
target_compile_definitions(mc6800_emulator PRIVATE
    BOARD_TYPE=${BOARD_TYPE}
)

# Add compile options for optimization and warnings
target_compile_options(mc6800_emulator PRIVATE
    -Wall
    -Wextra
    -O2
)
