/**
 * USB CDC Interface Implementation
 */

#include "usb_cdc.h"
#include "cpu_state.h"
#include "memory.h"
#include "ihex_parser.h"
#include "interrupts.h"
#include "pico/stdlib.h"
#include "tusb.h"
#include <stdio.h>
#include <string.h>
#include <stdarg.h>

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

    // Parse command
    if (strncmp(cmd, "load", 4) == 0) {
        // Enter HEX load mode
        in_hex_mode = true;
        hex_pos = 0;
        usb_cdc_send("Ready to receive Intel HEX data. Paste file now...\r\n");

    } else if (strncmp(cmd, "end", 3) == 0) {
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

    } else if (strncmp(cmd, "config rom", 10) == 0) {
        // Configure ROM region: config rom <base> <size>
        unsigned int base, size;
        if (sscanf(cmd + 10, "%x %x", &base, &size) == 2) {
            memory_configure_rom(base, size);
            usb_cdc_printf("OK: ROM configured at $%04X, size $%04X\r\n", base, size);
        } else {
            usb_cdc_send("ERROR: Usage: config rom <base_hex> <size_hex>\r\n");
        }

    } else if (strncmp(cmd, "config ram", 10) == 0) {
        // Configure RAM region: config ram <base> <size>
        unsigned int base, size;
        if (sscanf(cmd + 10, "%x %x", &base, &size) == 2) {
            memory_configure_ram(base, size);
            usb_cdc_printf("OK: RAM configured at $%04X, size $%04X\r\n", base, size);
        } else {
            usb_cdc_send("ERROR: Usage: config ram <base_hex> <size_hex>\r\n");
        }

    } else if (strncmp(cmd, "read", 4) == 0) {
        // Read memory: read <addr> <len>
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
            usb_cdc_send("ERROR: Usage: read <addr_hex> <len_hex>\r\n");
        }

    } else if (strncmp(cmd, "write", 5) == 0) {
        // Write memory: write <addr> <data...>
        unsigned int addr;
        char *addr_str = cmd + 5;
        // Skip leading whitespace
        while (*addr_str == ' ') addr_str++;
        // Parse address
        if (sscanf(addr_str, "%x", &addr) == 1) {
            // Skip past the address hex digits
            while (*addr_str && *addr_str != ' ') addr_str++;
            // Now find the data bytes
            char *data_str = addr_str;
            while (*data_str == ' ') data_str++;  // Skip spaces

            if (*data_str) {
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
            usb_cdc_send("ERROR: Usage: write <addr_hex> <byte_hex> ...\r\n");
        }

    } else if (strcmp(cmd, "status") == 0) {
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

    } else if (strcmp(cmd, "run") == 0) {
        // Start CPU execution
        cpu_start();
        usb_cdc_send("OK: CPU started\r\n");

    } else if (strcmp(cmd, "halt") == 0) {
        // Stop CPU execution
        cpu_halt();
        usb_cdc_send("OK: CPU halted\r\n");

    } else if (strcmp(cmd, "reset") == 0) {
        // Reset CPU
        interrupt_service_reset();
        usb_cdc_send("OK: CPU reset\r\n");

    } else if (strcmp(cmd, "help") == 0) {
        // Send as single string to avoid buffer overflow
        usb_cdc_send(
            "MC6800 Emulator Commands:\r\n"
            "  load                - Load Intel HEX (auto-detects EOF)\r\n"
            "  config rom <b> <s>  - Configure ROM region\r\n"
            "  config ram <b> <s>  - Configure RAM region\r\n"
            "  read <addr> <len>   - Read memory\r\n"
            "  write <addr> <data> - Write memory\r\n"
            "  status              - Display CPU status\r\n"
            "  run                 - Start CPU execution\r\n"
            "  halt                - Stop CPU execution\r\n"
            "  reset               - Reset CPU\r\n"
            "  help                - Show this help\r\n"
        );

    } else {
        usb_cdc_send("ERROR: Unknown command. Type 'help' for help.\r\n");
    }
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
