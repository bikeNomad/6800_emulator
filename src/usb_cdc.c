/**
 * USB CDC Interface Implementation
 */

#include "usb_cdc.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "debug_spi.h"
#include "emulator.h"
#include "hardware/clocks.h"
#include "ihex_parser.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"
#include "pico/bootrom.h"
#include "tusb.h"

// Command buffer
#define CMD_BUFFER_SIZE 4096
static char     cmd_buffer[CMD_BUFFER_SIZE];
static uint32_t cmd_pos = 0;
static bool     in_hex_mode = false;

// HEX data buffer
#define HEX_BUFFER_SIZE 32768
static char     hex_buffer[HEX_BUFFER_SIZE];
static uint32_t hex_pos = 0;

// Command tokenization
#define MAX_TOKENS 32
static char *cmd_tokens[MAX_TOKENS];
static int   cmd_token_count = 0;

// Command handler function type
typedef void (*cmd_handler_fn)(void);

// Command table entry
typedef struct {
    const char    *name;
    cmd_handler_fn handler;
    bool           needs_args;  // True if command needs remaining tokens as arguments
} command_entry_t;

static bool send_command_to_emulator(sm_event_t event) {
    sm_notification_t notification;
    if (!post_sm_event(event)) {
        usb_cdc_send("ERROR: Failed to send command to emulator\r\n");
        return false;  // queue full
    }
    uint32_t tries = 0;
    while (!receive_sm_notification(&notification)) {
        sleep_ms(1);
        tries++;
        if (tries > 100) {
            printf("Timeout waiting for notification after event %d\n", event);
            return false;
        }
    }
    return notification == NOTIF_OK;
}

static inline bool pause_emulator(void) {
    return send_command_to_emulator(EV_PAUSE_EMULATOR);
}

static inline bool resume_emulator(void) {
    return send_command_to_emulator(EV_RESUME_EMULATOR);
}

// Helper functions for bus operations with E clock management
static void bus_read_block_with_eclock(uint16_t address, uint16_t length, uint8_t *buffer) {
    if (!pause_emulator()) {
        return;
    }

    // Temporarily start E clock if needed
    bool was_running = eclock_is_running();
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

    resume_emulator();
}

static void bus_write_block_with_eclock(uint16_t address, const uint8_t *buffer, uint16_t length) {
    if (!pause_emulator()) {
        return;
    }

    // Temporarily start E clock if needed
    bool was_running = eclock_is_running();
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

    resume_emulator();
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
    if (!send_command_to_emulator(EV_CMD_LOAD)) {
        usb_cdc_send("ERROR: Failed to load image\r\n");
        return;
    }
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

    send_command_to_emulator(EV_CMD_RESET);
}

static void cmd_config_show(void) {
    // Display current memory configuration
    memory_print_summary(usb_cdc_printf);
    usb_cdc_printf("  Debug SPI: %s\r\n", debug_spi_is_enabled() ? "ON" : "OFF");
}

static void cmd_config_rom(void) {
    if (!pause_emulator()) {
        return;
    }
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

    resume_emulator();
}

