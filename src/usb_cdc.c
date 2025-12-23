/**
 * USB CDC Interface Implementation
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "usb_cdc.h"
#include "cpu_state.h"
#include "memory.h"
#include "bus.h"
#include "ihex_parser.h"
#include "interrupts.h"
#include "clock.h"
#include "debug_spi.h"
#include "pico/stdlib.h"
#include "pico/bootrom.h"
#include "hardware/clocks.h"
#include "tusb.h"
#include "instructions.h"

// Command buffer
#define CMD_BUFFER_SIZE 4096
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint32_t cmd_pos = 0;
static bool in_hex_mode = false;

// HEX data buffer
#define HEX_BUFFER_SIZE 32768
static char hex_buffer[HEX_BUFFER_SIZE];
static uint32_t hex_pos = 0;

// Command tokenization
#define MAX_TOKENS 32
static char *cmd_tokens[MAX_TOKENS];
static int cmd_token_count = 0;

// Command handler function type
typedef void (*cmd_handler_fn)(void);

// Command table entry
typedef struct {
    const char *name;
    cmd_handler_fn handler;
} command_entry_t;

// Helper functions for bus operations with E clock management
static void bus_read_block_with_eclock(uint16_t address, uint16_t length, uint8_t *buffer) {
    // Temporarily start E clock if CPU is halted
    bool was_running = cpu_is_running();
    if (!was_running) {
        eclock_start();
    }

    // Read block of data
    for (uint16_t i = 0; i < length; i++) {
        buffer[i] = bus_read_cycle(address + i);
        busy_wait_us(3);
    }

    // Stop E clock if we started it
    if (!was_running) {
        eclock_stop();
    }
}

static void bus_write_block_with_eclock(uint16_t address, const uint8_t *buffer, uint16_t length) {
    // Temporarily start E clock if CPU is halted
    bool was_running = cpu_is_running();
    if (!was_running) {
        eclock_start();
    }

    // Write block of data
    for (uint16_t i = 0; i < length; i++) {
        bus_write_cycle(address + i, buffer[i]);
        busy_wait_us(3);
    }

    // Stop E clock if we started it
    if (!was_running) {
        eclock_stop();
    }
}

static uint8_t bus_read_with_eclock(uint16_t address) {
    // Implement single-byte read using block version for consistency
    uint8_t buffer[1];
    bus_read_block_with_eclock(address, 1, buffer);
    return buffer[0];
}

static void bus_write_with_eclock(uint16_t address, uint8_t value) {
    // Implement single-byte write using block version for consistency
    bus_write_block_with_eclock(address, &value, 1);
}

//--------------------------------------------------------------------+
// Command Handler Functions
//--------------------------------------------------------------------+

static void cmd_load(void) {
    // Enter HEX load mode
    in_hex_mode = true;
    hex_pos = 0;
    usb_cdc_send("Ready to receive Intel HEX data. Paste file now...\r\n");
}

static void cmd_end(void) {
    // Exit HEX load mode and process data
    if (in_hex_mode && hex_pos > 0) {
        hex_buffer[hex_pos] = '\0';
        usb_cdc_send("Processing HEX data...\r\n");

        if (ihex_load_data(hex_buffer, hex_pos)) {
            usb_cdc_send("OK: EPROM loaded successfully\r\n");
        } else {
            usb_cdc_send("ERROR: Failed to load EPROM\r\n");
        }
    }
    in_hex_mode = false;
    hex_pos = 0;
}

static void cmd_config_show(void) {
    // Display current memory configuration
    uint16_t rom_base, rom_size, ram_base, ram_size;
    memory_get_rom_config(&rom_base, &rom_size);
    memory_get_ram_config(&ram_base, &ram_size);

    usb_cdc_send("Memory Configuration:\r\n");
    usb_cdc_printf("  ROM: $%04X-$%04X (%d bytes, %dKB)\r\n",
                   rom_base, rom_base + rom_size - 1,
                   rom_size, rom_size / 1024);
    usb_cdc_printf("  RAM: $%04X-$%04X (%d bytes, %dKB)\r\n",
                   ram_base, ram_base + ram_size - 1,
                   ram_size, ram_size / 1024);
    usb_cdc_send("  RAM mirroring: $0000-$00FF <-> $1000-$10FF\r\n");
    usb_cdc_printf("  Debug SPI: %s\r\n", debug_spi_is_enabled() ? "ON" : "OFF");
}

static void cmd_config_rom(void) {
    // Configure ROM region: config rom <base> <size>
    // Expects tokens: [config] [rom] <base> <size>
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: config rom <base_hex> <size_hex>\r\n");
        return;
    }
    
    unsigned int base, size;
    if (sscanf(cmd_tokens[0], "%x", &base) == 1 && sscanf(cmd_tokens[1], "%x", &size) == 1) {
        memory_configure_rom(base, size);
        usb_cdc_printf("OK: ROM configured at $%04X, size $%04X\r\n", base, size);
    } else {
        usb_cdc_send("ERROR: Usage: config rom <base_hex> <size_hex>\r\n");
    }
}

static void cmd_config_ram(void) {
    // Configure RAM region: config ram <base> <size>
    // Expects tokens: [config] [ram] <base> <size>
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: config ram <base_hex> <size_hex>\r\n");
        return;
    }
    
    unsigned int base, size;
    if (sscanf(cmd_tokens[0], "%x", &base) == 1 && sscanf(cmd_tokens[1], "%x", &size) == 1) {
        memory_configure_ram(base, size);
        usb_cdc_printf("OK: RAM configured at $%04X, size $%04X\r\n", base, size);
    } else {
        usb_cdc_send("ERROR: Usage: config ram <base_hex> <size_hex>\r\n");
    }
}

static void cmd_cmos_save(void) {
    // Manually save CMOS to flash
    if (memory_save_cmos()) {
        usb_cdc_send("OK: CMOS saved to flash\r\n");
    } else {
        usb_cdc_send("ERROR: Failed to save CMOS\r\n");
    }
}

static void cmd_cmos_dump(void) {
    // Display CMOS RAM contents
    const uint8_t *cmos = memory_get_cmos_shadow();
    usb_cdc_send("CMOS RAM ($0100-$01FF):\r\n");
    for (uint16_t i = 0; i < 256; i++) {
        if (i % 16 == 0) {
            usb_cdc_printf("%04X: ", 0x0100 + i);
        }
        usb_cdc_printf("%02X ", cmos[i]);
        if (i % 16 == 15) {
            usb_cdc_send("\r\n");
        }
    }
}

static void cmd_cmos_autosave(void) {
    // Enable/disable CMOS autosave
    // Expects tokens: [cmos] [autosave] <on|off>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: cmos autosave on/off\r\n");
        return;
    }
    
    if (strcmp(cmd_tokens[0], "on") == 0) {
        memory_set_cmos_autosave_enabled(true);
        usb_cdc_send("OK: CMOS autosave enabled\r\n");
    } else if (strcmp(cmd_tokens[0], "off") == 0) {
        memory_set_cmos_autosave_enabled(false);
        usb_cdc_send("OK: CMOS autosave disabled\r\n");
    } else {
        usb_cdc_send("ERROR: Usage: cmos autosave on/off\r\n");
    }
}

static void cmd_run(void) {
    // Start CPU execution
    cpu_start();
    usb_cdc_send("OK: CPU started\r\n");
}

static void cmd_halt(void) {
    // Stop CPU execution and save CMOS
    cpu_halt();
    memory_save_cmos();
    usb_cdc_send("OK: CPU halted, CMOS saved\r\n");
}

static void cmd_reset(void) {
    // Reset CPU (CMOS will be auto-saved by background task if needed)
    interrupt_service_reset();
    usb_cdc_send("OK: CPU reset\r\n");
}

static void cmd_bootloader(void) {
    // Enter bootloader mode
    usb_cdc_send("Entering bootloader mode...\r\n");
    sleep_ms(100);  // Give time for message to send
    reset_usb_boot(0, 0);  // Reset into USB bootloader
}

static void cmd_debug_on(void) {
    // Enable debug SPI output
    debug_spi_enable(true);
    usb_cdc_send("OK: Debug SPI enabled\r\n");
}

static void cmd_debug_off(void) {
    // Disable debug SPI output
    debug_spi_enable(false);
    usb_cdc_send("OK: Debug SPI disabled\r\n");
}

static void cmd_bus_info(void) {
    // Bus information
    usb_cdc_printf("Board: %s\r\n", BOARD_NAME);
    usb_cdc_printf("Address Lines: %d\r\n", ADDR_LINES);
    usb_cdc_printf("Address Mask: 0x%04X\r\n", ADDR_MASK);
    usb_cdc_printf("Max Address: 0x%04X\r\n", MAX_ADDRESS);
    usb_cdc_printf("Address Space: %d bytes\r\n", ADDR_SPACE_SIZE);
    usb_cdc_send("Bus Interface: Cycle-accurate E-clock synchronized\r\n");
}

static void cmd_status(void) {
    // Get CPU status
    usb_cdc_printf("CPU Status:\r\n");
    usb_cdc_printf("  PC: $%04X  (%s)\r\n",
                    cpu.pc,
                    instruction_get_mnemonic(memory_read_rom_shadow(cpu.pc)));
    usb_cdc_printf("  A:  $%02X\r\n", cpu.a);
    usb_cdc_printf("  B:  $%02X\r\n", cpu.b);
    usb_cdc_printf("  X:  $%04X\r\n", cpu.x);
    usb_cdc_printf("  SP: $%04X\r\n", cpu.sp);
    usb_cdc_printf("  CCR: $%02X [%c%c%c%c%c%c]\r\n",
                   cpu.ccr,
                   cpu_get_flag(CCR_H) ? 'H' : '-',
                   cpu_get_flag(CCR_I) ? 'I' : '-',
                   cpu_get_flag(CCR_N) ? 'N' : '-',
                   cpu_get_flag(CCR_Z) ? 'Z' : '-',
                   cpu_get_flag(CCR_V) ? 'V' : '-',
                   cpu_get_flag(CCR_C) ? 'C' : '-');
    usb_cdc_printf("  Running: %s\r\n", cpu_is_running() ? "YES" : "NO");
    usb_cdc_printf("  Halted: %s\r\n", cpu.halted ? "YES" : "NO");
    usb_cdc_printf("  Instructions: %llu\r\n", (unsigned long long)cpu.instruction_count);
    usb_cdc_printf("  Cycle Count: %lu\r\n", (unsigned long)eclock_get_count());
    usb_cdc_printf("  PIO Cycles: %lu\r\n", (unsigned long)eclock_get_pio_cycles());
    usb_cdc_printf("  Overage: %ld\r\n", (long)cycle_overage);

    // Calculate and display speed ratio
    uint32_t cycle_cnt = eclock_get_count();
    uint32_t pio_cnt = eclock_get_pio_cycles();
    if (pio_cnt > 0) {
        float speed_ratio = (float)cycle_cnt / (float)pio_cnt;
        usb_cdc_printf("  Speed: %.2fx real-time\r\n", speed_ratio);
    }

    // Include QSPI information
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);
    uint32_t qspi_freq_hz = sys_clock_hz / QSPI_CLOCK_DIVISOR;
    usb_cdc_printf("  QSPI Bus: %lu MHz (divisor: %d)\r\n", qspi_freq_hz / 1000000, QSPI_CLOCK_DIVISOR);
}

static void cmd_read(void) {
    // Read memory: read <addr> <len>
    // Expects tokens: [read] <addr> <len>
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: read <addr_hex> <len_hex>\r\n");
        return;
    }

    unsigned int addr, len;
    if (sscanf(cmd_tokens[0], "%x", &addr) != 1 || sscanf(cmd_tokens[1], "%x", &len) != 1) {
        usb_cdc_send("ERROR: Usage: read <addr_hex> <len_hex>\r\n");
        return;
    }

    if (addr > MAX_ADDRESS) {
        usb_cdc_send("ERROR: Address out of range\r\n");
    } else if (len == 0 || len > 1024) {
        usb_cdc_send("ERROR: Length must be 1-1024\r\n");
    } else if (addr + len > MAX_ADDRESS + 1) {
        usb_cdc_send("ERROR: Block exceeds address space\r\n");
    } else {
        usb_cdc_printf("Reading $%04X bytes from $%04X:\r\n", len, addr);

        // Check if any addresses in range are unmapped (require bus access)
        bool has_unmapped = false;
        for (uint32_t i = 0; i < len; i++) {
            if (memory_get_type((uint16_t)(addr + i)) == MEM_TYPE_UNMAPPED) {
                has_unmapped = true;
                break;
            }
        }

        if (has_unmapped) {
            // Use bus access with E clock management for unmapped addresses
            uint8_t buffer[1024];  // Max block size
            bus_read_block_with_eclock((uint16_t)addr, (uint16_t)len, buffer);
            for (uint32_t i = 0; i < len; i++) {
                if (i % 16 == 0) {
                    usb_cdc_printf("%04X: ", addr + i);
                }
                usb_cdc_printf("%02X ", buffer[i]);
                if (i % 16 == 15 || i == len - 1) {
                    usb_cdc_send("\r\n");
                }
            }
        } else {
            // Fast path for mapped memory only
            for (uint32_t i = 0; i < len; i++) {
                if (i % 16 == 0) {
                    usb_cdc_printf("%04X: ", addr + i);
                }
                uint8_t value = memory_read_fast(addr + i);
                usb_cdc_printf("%02X ", value);
                if (i % 16 == 15 || i == len - 1) {
                    usb_cdc_send("\r\n");
                }
            }
        }
    }
}

static void cmd_write(void) {
    // Write memory: write <addr> <data...>
    // Expects tokens: [write] <addr> <data1> <data2> ...
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: write <addr_hex> <byte_hex> ...\r\n");
        return;
    }

    unsigned int addr;
    if (sscanf(cmd_tokens[0], "%x", &addr) != 1) {
        usb_cdc_send("ERROR: Usage: write <addr_hex> <byte_hex> ...\r\n");
        return;
    }

    if (addr > MAX_ADDRESS) {
        usb_cdc_send("ERROR: Address out of range\r\n");
        return;
    }

    // Parse data bytes from remaining tokens
    uint8_t buffer[1024];  // Max block size
    uint32_t count = 0;
    for (int i = 1; i < cmd_token_count && count < 1024; i++) {
        unsigned int value;
        if (sscanf(cmd_tokens[i], "%x", &value) == 1 && value <= 255) {
            if (addr + count > MAX_ADDRESS) {
                usb_cdc_send("ERROR: Block exceeds address space\r\n");
                return;
            }
            buffer[count++] = (uint8_t)value;
        } else {
            usb_cdc_send("ERROR: Invalid hex data\r\n");
            return;
        }
    }

    if (count == 0) {
        usb_cdc_send("ERROR: No data provided\r\n");
        return;
    }

    // Check if any addresses in range are unmapped (require bus access)
    bool has_unmapped = false;
    for (uint32_t i = 0; i < count; i++) {
        if (memory_get_type((uint16_t)(addr + i)) == MEM_TYPE_UNMAPPED) {
            has_unmapped = true;
            break;
        }
    }

    if (has_unmapped) {
        // Use bus access with E clock management for unmapped addresses
        bus_write_block_with_eclock((uint16_t)addr, buffer, (uint16_t)count);
    } else {
        // Fast path for mapped memory only
        for (uint32_t i = 0; i < count; i++) {
            memory_write_fast(addr + i, buffer[i]);
        }
    }
    usb_cdc_send("OK\r\n");
}

static void cmd_break_set(void) {
    // Set breakpoint: break <addr>
    // Expects tokens: [break] <addr>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: break <addr_hex>\r\n");
        return;
    }

    unsigned int addr;
    if (sscanf(cmd_tokens[0], "%x", &addr) == 1) {
        if (cpu_add_breakpoint((uint16_t)addr)) {
            usb_cdc_printf("OK: Breakpoint set at $%04X\r\n", addr);
        } else {
            usb_cdc_printf("ERROR: Failed to set breakpoint at $%04X (max %d breakpoints)\r\n", addr, MAX_BREAKPOINTS);
        }
    } else {
        usb_cdc_send("ERROR: Usage: break <addr_hex>\r\n");
    }
}

static void cmd_break_clear(void) {
    // Clear breakpoint(s): break clear [<addr>]
    // Expects tokens: [break] [clear] [<addr>]
    if (cmd_token_count == 0) {
        // Clear all breakpoints
        cpu_clear_breakpoints();
        usb_cdc_send("OK: All breakpoints cleared\r\n");
    } else {
        // Clear specific breakpoint
        unsigned int addr;
        if (sscanf(cmd_tokens[0], "%x", &addr) == 1) {
            if (cpu_remove_breakpoint((uint16_t)addr)) {
                usb_cdc_printf("OK: Breakpoint at $%04X cleared\r\n", addr);
            } else {
                usb_cdc_printf("ERROR: No breakpoint at $%04X\r\n", addr);
            }
        } else {
            usb_cdc_send("ERROR: Usage: break clear <addr_hex>\r\n");
        }
    }
}

static void cmd_break_list(void) {
    // List all breakpoints
    uint8_t count = cpu_get_breakpoint_count();
    if (count == 0) {
        usb_cdc_send("No breakpoints set\r\n");
    } else {
        usb_cdc_send("Breakpoints:\r\n");
        const uint16_t* breakpoints = cpu_get_breakpoints();
        for (uint8_t i = 0; i < count; i++) {
            usb_cdc_printf("  $%04X\r\n", breakpoints[i]);
        }
    }
}

static void cmd_reg_pc(void) {
    // Set program counter: reg pc <val>
    // Expects tokens: [reg] [pc] <val>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: reg pc <value_hex>\r\n");
        return;
    }

    unsigned int value;
    if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
        cpu.pc = (uint16_t)value;
        usb_cdc_printf("OK: PC set to $%04X\r\n", cpu.pc);
    } else {
        usb_cdc_send("ERROR: Invalid register value\r\n");
    }
}

static void cmd_reg_a(void) {
    // Set accumulator A: reg a <val>
    // Expects tokens: [reg] [a] <val>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: reg a <value_hex>\r\n");
        return;
    }

    unsigned int value;
    if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
        cpu.a = (uint8_t)value;
        usb_cdc_printf("OK: A set to $%02X\r\n", cpu.a);
    } else {
        usb_cdc_send("ERROR: Invalid register value\r\n");
    }
}

static void cmd_reg_b(void) {
    // Set accumulator B: reg b <val>
    // Expects tokens: [reg] [b] <val>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: reg b <value_hex>\r\n");
        return;
    }

    unsigned int value;
    if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
        cpu.b = (uint8_t)value;
        usb_cdc_printf("OK: B set to $%02X\r\n", cpu.b);
    } else {
        usb_cdc_send("ERROR: Invalid register value\r\n");
    }
}

static void cmd_reg_x(void) {
    // Set index register X: reg x <val>
    // Expects tokens: [reg] [x] <val>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: reg x <value_hex>\r\n");
        return;
    }

    unsigned int value;
    if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
        cpu.x = (uint16_t)value;
        usb_cdc_printf("OK: X set to $%04X\r\n", cpu.x);
    } else {
        usb_cdc_send("ERROR: Invalid register value\r\n");
    }
}

static void cmd_reg_sp(void) {
    // Set stack pointer: reg sp <val>
    // Expects tokens: [reg] [sp] <val>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: reg sp <value_hex>\r\n");
        return;
    }

    unsigned int value;
    if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
        cpu.sp = (uint16_t)value;
        usb_cdc_printf("OK: SP set to $%04X\r\n", cpu.sp);
    } else {
        usb_cdc_send("ERROR: Invalid register value\r\n");
    }
}

static void cmd_reg_ccr(void) {
    // Set condition code register: reg ccr <val>
    // Expects tokens: [reg] [ccr] <val>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: reg ccr <value_hex>\r\n");
        return;
    }

    unsigned int value;
    if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
        cpu.ccr = ((uint8_t)value & 0x3F) | CCR_FIXED;  // Preserve bits 7-6
        usb_cdc_printf("OK: CCR set to $%02X\r\n", cpu.ccr);
    } else {
        usb_cdc_send("ERROR: Invalid register value\r\n");
    }
}

static void cmd_bus_read(void) {
    // Bus read: bus_read <address>
    // Expects tokens: [bus_read] <address>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: bus_read <address_hex>\r\n");
        return;
    }

    unsigned int address;
    if (sscanf(cmd_tokens[0], "%x", &address) == 1) {
        if (address > MAX_ADDRESS) {
            usb_cdc_send("ERROR: Address out of range\r\n");
        } else {
            uint8_t data = bus_read_with_eclock((uint16_t)address);
            usb_cdc_printf("%02X\r\n", data);
        }
    } else {
        usb_cdc_send("ERROR: Usage: bus_read <address_hex>\r\n");
    }
}

static void cmd_bus_write(void) {
    // Bus write: bus_write <address> <data>
    // Expects tokens: [bus_write] <address> <data>
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: bus_write <address_hex> <data_hex>\r\n");
        return;
    }

    unsigned int address, data;
    if (sscanf(cmd_tokens[0], "%x", &address) != 1 || sscanf(cmd_tokens[1], "%x", &data) != 1) {
        usb_cdc_send("ERROR: Usage: bus_write <address_hex> <data_hex>\r\n");
        return;
    }

    if (address > MAX_ADDRESS) {
        usb_cdc_send("ERROR: Address out of range\r\n");
    } else if (data > 255) {
        usb_cdc_send("ERROR: Data must be 0-255\r\n");
    } else {
        bus_write_with_eclock((uint16_t)address, (uint8_t)data);
        usb_cdc_send("OK\r\n");
    }
}

static void cmd_bus_read_block(void) {
    // Bus read block: bus_read_block <address> <length>
    // Expects tokens: [bus_read_block] <address> <length>
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: bus_read_block <address_hex> <length_hex>\r\n");
        return;
    }

    unsigned int address, length;
    if (sscanf(cmd_tokens[0], "%x", &address) != 1 || sscanf(cmd_tokens[1], "%x", &length) != 1) {
        usb_cdc_send("ERROR: Usage: bus_read_block <address_hex> <length_hex>\r\n");
        return;
    }

    if (address > MAX_ADDRESS) {
        usb_cdc_send("ERROR: Address out of range\r\n");
    } else if (length == 0 || length > 1024) {
        usb_cdc_send("ERROR: Length must be 1-1024\r\n");
    } else if (address + length > MAX_ADDRESS + 1) {
        usb_cdc_send("ERROR: Block exceeds address space\r\n");
    } else {
        // Read block and send data as space-separated hex bytes
        uint8_t buffer[1024];  // Max block size
        bus_read_block_with_eclock((uint16_t)address, (uint16_t)length, buffer);
        for (uint32_t i = 0; i < length; i++) {
            if (i > 0) usb_cdc_send(" ");
            usb_cdc_printf("%02X", buffer[i]);
        }
        usb_cdc_send("\r\n");
    }
}

static void cmd_bus_write_block(void) {
    // Bus write block: bus_write_block <address> <hex_data...>
    // Expects tokens: [bus_write_block] <address> <data1> <data2> ...
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: bus_write_block <address_hex> <byte_hex> ...\r\n");
        return;
    }

    unsigned int address;
    if (sscanf(cmd_tokens[0], "%x", &address) != 1) {
        usb_cdc_send("ERROR: Usage: bus_write_block <address_hex> <byte_hex> ...\r\n");
        return;
    }

    if (address > MAX_ADDRESS) {
        usb_cdc_send("ERROR: Address out of range\r\n");
        return;
    }

    // Parse data bytes from remaining tokens
    uint8_t buffer[1024];  // Max block size
    uint32_t count = 0;
    for (int i = 1; i < cmd_token_count && count < 1024; i++) {
        unsigned int value;
        if (sscanf(cmd_tokens[i], "%x", &value) == 1 && value <= 255) {
            if (address + count > MAX_ADDRESS) {
                usb_cdc_send("ERROR: Block exceeds address space\r\n");
                return;
            }
            buffer[count++] = (uint8_t)value;
        } else {
            usb_cdc_send("ERROR: Invalid hex data\r\n");
            return;
        }
    }

    if (count == 0) {
        usb_cdc_send("ERROR: No data provided\r\n");
        return;
    }

    // Write block using helper function
    bus_write_block_with_eclock((uint16_t)address, buffer, (uint16_t)count);
    usb_cdc_send("OK\r\n");
}

static void cmd_help(void) {
    // Send as single string to avoid buffer overflow
    usb_cdc_send(
        "MC6800 Emulator Commands:\r\n"
        "  load                      - Load Intel HEX (auto-detects ROM/CMOS)\r\n"
        "  config                    - Show memory configuration\r\n"
        "  config rom <b> <s>        - Configure ROM region\r\n"
        "  config ram <b> <s>        - Configure RAM region\r\n"
        "  cmos save                 - Manually save CMOS to flash\r\n"
        "  cmos dump                 - Display CMOS RAM contents\r\n"
        "  cmos autosave on/off      - Enable/disable CMOS autosave\r\n"
        "  read <addr> <len>         - Read memory\r\n"
        "  write <addr> <data>       - Write memory\r\n"
        "  status                    - Display CPU status\r\n"
        "  run                       - Start CPU execution\r\n"
        "  halt                      - Stop CPU execution (auto-saves CMOS)\r\n"
        "  reset                     - Reset CPU (auto-saves CMOS)\r\n"
        "  cycletest                 - Test instruction cycle counts\r\n"
        "  debug on/off              - Enable/disable SPI debug output\r\n"
        "  break <addr>              - Set breakpoint at address\r\n"
        "  break clear               - Clear all breakpoints\r\n"
        "  break clear <addr>        - Clear specific breakpoint\r\n"
        "  break list                - List all breakpoints\r\n"
        "  reg pc <val>              - Set program counter\r\n"
        "  reg a <val>               - Set accumulator A\r\n"
        "  reg b <val>               - Set accumulator B\r\n"
        "  reg x <val>               - Set index register X\r\n"
        "  reg sp <val>              - Set stack pointer\r\n"
        "  reg ccr <val>             - Set condition code register\r\n"
        "  bus_read <addr>           - Read byte from hardware bus\r\n"
        "  bus_write <addr> <data>   - Write byte to hardware bus\r\n"
        "  bus_read_block <addr> <len> - Read block from hardware bus\r\n"
        "  bus_write_block <addr> <data...> - Write block to hardware bus\r\n"
        "  bus_info                  - Show bus configuration\r\n"
        "  bootloader                - Enter bootloader mode\r\n"
        "  help                      - Show this help\r\n"
    );
}

//--------------------------------------------------------------------+
// Command Tokenizer and Dispatcher
//--------------------------------------------------------------------+

static int tokenize_command(char *cmd) {
    // Tokenize the command line into words
    cmd_token_count = 0;
    char *token = strtok(cmd, " \t");
    
    while (token != NULL && cmd_token_count < MAX_TOKENS) {
        cmd_tokens[cmd_token_count++] = token;
        token = strtok(NULL, " \t");
    }
    
    return cmd_token_count;
}

static void dispatch_command(void) {
    // Empty command
    if (cmd_token_count == 0) {
        return;
    }

    // Build two-word command string if possible
    char two_word[64];
    if (cmd_token_count >= 2) {
        snprintf(two_word, sizeof(two_word), "%s %s", cmd_tokens[0], cmd_tokens[1]);
    }

    // Try two-word match first
    if (cmd_token_count >= 2) {
        if (strcmp(two_word, "config rom") == 0) {
            // Shift tokens by copying remaining tokens to beginning
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_config_rom();
            return;
        } else if (strcmp(two_word, "config ram") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_config_ram();
            return;
        } else if (strcmp(two_word, "cmos save") == 0) {
            cmd_cmos_save();
            return;
        } else if (strcmp(two_word, "cmos dump") == 0) {
            cmd_cmos_dump();
            return;
        } else if (strcmp(two_word, "cmos autosave") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_cmos_autosave();
            return;
        } else if (strcmp(two_word, "debug on") == 0) {
            cmd_debug_on();
            return;
        } else if (strcmp(two_word, "debug off") == 0) {
            cmd_debug_off();
            return;
        } else if (strcmp(two_word, "break clear") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_break_clear();
            return;
        } else if (strcmp(two_word, "break list") == 0) {
            cmd_break_list();
            return;
        } else if (strcmp(two_word, "reg pc") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_reg_pc();
            return;
        } else if (strcmp(two_word, "reg a") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_reg_a();
            return;
        } else if (strcmp(two_word, "reg b") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_reg_b();
            return;
        } else if (strcmp(two_word, "reg x") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_reg_x();
            return;
        } else if (strcmp(two_word, "reg sp") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_reg_sp();
            return;
        } else if (strcmp(two_word, "reg ccr") == 0) {
            for (int i = 2; i < cmd_token_count; i++) {
                cmd_tokens[i - 2] = cmd_tokens[i];
            }
            cmd_token_count -= 2;
            cmd_reg_ccr();
            return;
        }
    }

    // Try single-word match
    if (strcmp(cmd_tokens[0], "load") == 0) {
        cmd_load();
    } else if (strcmp(cmd_tokens[0], "end") == 0) {
        cmd_end();
    } else if (strcmp(cmd_tokens[0], "config") == 0) {
        cmd_config_show();
    } else if (strcmp(cmd_tokens[0], "read") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_read();
    } else if (strcmp(cmd_tokens[0], "write") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_write();
    } else if (strcmp(cmd_tokens[0], "status") == 0) {
        cmd_status();
    } else if (strcmp(cmd_tokens[0], "run") == 0) {
        cmd_run();
    } else if (strcmp(cmd_tokens[0], "halt") == 0) {
        cmd_halt();
    } else if (strcmp(cmd_tokens[0], "reset") == 0) {
        cmd_reset();
    } else if (strcmp(cmd_tokens[0], "bootloader") == 0 || strcmp(cmd_tokens[0], "boot") == 0) {
        cmd_bootloader();
    } else if (strcmp(cmd_tokens[0], "break") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_break_set();
    } else if (strcmp(cmd_tokens[0], "bus_read_block") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_bus_read_block();
    } else if (strcmp(cmd_tokens[0], "bus_write_block") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_bus_write_block();
    } else if (strcmp(cmd_tokens[0], "bus_write") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_bus_write();
    } else if (strcmp(cmd_tokens[0], "bus_read") == 0) {
        for (int i = 1; i < cmd_token_count; i++) {
            cmd_tokens[i - 1] = cmd_tokens[i];
        }
        cmd_token_count--;
        cmd_bus_read();
    } else if (strcmp(cmd_tokens[0], "bus_info") == 0) {
        cmd_bus_info();
    } else if (strcmp(cmd_tokens[0], "help") == 0) {
        cmd_help();
    } else {
        usb_cdc_send("ERROR: Unknown command. Type 'help' for help.\r\n");
    }
}

// Process a complete command line
static void process_command(char *cmd) {
    // Skip leading whitespace
    while (*cmd && (*cmd == ' ' || *cmd == '\t')) {
        cmd++;
    }

    // Empty command
    if (*cmd == '\0') {
        return;
    }

    // Tokenize and dispatch command
    tokenize_command(cmd);
    dispatch_command();
}

// Initialize USB CDC
void usb_cdc_init(void) {
    // Initialize TinyUSB device stack
    tusb_init();

    cmd_pos = 0;
    in_hex_mode = false;
    printf("USB CDC interface initialized\n");
}

// Process USB events
void usb_cdc_task(void) {
    // Process TinyUSB events
    tud_task();

    // Process available characters in batches to prevent buffer overflow
    // Call tud_task() periodically to keep USB stack responsive
    uint32_t chars_processed = 0;
    const uint32_t BATCH_SIZE = 64; // Process in small batches

    while (tud_cdc_available() && chars_processed < 512) {
        // Read data
        char c = tud_cdc_read_char();
        chars_processed++;

        // Handle character
        if (in_hex_mode) {
            // In HEX mode, accumulate data until END command or EOF record
            // No echo to avoid slowing down paste operations
            if (hex_pos < HEX_BUFFER_SIZE - 1) {
                hex_buffer[hex_pos++] = c;
            }

            // Check for end of line (both CR and LF)
            if (c == '\n' || c == '\r') {
                // Check for 'end' command
                if (hex_pos >= 4 && strncmp(&hex_buffer[hex_pos - 4], "end", 3) == 0) {
                    // Remove 'end' from buffer and process
                    hex_pos -= 4;
                    process_command("end");
                }
                // Check for Intel HEX EOF record (:00000001FF)
                else if (hex_pos >= 12 && strncmp(&hex_buffer[hex_pos - 12], ":00000001FF", 11) == 0) {
                    // Found EOF record - auto-exit hex mode and process
                    usb_cdc_send("EOF record detected, processing HEX data...\r\n");
                    hex_buffer[hex_pos] = '\0';
                    if (ihex_load_data(hex_buffer, hex_pos)) {
                        usb_cdc_send("OK: EPROM loaded successfully\r\n");
                    } else {
                        usb_cdc_send("ERROR: Failed to load EPROM\r\n");
                    }
                    in_hex_mode = false;
                    hex_pos = 0;
                }
            }

        } else {
            // Normal command mode - echo as we go
            if (c == '\r' || c == '\n') {
                // End of line - process command
                tud_cdc_write_str("\r\n");
                tud_cdc_write_flush();

                if (cmd_pos > 0) {
                    cmd_buffer[cmd_pos] = '\0';
                    process_command(cmd_buffer);
                    cmd_pos = 0;
                }
            } else if (c == '\b' || c == 0x7F) {
                // Backspace
                if (cmd_pos > 0) {
                    cmd_pos--;
                    tud_cdc_write_str("\b \b");
                    tud_cdc_write_flush();
                }
            } else if (c >= 32 && c < 127) {
                // Printable character
                if (cmd_pos < CMD_BUFFER_SIZE - 1) {
                    cmd_buffer[cmd_pos++] = c;
                    // Echo character
                    tud_cdc_write_char(c);
                }
            }
        }

        // Periodically call tud_task() to keep USB responsive
        if ((chars_processed % BATCH_SIZE) == 0) {
            tud_task();
        }
    }
}

// Send string to USB
void usb_cdc_send(const char *str) {
    if (!tud_cdc_connected()) {
        return;
    }

    size_t len = strlen(str);
    size_t sent = 0;

    // Send in chunks, waiting for space if needed
    while (sent < len) {
        size_t available = tud_cdc_write_available();
        if (available > 0) {
            size_t to_send = (len - sent) < available ? (len - sent) : available;
            uint32_t written = tud_cdc_write(str + sent, to_send);
            sent += written;
            tud_cdc_write_flush();
        } else {
            // Buffer full, let TinyUSB process and drain
            tud_task();
            sleep_us(100);
        }
    }
}

// Send formatted string to USB
void usb_cdc_printf(const char *fmt, ...) {
    char buffer[256];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    usb_cdc_send(buffer);
}

//--------------------------------------------------------------------+
// TinyUSB Device Callbacks
//--------------------------------------------------------------------+

// Invoked when device is mounted
void tud_mount_cb(void) {
    printf("USB mounted\n");
}

// Invoked when device is unmounted
void tud_umount_cb(void) {
    printf("USB unmounted\n");
}

// Invoked when CDC line state changes (DTR/RTS)
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts) {
    (void)itf;
    (void)rts;

    if (dtr) {
        printf("USB CDC connected\n");
    } else {
        printf("USB CDC disconnected\n");
    }
}
