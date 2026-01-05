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
    bool needs_args; // True if command needs remaining tokens as arguments
} command_entry_t;

static bool send_command_to_emulator(sm_event_t event) {
    sm_notification_t notification;
    if (!post_sm_event(event)) {
        usb_cdc_send("ERROR: Failed to send command to emulator\r\n");
        return false; // queue full
    }
    while (!receive_sm_notification(&notification)) {
        sleep_ms(1);
    }
    return notification == NOTIF_OK;
}

static inline bool pause_emulator(void) { return send_command_to_emulator(EV_PAUSE_EMULATOR); }

static inline bool resume_emulator(void) { return send_command_to_emulator(EV_RESUME_EMULATOR); }

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
    // Reset CPU (CMOS will be auto-saved by background task if needed)
    if (!send_command_to_emulator(EV_CMD_RESET)) {
        usb_cdc_send("ERROR: Failed to reset CPU\r\n");
        return;
    }
    usb_cdc_send("OK: CPU reset\r\n");
}

static void cmd_bootloader(void) {
    // Enter bootloader mode
    usb_cdc_send("Entering bootloader mode...\r\n");
    sleep_ms(100);        // Give time for message to send
    reset_usb_boot(0, 0); // Reset into USB bootloader
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
    uint8_t buffer[1024]; // Max block size
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
        cpu.ccr = ((uint8_t)value & 0x3F) | CCR_FIXED; // Preserve bits 7-6
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
        // Read block and send data as space-separated hex bytes
        uint8_t buffer[1024]; // Max block size
        bus_read_block_with_eclock((uint16_t)address, (uint16_t)length, buffer);
        for (uint32_t i = 0; i < length; i++) {
            if (i > 0)
                usb_cdc_send(" ");
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
    uint8_t buffer[1024]; // Max block size
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
static void cmd_print_instruction_counts(void) { instruction_count_report(usb_cdc_printf); }

static void cmd_reset_instruction_counts(void) {
    bool old = instruction_count_enable(false);
    instruction_count_initialize();
    instruction_count_enable(old);
    cpu.instruction_count = 0; // Reset instruction counter
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

static void cmd_help(void) {
    // Send as single string to avoid buffer overflow
    usb_cdc_send("MC6800 Emulator Commands:\r\n"
                 "  load                      - Load Intel HEX (auto-detects "
                 "ROM/CMOS)\r\n"
                 "  config                    - Show memory configuration\r\n"
                 "  config rom <b> <s>        - Configure ROM region\r\n"
                 "  config ram <b> <s>        - Configure RAM region\r\n"
                 "  cmos dump                 - Display CMOS RAM contents\r\n"
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
    {"config rom", cmd_config_rom, true}, // needs <base> <size>
    {"config ram", cmd_config_ram, true}, // needs <base> <size>
    {"cmos dump", cmd_cmos_dump, false},
    {"debug on", cmd_debug_on, false},
    {"debug off", cmd_debug_off, false},
    {"break clear", cmd_break_clear, true}, // needs optional <addr>
    {"break list", cmd_break_list, false},
    {"reg pc", cmd_reg_pc, true},   // needs <value>
    {"reg a", cmd_reg_a, true},     // needs <value>
    {"reg b", cmd_reg_b, true},     // needs <value>
    {"reg x", cmd_reg_x, true},     // needs <value>
    {"reg sp", cmd_reg_sp, true},   // needs <value>
    {"reg ccr", cmd_reg_ccr, true}, // needs <value>
#if COUNT_INSTRUCTIONS
    {"count print", cmd_print_instruction_counts, false},
    {"count reset", cmd_reset_instruction_counts, false},
    {"count on", cmd_count_on, false},
    {"count off", cmd_count_off, false},
#endif

    // Single-word commands
    {"load", cmd_load, false},
    {"end", cmd_end, false},
    {"config", cmd_config_show, false},
    {"read", cmd_read, true},   // needs <addr> <len>
    {"write", cmd_write, true}, // needs <addr> <data...>
    {"status", cmd_status, false},
    {"run", cmd_run, false},
    {"halt", cmd_halt, false},
    {"reset", cmd_reset, false},
    {"bootloader", cmd_bootloader, false},
    {"boot", cmd_bootloader, false},                // Alias for bootloader
    {"break", cmd_break_set, true},                 // needs <addr>
    {"bus_read_block", cmd_bus_read_block, true},   // needs <addr> <len>
    {"bus_write_block", cmd_bus_write_block, true}, // needs <addr> <data...>
    {"bus_write", cmd_bus_write, true},             // needs <addr> <data>
    {"bus_read", cmd_bus_read, true},               // needs <addr>
    {"bus_info", cmd_bus_info, false},
    {"help", cmd_help, false},
    {NULL, NULL, false} // Terminator
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
        bool matched = false;
        int tokens_consumed = 0;

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
                    tud_cdc_write_flush(); // echo
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
int usb_cdc_printf(const char *restrict fmt, ...) {
    char buffer[256];
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
void tud_mount_cb(void) { printf("USB mounted\n"); }

// Invoked when device is unmounted
void tud_umount_cb(void) { printf("USB unmounted\n"); }

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