static void cmd_config_ram(void) {
    if (!pause_emulator()) {
        return;
    }
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

    resume_emulator();
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

static void cmd_run(void) {
    // Start CPU execution
    if (!send_command_to_emulator(EV_CMD_RUN)) {
        usb_cdc_send("ERROR: Failed to start CPU\r\n");
        return;
    }
    usb_cdc_send("OK: CPU started\r\n");
}

static void cmd_halt(void) {
    // Stop CPU execution
    if (!send_command_to_emulator(EV_CMD_HALT)) {
        usb_cdc_send("ERROR: Failed to halt CPU\r\n");
        return;
    }
    usb_cdc_send("OK: CPU halted\r\n");
}

static void cmd_reset(void) {
    if (!send_command_to_emulator(EV_CMD_RESET)) {
        usb_cdc_send("ERROR: Failed to reset CPU\r\n");
        return;
    }
    usb_cdc_send("OK: CPU reset\r\n");
}

static void cmd_bootloader(void) {
    // Enter bootloader mode
    usb_cdc_send("Entering bootloader mode...\r\n");
    sleep_ms(100);         // Give time for message to send
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
    // Display startup warnings if any
    sanity_result_t startup_status = memory_get_startup_status();
    if (startup_status != SANITY_OK) {
        usb_cdc_printf("=== STARTUP WARNING ===\r\n");
        switch (startup_status) {
        case SANITY_NO_SAVED_MAP:
            usb_cdc_printf("No saved memory map found. Using defaults.\r\n");
            usb_cdc_printf("Run 'scan_memory' command to auto-configure.\r\n");
            break;
        case SANITY_RAM_MISMATCH:
            usb_cdc_printf("WARNING: RAM mismatch detected!\r\n");
            usb_cdc_printf("Run 'scan_memory' to update configuration.\r\n");
            break;
        case SANITY_ROM_UNEXPECTED:
            usb_cdc_printf("WARNING: Unexpected ROM/RAM detected.\r\n");
            usb_cdc_printf("Run 'scan_memory' to update configuration.\r\n");
            break;
        default:
            break;
        }
        usb_cdc_printf("=======================\r\n\r\n");
    }

    usb_cdc_printf("Emulator State: %s\r\n", sm_current_state_name());
    // Get CPU status
    usb_cdc_printf("CPU Status:\r\n");
    cpu_print_state(usb_cdc_printf);
    uint32_t cycle_cnt = eclock_get_count();
    uint32_t eclock_pio_cycles = eclock_is_running() ? eclock_get_pio_cycles() : last_pio_cycles;

    usb_cdc_printf("  Running: %s\r\n", cpu_is_running() ? "YES" : "NO");
    usb_cdc_printf("  Halted: %s\r\n", cpu.halted ? "YES" : "NO");
    usb_cdc_printf("  Instructions: %llu\r\n", (unsigned long long)cpu.instruction_count);
    usb_cdc_printf("  Cycle Count: %lu\r\n", cycle_cnt);
    usb_cdc_printf("  PIO Cycles: %lu\r\n", eclock_pio_cycles);
    usb_cdc_printf("  Overage: %ld\r\n", (long)cycle_overage);
    usb_cdc_printf("  Underage: %ld\r\n", (long)cycle_underage);
    usb_cdc_printf("  Speed ratio: %fx\r\n",
                   (cycle_cnt + (int32_t)cycle_underage) /
                       (eclock_pio_cycles > 0 ? (float)eclock_pio_cycles : 1.0f));

    // Calculate and display speed ratio
    if (eclock_pio_cycles > 0) {
        float speed_ratio = (float)cycle_cnt / (float)eclock_pio_cycles;
        usb_cdc_printf("  Speed: %fx real-time\r\n", speed_ratio);
    }

    // Include QSPI information
    uint32_t sys_clock_hz = clock_get_hz(clk_sys);
    uint32_t qspi_freq_hz = sys_clock_hz / QSPI_CLOCK_DIVISOR;
    usb_cdc_printf("  QSPI Bus: %lu MHz (divisor: %d)\r\n", qspi_freq_hz / 1000000,
                   QSPI_CLOCK_DIVISOR);

    if (bus_read_reset()) {
        usb_cdc_printf("  /RESET Pin asserted\r\n");
    }
    if (bus_read_nmi()) {
        usb_cdc_printf("  /NMI Pin asserted\r\n");
    }
    if (bus_read_irq()) {
        usb_cdc_printf("  /IRQ Pin asserted\r\n");
    }
    if (!eclock_is_running()) {
        usb_cdc_printf("  E Clock is stopped\r\n");
    }
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
        if (pause_emulator() == false) {
            return;
        }

        usb_cdc_printf("Reading $%04X bytes from $%04X:\r\n", len, addr);

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

        resume_emulator();
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
    uint8_t  buffer[1024];  // Max block size
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

    if (pause_emulator() == false) {
        return;
    }

    for (uint32_t i = 0; i < count; i++) {
        memory_write_fast(addr + i, buffer[i]);
    }

    resume_emulator();

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
            usb_cdc_printf("ERROR: Failed to set breakpoint at $%04X (max %d "
                           "breakpoints)\r\n",
                           addr, MAX_BREAKPOINTS);
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
        const uint16_t *breakpoints = cpu_get_breakpoints();
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
        if (pause_emulator() == false) {
            return;
        }
        cpu.pc = (uint16_t)value;
        resume_emulator();
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
        if (pause_emulator() == false) {
            return;
        }
        cpu.a = (uint8_t)value;
        resume_emulator();
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
        if (pause_emulator() == false) {
            return;
        }
        cpu.b = (uint8_t)value;
        resume_emulator();
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
        if (pause_emulator() == false) {
            return;
        }
        cpu.x = (uint16_t)value;
        resume_emulator();
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
        if (pause_emulator() == false) {
            return;
        }
        cpu.sp = (uint16_t)value;
        resume_emulator();
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
        if (pause_emulator() == false) {
            return;
        }
        cpu.ccr = ((uint8_t)value & 0x3F) | CCR_FIXED;  // Preserve bits 7-6
        resume_emulator();
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
        // Read block and format output like the 'read' command
        uint8_t buffer[1024];  // Max block size
        bus_read_block_with_eclock((uint16_t)address, (uint16_t)length, buffer);

        usb_cdc_printf("Reading $%04X bytes from $%04X:\r\n", length, address);

        for (uint32_t i = 0; i < length; i++) {
            if (i % 16 == 0) {
                usb_cdc_printf("%04X: ", address + i);
            }
            usb_cdc_printf("%02X ", buffer[i]);
            if (i % 16 == 15 || i == length - 1) {
                usb_cdc_send("\r\n");
            }
        }
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
    uint8_t  buffer[1024];  // Max block size
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

#if COUNT_INSTRUCTIONS
static void cmd_print_instruction_counts(void) {
    instruction_count_report(usb_cdc_printf);
}

static void cmd_reset_instruction_counts(void) {
    bool old = instruction_count_enable(false);
    instruction_count_initialize();
    instruction_count_enable(old);
    cpu.instruction_count = 0;  // Reset instruction counter
    clock_reset_counters();
    usb_cdc_printf("OK: Instruction counts reset (counting: %d)\r\n", old);
}

static void cmd_count_on(void) {
    instruction_count_enable(true);
    usb_cdc_printf("OK: Instruction counting enabled\r\n");
}

static void cmd_count_off(void) {
    instruction_count_enable(false);
    usb_cdc_printf("OK: Instruction counting disabled\r\n");
}
#endif

// Helper function to get memory type and mapping status for an address
static memory_type_t get_memory_info_at_address(uint16_t address, bool *mapped) {
    memory_type_t type = memory_get_mapping_type(address);
    *mapped = memory_is_address_mapped(address);
    return type;
}

// Helper function to format memory info string
static const char *format_memory_info(memory_type_t type, bool mapped) {
    if (!mapped) {
        return "UNMAPPED";
    }

    switch (type) {
    case MEM_TYPE_ROM:
        return "ROM";
    case MEM_TYPE_RAM:
        return "RAM";
    case MEM_TYPE_CMOS:
        return "CMOS";
    case MEM_TYPE_UNMAPPED:
        return "UNMAPPED";
    default:
        return "UNKNOWN";
    }
}

static void cmd_map_show(void) {
    // Reload memory map from flash to ensure it's not corrupted
    memory_load_memory_map_from_flash();

    // Display memory mapping state with contiguous ranges
    usb_cdc_send("Memory Map:\r\n");

    // Determine if this is System 11 (full A15 decode) or other system
    // For System 11, the memory map has entries for the full 64KB address space
    // For other systems, only the low 32KB is mapped with aliases
    bool is_system11 = false;
    for (int i = 128; i < 256; i++) {  // Check high address space pages
        if (memory_map[i] != ENTRY_UNMAPPED_BUS) {
            is_system11 = true;
            break;
        }
    }

    if (is_system11) {
        // System 11: Display full 64KB address space
        usb_cdc_send("Full address space (A15 fully decoded):\r\n");
        uint32_t current_addr = 0x0000;
        uint32_t end_addr = 0xFFFF;

        while (current_addr <= end_addr) {
            bool          mapped = false;
            memory_type_t type = get_memory_info_at_address((uint16_t)current_addr, &mapped);
            const char   *info = format_memory_info(type, mapped);

            // Find the end of this contiguous range
            uint32_t range_end = current_addr;
            while (range_end + 1 <= end_addr) {
                bool          next_mapped = false;
                memory_type_t next_type = get_memory_info_at_address((uint16_t)(range_end + 1), &next_mapped);
                const char   *next_info = format_memory_info(next_type, next_mapped);

                if (strcmp(info, next_info) == 0) {
                    range_end++;
                } else {
                    break;
                }
            }

            usb_cdc_printf("  $%04X-$%04X: %s\r\n", (uint16_t)current_addr, (uint16_t)range_end, info);
            current_addr = range_end + 1;
        }
    } else {
        // Other systems: Display low/high address spaces with aliases
        // Process low address space (A15 = 0)
        usb_cdc_send("Low address space (A15 = 0):\r\n");
        uint16_t current_addr = 0x0000;
        uint16_t end_addr = 0x8000;  // Up to but not including high alias

        while (current_addr < end_addr) {
            bool          mapped = false;
            memory_type_t type = get_memory_info_at_address(current_addr, &mapped);
            const char   *info = format_memory_info(type, mapped);

            // Find the end of this contiguous range
            uint16_t range_end = current_addr;
            while (range_end + 1 < end_addr) {
                bool          next_mapped = false;
                memory_type_t next_type = get_memory_info_at_address(range_end + 1, &next_mapped);
                const char   *next_info = format_memory_info(next_type, next_mapped);

                if (strcmp(info, next_info) == 0) {
                    range_end++;
                } else {
                    break;
                }
            }

            usb_cdc_printf("  $%04X-$%04X: %s\r\n", current_addr, range_end, info);
            current_addr = range_end + 1;
        }

        // Process high address space (A15 = 1) as aliases
        usb_cdc_send("\nHigh address space (A15 = 1, aliases):\r\n");
        current_addr = 0x8000;
        end_addr = 0xFFFF;

        while (current_addr < end_addr) {
            bool          mapped = false;
            memory_type_t type = get_memory_info_at_address(current_addr, &mapped);
            const char   *info = format_memory_info(type, mapped);

            // Find the end of this contiguous range
            uint16_t range_end = current_addr;
            while (range_end + 1 < end_addr) {
                bool          next_mapped = false;
                memory_type_t next_type = get_memory_info_at_address(range_end + 1, &next_mapped);
                const char   *next_info = format_memory_info(next_type, next_mapped);

                if (strcmp(info, next_info) == 0) {
                    range_end++;
                } else {
                    break;
                }
            }

            usb_cdc_printf("  $%04X-$%04X: %s (alias of $%04X-$%04X)\r\n", current_addr, range_end,
                           info, current_addr & 0x7FFF, range_end & 0x7FFF);
            current_addr = range_end + 1;
        }
    }
}

static void cmd_map_clear(void) {
    send_command_to_emulator(EV_CMD_HALT);
    // Clear all ROM mapping
    memory_clear_rom_mapping();
    usb_cdc_send("OK: All ROM pages unmapped\r\n");
    send_command_to_emulator(EV_CMD_RESET);
}

static void cmd_map_program(void) {
    // Manually map a specific ROM page (for debugging)
    // Expects tokens: [map] [program] <addr>
    if (cmd_token_count < 1) {
        usb_cdc_send("ERROR: Usage: map program <address_hex>\r\n");
        return;
    }

    unsigned int address;
    if (sscanf(cmd_tokens[0], "%x", &address) == 1) {
        if (address >= mem_config.rom_base && address < mem_config.rom_base + mem_config.rom_size) {
            send_command_to_emulator(EV_CMD_HALT);
            memory_set_rom_mapping(address, true);
            send_command_to_emulator(EV_CMD_RESET);
            usb_cdc_printf("OK: Mapped ROM page at $%04X\r\n", address);
        } else {
            usb_cdc_send("ERROR: Address outside ROM range\r\n");
        }
    } else {
        usb_cdc_send("ERROR: Usage: map program <address_hex>\r\n");
    }
}

static void cmd_checksum(void) {
    // Checksum: checksum <addr> <len>
    // Expects tokens: [checksum] <addr> <len>
    if (cmd_token_count < 2) {
        usb_cdc_send("ERROR: Usage: checksum <addr_hex> <len_hex>\r\n");
        return;
    }

    unsigned int addr, len;
    if (sscanf(cmd_tokens[0], "%x", &addr) != 1 || sscanf(cmd_tokens[1], "%x", &len) != 1) {
        usb_cdc_send("ERROR: Usage: checksum <addr_hex> <len_hex>\r\n");
        return;
    }

    if (addr > MAX_ADDRESS) {
        usb_cdc_send("ERROR: Address out of range\r\n");
    } else if (len == 0 || len > 65535) {
        usb_cdc_send("ERROR: Length must be 1-65535\r\n");
    } else if (addr + len > MAX_ADDRESS + 1) {
        usb_cdc_send("ERROR: Block exceeds address space\r\n");
    } else {
        if (pause_emulator() == false) {
            return;
        }

        // Compute checksum (modulo 64k sum of bytes)
        uint32_t sum = 0;
        for (uint32_t i = 0; i < len; i++) {
            uint8_t value = memory_read_fast(addr + i);
            sum += value;
        }
        uint16_t checksum = sum & 0xFFFF;  // Modulo 64k

        usb_cdc_printf("Checksum of $%04X bytes from $%04X: $%04X\r\n", len, addr, checksum);

        resume_emulator();
    }
}

static void cmd_copy_roms(void) {
    // Copy ROMs: scan ROM address range and copy non-0xFF pages to persistent storage
    usb_cdc_send("Scanning ROM address range for valid data...\r\n");

    if (!pause_emulator()) {
        usb_cdc_send("ERROR: Failed to pause emulator\r\n");
        return;
    }

    // Clear ROM load buffer and mapping bitmap
    memory_clear_rom_load_buffer();
    memory_clear_rom_mapping();

    uint16_t pages_scanned = 0;
    uint16_t pages_copied = 0;

    // Scan through each 256-byte page in ROM range
    for (uint16_t page_addr = mem_config.rom_base;
         page_addr < mem_config.rom_base + mem_config.rom_size;
         page_addr += ENTRY_PAGE_SIZE) {

        pages_scanned++;

        // Read the entire 256-byte page from bus
        uint8_t page_buffer[ENTRY_PAGE_SIZE];
        bus_read_block_with_eclock(page_addr, ENTRY_PAGE_SIZE, page_buffer);

        // Check if page contains any non-0xFF data
        bool has_data = false;
        for (uint16_t i = 0; i < ENTRY_PAGE_SIZE; i++) {
            if (page_buffer[i] != 0xFF) {
                has_data = true;
                break;
            }
        }

        if (has_data) {
            // Load page data using memory_load_hex_data (handles address translation)
            if (memory_load_hex_data(page_addr, page_buffer, ENTRY_PAGE_SIZE)) {
                // Mark page as mapped
                memory_set_rom_mapping(page_addr, true);
                pages_copied++;
                usb_cdc_printf("Copied page at $%04X\r\n", page_addr);
            } else {
                usb_cdc_printf("ERROR: Failed to load page at $%04X\r\n", page_addr);
            }
        }

        // Progress update every 16 pages (4KB)
        if ((pages_scanned % 16) == 0) {
            usb_cdc_printf("Scanned %u pages, copied %u pages...\r\n", pages_scanned, pages_copied);
        }
    }

    usb_cdc_send("Scan complete. Finalizing ROM load...\r\n");

    // Finalize the load (write to flash and update mappings)
    if (memory_finalize_load()) {
        usb_cdc_printf("OK: ROM copy complete - %u pages scanned, %u pages copied\r\n",
                       pages_scanned, pages_copied);
    } else {
        usb_cdc_send("ERROR: Failed to finalize ROM load\r\n");
    }

    resume_emulator();
}

static void cmd_scan_memory(void) {
    // Scan memory and auto-configure memory map
    usb_cdc_send("Scanning target system memory...\r\n");

    if (!memory_scan_and_build_map(usb_cdc_printf)) {
        usb_cdc_send("ERROR: Failed to scan memory\r\n");
        return;
    }

    // Print scan results (use coalesced results for System 11)
    usb_cdc_send("\r\nScan Results:\r\n");

    const scan_result_t *results = memory_get_coalesced_scan_results();
    uint16_t             start = 0;
    uint8_t              last_type = results[0].type;

    for (int page = 1; page <= 256; page++) {
        uint8_t current_type = (page < 256) ? results[page].type : 255;  // 255 = sentinel

        if (current_type != last_type || page == 256) {
            uint16_t    end = (page << 8) - 1;
            const char *type_str;
            switch (last_type) {
            case 0:
                type_str = "EMPTY";
                break;
            case 1:
                type_str = "ROM";
                break;
            case 2:
                type_str = "RAM";
                break;
            case 3:
                type_str = "CMOS";
                break;
            case 4:
                type_str = "PIA";
                break;
            case 5:
                type_str = "UNMAPPED";
                break;
            default:
                type_str = "UNKNOWN";
                break;
            }
            usb_cdc_printf("  $%04X-$%04X: %s\r\n", start, end, type_str);

            start = page << 8;
            last_type = current_type;
        }
    }

    // Show configuration
    usb_cdc_printf("\r\nMemory Configuration:\r\n");
    usb_cdc_printf("  ROM: $%04X-$%04X (%d bytes)\r\n",
                   mem_config.rom_base,
                   mem_config.rom_base + mem_config.rom_size - 1,
                   mem_config.rom_size);
    usb_cdc_printf("  RAM: $%04X-$%04X (%d bytes)\r\n",
                   mem_config.ram_base,
                   mem_config.ram_base + mem_config.ram_size - 1,
                   mem_config.ram_size);
    if (mem_config.cmos_size > 0) {
        usb_cdc_printf("  CMOS: $%04X-$%04X (%d bytes)\r\n",
                       mem_config.cmos_base,
                       mem_config.cmos_base + mem_config.cmos_size - 1,
                       mem_config.cmos_size);
    }

    usb_cdc_send("\r\nMemory map and ROM contents have been saved to flash.\r\n");
    usb_cdc_send("Configuration will be used automatically on next boot.\r\n");
    usb_cdc_send("Target system can now be removed - emulator will run from stored ROM.\r\n");
}

static void cmd_verify_memory(void) {
    // Verify memory configuration against hardware
    usb_cdc_send("Verifying memory configuration...\r\n");

    if (!pause_emulator()) {
        usb_cdc_send("ERROR: Failed to pause emulator\r\n");
        return;
    }

    sanity_result_t result = memory_sanity_check();

    resume_emulator();

    switch (result) {
    case SANITY_OK:
        usb_cdc_send("OK: Memory configuration matches hardware\r\n");
        break;
    case SANITY_RAM_MISMATCH:
        usb_cdc_send("ERROR: RAM mismatch - run 'scan_memory' to update\r\n");
        break;
    case SANITY_ROM_UNEXPECTED:
        usb_cdc_send("WARNING: ROM/RAM differs from saved - run 'scan_memory' to update\r\n");
        break;
    case SANITY_NO_SAVED_MAP:
        usb_cdc_send("ERROR: No saved memory map - run 'scan_memory' first\r\n");
        break;
    }
}

static void cmd_help(void) {
    // Send as single string to avoid buffer overflow
    usb_cdc_send("MC6800 Emulator Commands:\r\n"
                 "  load                      - Load Intel HEX (auto-detects "
                 "ROM/CMOS)\r\n"
                 "  config                    - Show memory configuration\r\n"
                 "  config rom <b> <s>        - Configure ROM region\r\n"
                 "  config ram <b> <s>        - Configure RAM region\r\n"
                 "  cmos dump                 - Display CMOS RAM contents\r\n"
                 "  checksum <addr> <len>     - Calculate checksum of memory range\r\n"
                 "  read <addr> <len>         - Read memory\r\n"
                 "  write <addr> <data>       - Write memory\r\n"
                 "  status                    - Display CPU status\r\n"
                 "  run                       - Start CPU execution\r\n"
                 "  halt                      - Stop CPU execution (auto-saves CMOS)\r\n"
                 "  reset                     - Reset CPU (auto-saves CMOS)\r\n"
#if COUNT_INSTRUCTIONS
                 "  count print               - Print instruction execution counts\r\n"
                 "  count reset               - Reset instruction execution counts\r\n"
                 "  count on                  - Enable instruction counting\r\n"
                 "  count off                 - Disable instruction counting\r\n"
#endif
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
                 "  map show                  - Show ROM mapping state\r\n"
                 "  map clear                 - Clear all ROM mapping\r\n"
                 "  map program <addr>        - Manually map ROM page\r\n"
                 "  copy_roms                 - Copy ROM data from bus to persistent storage\r\n"
                 "  scan_memory               - Auto-detect and configure memory map\r\n"
                 "  verify_memory             - Verify memory configuration\r\n"
                 "  bootloader                - Enter bootloader mode\r\n"
                 "  help                      - Show this help\r\n");
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

// Command table - two-word commands checked first, then single-word
static const command_entry_t command_table[] = {
    // Two-word commands (require exact match of first two tokens)
    { "config rom", cmd_config_rom, true },  // needs <base> <size>
    { "config ram", cmd_config_ram, true },  // needs <base> <size>
    { "cmos dump", cmd_cmos_dump, false },
    { "debug on", cmd_debug_on, false },
    { "debug off", cmd_debug_off, false },
    { "break clear", cmd_break_clear, true },  // needs optional <addr>
    { "break list", cmd_break_list, false },
    { "reg pc", cmd_reg_pc, true },            // needs <value>
    { "reg a", cmd_reg_a, true },              // needs <value>
    { "reg b", cmd_reg_b, true },              // needs <value>
    { "reg x", cmd_reg_x, true },              // needs <value>
    { "reg sp", cmd_reg_sp, true },            // needs <value>
    { "reg ccr", cmd_reg_ccr, true },          // needs <value>
#if COUNT_INSTRUCTIONS
    { "count print", cmd_print_instruction_counts, false },
    { "count reset", cmd_reset_instruction_counts, false },
    { "count on", cmd_count_on, false },
    { "count off", cmd_count_off, false },
#endif

    // Single-word commands
    { "load", cmd_load, false },
    { "end", cmd_end, false },
    { "config", cmd_config_show, false },
    { "checksum", cmd_checksum, true },  // needs <addr> <len>
    { "read", cmd_read, true },          // needs <addr> <len>
    { "write", cmd_write, true },        // needs <addr> <data...>
    { "status", cmd_status, false },
    { "run", cmd_run, false },
    { "halt", cmd_halt, false },
    { "reset", cmd_reset, false },
    { "bootloader", cmd_bootloader, false },
    { "boot", cmd_bootloader, false },                 // Alias for bootloader
    { "break", cmd_break_set, true },                  // needs <addr>
    { "bus_read_block", cmd_bus_read_block, true },    // needs <addr> <len>
    { "bus_write_block", cmd_bus_write_block, true },  // needs <addr> <data...>
    { "bus_write", cmd_bus_write, true },              // needs <addr> <data>
    { "bus_read", cmd_bus_read, true },                // needs <addr>
    { "bus_info", cmd_bus_info, false },
    { "map show", cmd_map_show, false },
    { "map clear", cmd_map_clear, false },
    { "map program", cmd_map_program, true },  // needs <addr>
    { "copy_roms", cmd_copy_roms, false },
    { "scan_memory", cmd_scan_memory, false },
    { "verify_memory", cmd_verify_memory, false },
    { "help", cmd_help, false },
    { NULL, NULL, false }  // Terminator
};

static void dispatch_command(void) {
    // Empty command
    if (cmd_token_count == 0) {
        return;
    }

    // Build two-word command string if we have at least 2 tokens
    char two_word[64];
    if (cmd_token_count >= 2) {
        snprintf(two_word, sizeof(two_word), "%s %s", cmd_tokens[0], cmd_tokens[1]);
    }

    // Search through command table
    for (int i = 0; command_table[i].name != NULL; i++) {
        const char *cmd_name = command_table[i].name;
        bool        matched = false;
        int         tokens_consumed = 0;

        // Check if this is a two-word command (contains a space)
        if (strchr(cmd_name, ' ') != NULL) {
            // Two-word command - only match if we have enough tokens
            if (cmd_token_count >= 2 && strcmp(two_word, cmd_name) == 0) {
                matched = true;
                tokens_consumed = 2;
            }
        } else {
            // Single-word command - match first token
            if (strcmp(cmd_tokens[0], cmd_name) == 0) {
                matched = true;
                tokens_consumed = 1;
            }
        }

        if (matched) {
            // If command needs arguments, shift remaining tokens
            if (command_table[i].needs_args) {
                for (int j = tokens_consumed; j < cmd_token_count; j++) {
                    cmd_tokens[j - tokens_consumed] = cmd_tokens[j];
                }
                cmd_token_count -= tokens_consumed;
            }

            // Call the handler
            command_table[i].handler();
            return;
        }
    }

    // No match found
    usb_cdc_send("ERROR: Unknown command. Type 'help' for help.\r\n");
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
    uint32_t       chars_processed = 0;
    const uint32_t BATCH_SIZE = 64;  // Process in small batches

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
                else if (hex_pos >= 12 &&
                         strncmp(&hex_buffer[hex_pos - 12], ":00000001FF", 11) == 0) {
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
                    send_command_to_emulator(EV_CMD_RESET);
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
                    tud_cdc_write_flush();  // echo
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
            size_t   to_send = (len - sent) < available ? (len - sent) : available;
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
int usb_cdc_printf(const char *restrict fmt, ...) {
    char    buffer[256];
    va_list args;
    va_start(args, fmt);
    int retval = vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    usb_cdc_send(buffer);
    return retval;
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
