/**
 * USB CDC Interface Implementation
 */

#include "usb_cdc.h"
#include "cpu_state.h"
#include "memory.h"
#include "ihex_parser.h"
#include "interrupts.h"
#include "pico/stdlib.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

// Stub implementations for TinyUSB functions (until proper USB is configured)
static inline void tud_task(void) { }
static inline bool tud_cdc_connected(void) { return false; }
static inline uint32_t tud_cdc_available(void) { return 0; }
static inline char tud_cdc_read_char(void) { return 0; }
static inline uint32_t tud_cdc_write_char(char c) { (void)c; return 0; }
static inline uint32_t tud_cdc_write_str(const char *str) { (void)str; return 0; }
static inline uint32_t tud_cdc_write_flush(void) { return 0; }

// Command buffer
#define CMD_BUFFER_SIZE 4096
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint32_t cmd_pos = 0;
static bool in_hex_mode = false;

// HEX data buffer
#define HEX_BUFFER_SIZE 32768
static char hex_buffer[HEX_BUFFER_SIZE];
static uint32_t hex_pos = 0;

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

    printf("Command: %s\n", cmd);

    // Parse command
    if (strncmp(cmd, "LOAD", 4) == 0) {
        // Enter HEX load mode
        in_hex_mode = true;
        hex_pos = 0;
        usb_cdc_send("Ready to receive Intel HEX data. Send END to finish.\r\n");

    } else if (strncmp(cmd, "END", 3) == 0) {
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

    } else if (strncmp(cmd, "CONFIG ROM", 10) == 0) {
        // Configure ROM region: CONFIG ROM <base> <size>
        unsigned int base, size;
        if (sscanf(cmd + 10, "%x %x", &base, &size) == 2) {
            memory_configure_rom(base, size);
            usb_cdc_printf("OK: ROM configured at $%04X, size $%04X\r\n", base, size);
        } else {
            usb_cdc_send("ERROR: Usage: CONFIG ROM <base_hex> <size_hex>\r\n");
        }

    } else if (strncmp(cmd, "CONFIG RAM", 10) == 0) {
        // Configure RAM region: CONFIG RAM <base> <size>
        unsigned int base, size;
        if (sscanf(cmd + 10, "%x %x", &base, &size) == 2) {
            memory_configure_ram(base, size);
            usb_cdc_printf("OK: RAM configured at $%04X, size $%04X\r\n", base, size);
        } else {
            usb_cdc_send("ERROR: Usage: CONFIG RAM <base_hex> <size_hex>\r\n");
        }

    } else if (strncmp(cmd, "READ", 4) == 0) {
        // Read memory: READ <addr> <len>
        unsigned int addr, len;
        if (sscanf(cmd + 4, "%x %x", &addr, &len) == 2) {
            usb_cdc_printf("Reading $%04X bytes from $%04X:\r\n", len, addr);
            for (uint32_t i = 0; i < len; i++) {
                if (i % 16 == 0) {
                    usb_cdc_printf("%04X: ", addr + i);
                }
                uint8_t value = memory_read(addr + i);
                usb_cdc_printf("%02X ", value);
                if (i % 16 == 15 || i == len - 1) {
                    usb_cdc_send("\r\n");
                }
            }
        } else {
            usb_cdc_send("ERROR: Usage: READ <addr_hex> <len_hex>\r\n");
        }

    } else if (strncmp(cmd, "WRITE", 5) == 0) {
        // Write memory: WRITE <addr> <data...>
        unsigned int addr;
        if (sscanf(cmd + 5, "%x", &addr) == 1) {
            char *data_str = strchr(cmd + 5, ' ');
            if (data_str) {
                data_str++;
                while (*data_str) {
                    unsigned int value;
                    if (sscanf(data_str, "%x", &value) == 1) {
                        memory_write(addr++, value);
                        // Skip to next hex value
                        while (*data_str && *data_str != ' ') data_str++;
                        while (*data_str && *data_str == ' ') data_str++;
                    } else {
                        break;
                    }
                }
                usb_cdc_send("OK\r\n");
            }
        } else {
            usb_cdc_send("ERROR: Usage: WRITE <addr_hex> <byte_hex> ...\r\n");
        }

    } else if (strcmp(cmd, "STATUS") == 0) {
        // Get CPU status
        usb_cdc_printf("CPU Status:\r\n");
        usb_cdc_printf("  PC: $%04X\r\n", cpu.pc);
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

    } else if (strcmp(cmd, "RUN") == 0) {
        // Start CPU execution
        cpu_start();
        usb_cdc_send("OK: CPU started\r\n");

    } else if (strcmp(cmd, "HALT") == 0) {
        // Stop CPU execution
        cpu_halt();
        usb_cdc_send("OK: CPU halted\r\n");

    } else if (strcmp(cmd, "RESET") == 0) {
        // Reset CPU
        interrupt_service_reset();
        usb_cdc_send("OK: CPU reset\r\n");

    } else if (strcmp(cmd, "HELP") == 0) {
        usb_cdc_send("MC6800 Emulator Commands:\r\n");
        usb_cdc_send("  LOAD              - Enter HEX load mode\r\n");
        usb_cdc_send("  END               - Finish HEX load\r\n");
        usb_cdc_send("  CONFIG ROM <b> <s> - Configure ROM region\r\n");
        usb_cdc_send("  CONFIG RAM <b> <s> - Configure RAM region\r\n");
        usb_cdc_send("  READ <addr> <len> - Read memory\r\n");
        usb_cdc_send("  WRITE <addr> <data...> - Write memory\r\n");
        usb_cdc_send("  STATUS            - Display CPU status\r\n");
        usb_cdc_send("  RUN               - Start CPU execution\r\n");
        usb_cdc_send("  HALT              - Stop CPU execution\r\n");
        usb_cdc_send("  RESET             - Reset CPU\r\n");
        usb_cdc_send("  HELP              - Show this help\r\n");

    } else {
        usb_cdc_send("ERROR: Unknown command. Type HELP for help.\r\n");
    }
}

// Initialize USB CDC
void usb_cdc_init(void) {
    // TinyUSB will be initialized by stdio_init_all() in main
    cmd_pos = 0;
    in_hex_mode = false;
    printf("USB CDC interface initialized\n");
}

// Process USB events
void usb_cdc_task(void) {
    // Process TinyUSB events
    tud_task();

    // Check if data available
    if (tud_cdc_available()) {
        // Read data
        char c = tud_cdc_read_char();

        // Echo character (except for HEX mode)
        if (!in_hex_mode) {
            tud_cdc_write_char(c);
            tud_cdc_write_flush();
        }

        // Handle character
        if (in_hex_mode) {
            // In HEX mode, accumulate data until END command
            if (hex_pos < HEX_BUFFER_SIZE - 1) {
                hex_buffer[hex_pos++] = c;
            }

            // Check for END command at start of line
            if (c == '\n' && hex_pos >= 4) {
                if (strncmp(&hex_buffer[hex_pos - 4], "END", 3) == 0) {
                    // Remove END from buffer and process
                    hex_pos -= 4;
                    process_command("END");
                }
            }

        } else {
            // Normal command mode
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
                }
            }
        }
    }
}

// Send string to USB
void usb_cdc_send(const char *str) {
    if (tud_cdc_connected()) {
        tud_cdc_write_str(str);
        tud_cdc_write_flush();
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
