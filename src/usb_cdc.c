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
#include "hardware/gpio.h"
#include "hardware/structs/iobank0.h"
#include "hardware/structs/padsbank0.h"
#include "pico/time.h"
#include "ihex_parser.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"
#include "memory_fingerprint.h"
#include "memory_map.h"
#include "pico/bootrom.h"
#include "tusb.h"
#include "hardware/adc.h"
#include "hardware/vreg.h"

// Command buffer
#define CMD_BUFFER_SIZE 4096
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint32_t cmd_pos = 0;
static bool in_hex_mode = false;

// HEX data buffer
#define HEX_BUFFER_SIZE (131072)
static char hex_buffer[HEX_BUFFER_SIZE];
static uint32_t hex_pos = 0;

/* Choose 'C' for Celsius or 'F' for Fahrenheit. */
#define TEMPERATURE_UNITS 'C'
static const char *voltage_names[] = {
	"0.55", "0.60", "0.65", "0.70", "0.75", "0.80", "0.85", "0.90", "0.95", "1.00", "1.05",
	"1.10", "1.15", "1.20", "1.25", "1.30", "1.35", "1.40", "1.50", "1.60", "1.65", "1.70",
	"1.80", "1.90", "2.00", "2.35", "2.50", "2.65", "2.80", "3.00", "3.15", "3.30",
};

static inline const char *core_voltage_str(void)
{
	return voltage_names[vreg_get_voltage()];
}

// Command tokenization
#define MAX_TOKENS 32
static char *cmd_tokens[MAX_TOKENS];
static int cmd_token_count = 0;

// Command handler function type
typedef void (*cmd_handler_fn)(void);

// Command table entry
typedef struct command_entry_t {
	const char *name;
	cmd_handler_fn handler;
	bool needs_args;     // True if command needs remaining tokens as arguments
	bool pause_emulator; // True if we must pause the emulator while running the handler
} command_entry_t;

bool send_command_to_emulator(sm_event_t event)
{
	sm_notification_t notification;
	printf("sending event %d (%s) to emulator\r\n", event, event_names[event]);
	if (!post_sm_event(event)) {
		usb_cdc_send("ERROR: Failed to send command to emulator\r\n");
		return false; // queue full
	}
	uint32_t tries = 0;
	while (!receive_sm_notification(&notification)) {
		sleep_ms(1);
		tries++;
		if (tries > 100) {
			usb_cdc_printf("Timeout waiting for notification after event %d\r\n",
				       event);
			return false;
		}
	}
	printf("Got notification %s after %ld tries\r\n", notification == NOTIF_OK ? "OK" : "ERROR",
	       tries);
	return notification == NOTIF_OK;
}

static inline bool pause_emulator(void)
{
	return send_command_to_emulator(EV_PAUSE_EMULATOR);
}

static inline bool resume_emulator(void)
{
	return send_command_to_emulator(EV_RESUME_EMULATOR);
}

static uint8_t bus_read_with_eclock(uint16_t address)
{
	// Implement single-byte read using block version for consistency
	uint8_t buffer[1];
	bus_read_block_with_eclock(address, 1, buffer);
	return buffer[0];
}

static void bus_write_with_eclock(uint16_t address, uint8_t value)
{
	// Implement single-byte write using block version for consistency
	bus_write_block_with_eclock(address, &value, 1);
}

/* References for this implementation:
 * raspberry-pi-pico-c-sdk.pdf, Section '4.1.1. hardware_adc'
 * pico-examples/adc/adc_console/adc_console.c */
static float read_onboard_temperature(const char unit)
{
	adc_select_input(8); // RP2350 80 pin

	/* 12-bit conversion, assume max value == ADC_VREF == 3.3 V */
	const float conversionFactor = 3.3f / (1 << 12);

	float adc = (float)adc_read() * conversionFactor;
	float tempC = 27.0f - (adc - 0.706f) / 0.001721f;

	if (unit == 'C') {
		return tempC;
	} else if (unit == 'F') {
		return tempC * 9 / 5 + 32;
	}

	return -1.0f;
}

static float average_temperature(const char unit, uint ntimes)
{
	float avg = 0.0;
	uint n = ntimes;
	while (n-- > 0) {
		avg += read_onboard_temperature(unit);
	}
	return avg / ntimes;
}

void init_temperature_sensor(void)
{
	adc_init();
	adc_set_temp_sensor_enabled(true);
}

//--------------------------------------------------------------------+
// Command Handler Functions
//--------------------------------------------------------------------+

static void cmd_load(void)
{
	if (!send_command_to_emulator(EV_CMD_LOAD)) {
		return;
	}
	// Enter HEX load mode
	in_hex_mode = true;
	hex_pos = 0;
	usb_cdc_send("Ready to receive Intel HEX data. Paste file now...\r\n");
}

static void cmd_end(void)
{
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

static void cmd_config_show(void)
{
	// Display current memory configuration
	memory_print_summary(usb_cdc_printf);
	usb_cdc_printf("  Debug SPI: %s\r\n", debug_spi_is_enabled() ? "ON" : "OFF");
}

static void cmd_map_rom(void)
{
	// Configure ROM region: config rom <base> <size>
	// Expects tokens: [config] [rom] <base> <size>
	if (cmd_token_count < 2) {
		usb_cdc_send("ERROR: Usage: map rom <base_hex> <size_hex>\r\n");
		return;
	}

	unsigned int base, size;
	if (sscanf(cmd_tokens[0], "%x", &base) == 1 && sscanf(cmd_tokens[1], "%x", &size) == 1) {
		memory_configure_rom(base, size);
		usb_cdc_printf("OK: ROM configured at $%04X, size $%04X\r\n", base, size);
		if (!copy_rom_contents_from_bus(usb_cdc_printf)) {
			usb_cdc_send("ERROR: Failed to copy ROM contents from flash\r\n");
		}
	} else {
		usb_cdc_send("ERROR: Usage: map rom <base_hex> <size_hex>\r\n");
	}
}

static void cmd_map_ram(void)
{
	// Configure RAM region: config ram <base> <size>
	// Expects tokens: [config] [ram] <base> <size>
	if (cmd_token_count < 2) {
		usb_cdc_send("ERROR: Usage: map ram <base_hex> <size_hex>\r\n");
		return;
	}

	unsigned int base, size;
	if (sscanf(cmd_tokens[0], "%x", &base) == 1 && sscanf(cmd_tokens[1], "%x", &size) == 1) {
		memory_configure_ram(base, size);
		usb_cdc_printf("OK: RAM configured at $%04X, size $%04X\r\n", base, size);
	} else {
		usb_cdc_send("ERROR: Usage: map ram <base_hex> <size_hex>\r\n");
	}
}

static void cmd_run(void)
{
	// Start CPU execution
	if (!send_command_to_emulator(EV_CMD_RUN)) {
		return;
	}
	usb_cdc_send("OK: CPU started\r\n");
}

static void cmd_halt(void)
{
	// Stop CPU execution
	if (!send_command_to_emulator(EV_CMD_HALT)) {
		return;
	}
	usb_cdc_send("OK: CPU halted\r\n");
}

static void cmd_reset(void)
{
	if (!send_command_to_emulator(EV_CMD_RESET)) {
		return;
	}
	usb_cdc_send("OK: CPU reset\r\n");
}

static void cmd_bootloader(void)
{
	// Enter bootloader mode
	usb_cdc_send("Entering bootloader mode...\r\n");
	sleep_ms(100);        // Give time for message to send
	reset_usb_boot(0, 0); // Reset into USB bootloader
}

static void cmd_debug_on(void)
{
	// Enable debug SPI output
	debug_spi_enable(true);
	usb_cdc_send("OK: Debug SPI enabled\r\n");
}

static void cmd_debug_off(void)
{
	// Disable debug SPI output
	debug_spi_enable(false);
	usb_cdc_send("OK: Debug SPI disabled\r\n");
}

static void cmd_clock_internal(void)
{
	if (eclock_is_running()) {
		usb_cdc_send("ERROR: Stop CPU first (halt command)\r\n");
		return;
	}
	eclock_set_mode(ECLOCK_INTERNAL);
	usb_cdc_send("OK: E clock set to internal (PIO generated)\r\n");
}

static void cmd_clock_external(void)
{
	if (eclock_is_running()) {
		usb_cdc_send("ERROR: Stop CPU first (halt command)\r\n");
		return;
	}
	eclock_set_mode(ECLOCK_EXTERNAL);
	usb_cdc_send("OK: E clock set to external (input from target)\r\n");
}

static void cmd_clock_test(void)
{
	if (eclock_is_running()) {
		usb_cdc_send("ERROR: Stop CPU first (halt command)\r\n");
		return;
	}

	usb_cdc_printf("E clock output GPIO: %d\r\n", GPIO_ECLOCK);
	usb_cdc_printf("E clock input GPIO:  %d\r\n", GPIO_ECLOCK_IN);
	usb_cdc_printf("Current mode: %s\r\n",
		       eclock_get_mode() == ECLOCK_INTERNAL ? "internal" : "external");

	// Test 1: Direct GPIO sampling on GPIO_ECLOCK (through level shifter)
	usb_cdc_send("\r\nTest 1: GPIO_ECLOCK (output pin) sampling...\r\n");

	gpio_init(GPIO_ECLOCK);
	gpio_set_dir(GPIO_ECLOCK, GPIO_IN);
	gpio_set_input_enabled(GPIO_ECLOCK, true);
	gpio_set_pulls(GPIO_ECLOCK, false, false);

	usb_cdc_printf("  GPIO%d CTRL: 0x%08lX\r\n", GPIO_ECLOCK, iobank0_hw->io[GPIO_ECLOCK].ctrl);
	usb_cdc_printf("  PAD CTRL: 0x%08lX\r\n", padsbank0_hw->io[GPIO_ECLOCK]);

	uint32_t transitions = 0;
	bool last_state = gpio_get(GPIO_ECLOCK);
	absolute_time_t end_time = make_timeout_time_us(100);

	while (!time_reached(end_time)) {
		bool state = gpio_get(GPIO_ECLOCK);
		if (state != last_state) {
			transitions++;
			last_state = state;
		}
	}
	usb_cdc_printf("  Transitions in 100us: %lu (expected ~178)\r\n", transitions);

	// Test 2: Direct GPIO sampling on GPIO_ECLOCK_IN (direct input)
	usb_cdc_send("\r\nTest 2: GPIO_ECLOCK_IN (input pin) sampling...\r\n");

	gpio_init(GPIO_ECLOCK_IN);
	gpio_set_dir(GPIO_ECLOCK_IN, GPIO_IN);
	gpio_set_input_enabled(GPIO_ECLOCK_IN, true);
	gpio_set_pulls(GPIO_ECLOCK_IN, false, false);

	usb_cdc_printf("  GPIO%d CTRL: 0x%08lX\r\n", GPIO_ECLOCK_IN,
		       iobank0_hw->io[GPIO_ECLOCK_IN].ctrl);
	usb_cdc_printf("  PAD CTRL: 0x%08lX\r\n", padsbank0_hw->io[GPIO_ECLOCK_IN]);

	transitions = 0;
	last_state = gpio_get(GPIO_ECLOCK_IN);
	end_time = make_timeout_time_us(100);

	while (!time_reached(end_time)) {
		bool state = gpio_get(GPIO_ECLOCK_IN);
		if (state != last_state) {
			transitions++;
			last_state = state;
		}
	}
	usb_cdc_printf("  Transitions in 100us: %lu (expected ~178)\r\n", transitions);

	// Test 3: PIO external clock counting using GPIO_ECLOCK_IN
	usb_cdc_send("\r\nTest 3: PIO external clock program (100us window)...\r\n");

	// Save current mode and switch to external
	eclock_mode_t saved_mode = eclock_get_mode();
	eclock_set_mode(ECLOCK_EXTERNAL);
	eclock_reset_pio_counter();

	// Read initial value
	uint32_t initial = eclock_get_pio_cycles();
	usb_cdc_printf("  Initial PIO cycle count: %lu\r\n", initial);

	// Start PIO and wait
	pio_sm_set_enabled(ECLK_PIO, E_SM, true);
	busy_wait_us(100);

	// Read final value
	uint32_t final = eclock_get_pio_cycles();
	pio_sm_set_enabled(ECLK_PIO, E_SM, false);

	usb_cdc_printf("  Final PIO cycle count: %lu\r\n", final);
	usb_cdc_printf("  Cycles counted: %lu\r\n", final - initial);
	usb_cdc_printf("  Expected at 894kHz: ~89 cycles\r\n");

	// Test 4: Check PIO state machine state
	usb_cdc_send("\r\nTest 4: PIO state machine debug info...\r\n");
	usb_cdc_printf("  PIO SM enabled: %s\r\n", (ECLK_PIO->ctrl & (1 << E_SM)) ? "yes" : "no");
	usb_cdc_printf("  PIO FSTAT: 0x%08lX\r\n", ECLK_PIO->fstat);
	usb_cdc_printf("  PIO FDEBUG: 0x%08lX\r\n", ECLK_PIO->fdebug);

	// Restore original mode
	eclock_set_mode(saved_mode);

	usb_cdc_send("\r\nTest complete.\r\n");
}

static void cmd_bus_info(void)
{
	// Bus information
	usb_cdc_printf("Board: %s\r\n", BOARD_NAME);
	usb_cdc_printf("Address Lines: %d\r\n", ADDR_LINES);
	usb_cdc_printf("Address Mask: 0x%04X\r\n", ADDR_MASK);
	usb_cdc_printf("Max Address: 0x%04X\r\n", MAX_ADDRESS);
	usb_cdc_printf("Address Space: %d bytes\r\n", ADDR_SPACE_SIZE);
	usb_cdc_send("Bus Interface: Cycle-accurate E-clock synchronized\r\n");
}

static void cmd_status(void)
{
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
	uint32_t eclock_pio_cycles =
		eclock_is_running() ? eclock_get_pio_cycles() : last_pio_cycles;

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
	usb_cdc_printf("QSPI Bus: %lu MHz (divisor: %d)\r\n", qspi_freq_hz / 1000000,
		       QSPI_CLOCK_DIVISOR);

	// Include temperature information
	usb_cdc_printf("Clock: %luMHz, voltage: %sV, Temperature: %.1fºC\r\n",
		       sys_clock_hz / 1000000, core_voltage_str(),
		       average_temperature(TEMPERATURE_UNITS, 10));

	if (bus_read_reset()) {
		usb_cdc_printf("  /RESET Pin asserted\r\n");
	}
	if (bus_read_nmi()) {
		usb_cdc_printf("  /NMI Pin asserted\r\n");
	}
	if (bus_read_irq()) {
		usb_cdc_printf("  /IRQ Pin asserted\r\n");
	}
	usb_cdc_printf("  E Clock: %s (%s)\r\n", eclock_is_running() ? "running" : "stopped",
		       eclock_get_mode() == ECLOCK_INTERNAL ? "internal" : "external");
}

static void cmd_read(void)
{
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

		bool started_eclock = false;
		if (memory_range_has_unmapped(addr, len)) {
			// Temporarily start E clock if needed
			if (!eclock_is_running()) {
				eclock_start();
				started_eclock = true;
			}
		}

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

		if (started_eclock) {
			eclock_stop();
		}
	}
}

static void cmd_write(void)
{
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

	for (uint32_t i = 0; i < count; i++) {
		memory_write_fast(addr + i, buffer[i]);
	}

	usb_cdc_send("OK\r\n");
}

static void cmd_break_set(void)
{
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

static void cmd_break_clear(void)
{
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

static void cmd_break_list(void)
{
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

static void cmd_reg_pc(void)
{
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

static void cmd_reg_a(void)
{
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

static void cmd_reg_b(void)
{
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

static void cmd_reg_x(void)
{
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

static void cmd_reg_sp(void)
{
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

static void cmd_reg_ccr(void)
{
	// Set condition code register: reg ccr <val>
	// Expects tokens: [reg] [ccr] <val>
	if (cmd_token_count < 1) {
		usb_cdc_send("ERROR: Usage: reg ccr <value_hex>\r\n");
		return;
	}

	unsigned int value;
	if (sscanf(cmd_tokens[0], "%x", &value) == 1) {
		cpu.ccr = ((uint8_t)value & 0x3F) | CCR_FIXED; // Preserve bits 7-6
		usb_cdc_printf("OK: CCR set to $%02X\r\n", cpu.ccr);
	} else {
		usb_cdc_send("ERROR: Invalid register value\r\n");
	}
}

static void cmd_bus_read(void)
{
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

static void cmd_bus_write(void)
{
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

static void cmd_bus_read_block(void)
{
	// Bus read block: bus_read_block <address> <length>
	// Expects tokens: [bus_read_block] <address> <length>
	if (cmd_token_count < 2) {
		usb_cdc_send("ERROR: Usage: bus_read_block <address_hex> <length_hex>\r\n");
		return;
	}

	unsigned int address, length;
	if (sscanf(cmd_tokens[0], "%x", &address) != 1 ||
	    sscanf(cmd_tokens[1], "%x", &length) != 1) {
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
		uint8_t buffer[1024]; // Max block size
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

static void cmd_bus_write_block(void)
{
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
static void cmd_print_instruction_counts(void)
{
	instruction_count_report(usb_cdc_printf);
}

static void cmd_reset_instruction_counts(void)
{
	bool old = instruction_count_enable(false);
	instruction_count_initialize();
	instruction_count_enable(old);
	cpu.instruction_count = 0; // Reset instruction counter
	clock_reset_counters();
	usb_cdc_printf("OK: Instruction counts reset (counting: %d)\r\n", old);
}

static void cmd_count_on(void)
{
	instruction_count_enable(true);
	usb_cdc_printf("OK: Instruction counting enabled\r\n");
}

static void cmd_count_off(void)
{
	instruction_count_enable(false);
	usb_cdc_printf("OK: Instruction counting disabled\r\n");
}
#endif

// Helper function to format memory info string
static const char *format_memory_info(memory_type_t type)
{
	switch (type) {
	case MEM_TYPE_ROM:
		return "ROM";
	case MEM_TYPE_RAM:
		return "RAM";
	case MEM_TYPE_UNMAPPED:
		return "UNMAPPED";
	default:
		return "UNKNOWN";
	}
}

static void cmd_map_show(void)
{
	// Reload memory map from flash to ensure it's not corrupted
	memory_load_memory_map_from_flash();

	// Display memory mapping state with contiguous ranges
	// Only show unique address space based on decoded bits
	uint32_t end_addr = (1 << mem_config.decoded_bits) - 1;
	usb_cdc_printf("Memory Map ($0000-$%04lX, %d bits decoded):\r\n", end_addr,
		       mem_config.decoded_bits);

	uint32_t current_addr = 0x0000;

	while (current_addr <= end_addr) {
		memory_type_t type = memory_get_type((uint16_t)current_addr);
		const char *info = format_memory_info(type);

		// Find the end of this contiguous range
		uint32_t range_end = current_addr;
		while (range_end + 1 <= end_addr) {
			memory_type_t next_type = memory_get_type((uint16_t)(range_end + 1));
			const char *next_info = format_memory_info(next_type);

			if (strcmp(info, next_info) == 0) {
				range_end++;
			} else {
				break;
			}
		}

		usb_cdc_printf("  $%04X-$%04X: %s\r\n", (uint16_t)current_addr, (uint16_t)range_end,
			       info);
		current_addr = range_end + 1;
	}
}

static void cmd_map_clear(void)
{
	send_command_to_emulator(EV_CMD_HALT);
	memory_clear_map();
	usb_cdc_send("OK: All memory pages unmapped\r\n");
	send_command_to_emulator(EV_CMD_RESET);
}

// Helper function to generate a single Intel HEX record
static void generate_ihex_record(uint16_t address, uint8_t *data, uint8_t byte_count, uint8_t record_type)
{
	// Calculate checksum (two's complement of sum of all bytes)
	uint8_t checksum = byte_count;
	checksum += (address >> 8) & 0xFF;
	checksum += address & 0xFF;
	checksum += record_type;
	for (int i = 0; i < byte_count; i++) {
		checksum += data[i];
	}
	checksum = (~checksum + 1) & 0xFF; // Two's complement

	// Output record: :LLAAAATTDD...DDCC
	usb_cdc_printf(":%02X%04X%02X", byte_count, address, record_type);
	for (int i = 0; i < byte_count; i++) {
		usb_cdc_printf("%02X", data[i]);
	}
	usb_cdc_printf("%02X\r\n", checksum);
}

static void cmd_download(void)
{
	// Download memory as Intel HEX: download <addr> <len>
	// Expects tokens: [download] <addr> <len>
	if (cmd_token_count < 2) {
		usb_cdc_send("ERROR: Usage: download <addr_hex> <len_hex>\r\n");
		return;
	}

	unsigned int addr, len;
	if (sscanf(cmd_tokens[0], "%x", &addr) != 1 || sscanf(cmd_tokens[1], "%x", &len) != 1) {
		usb_cdc_send("ERROR: Usage: download <addr_hex> <len_hex>\r\n");
		return;
	}

	if (addr > MAX_ADDRESS) {
		usb_cdc_send("ERROR: Address out of range\r\n");
		return;
	}
	if (len == 0 || len > 65536) {
		usb_cdc_send("ERROR: Length must be 1-65536\r\n");
		return;
	}
	if (addr + len > MAX_ADDRESS + 1) {
		usb_cdc_send("ERROR: Block exceeds address space\r\n");
		return;
	}

	usb_cdc_printf("Downloading $%04X bytes from $%04X as Intel HEX:\r\n", len, addr);

	bool started_eclock = false;
	if (memory_range_has_unmapped(addr, len)) {
		// Temporarily start E clock if needed for bus access
		if (!eclock_is_running()) {
			eclock_start();
			started_eclock = true;
		}
	}

	// Generate Intel HEX data records (16 bytes per record is standard)
	#define BYTES_PER_RECORD 16
	uint8_t buffer[BYTES_PER_RECORD];
	uint32_t remaining = len;
	uint16_t current_addr = addr;

	while (remaining > 0) {
		uint8_t bytes_this_record = (remaining >= BYTES_PER_RECORD) ? BYTES_PER_RECORD : remaining;

		// Read data from memory
		for (uint8_t i = 0; i < bytes_this_record; i++) {
			buffer[i] = memory_read_fast(current_addr + i);
		}

		// Generate and output the record
		generate_ihex_record(current_addr, buffer, bytes_this_record, IHEX_TYPE_DATA);

		current_addr += bytes_this_record;
		remaining -= bytes_this_record;
	}

	// Output EOF record
	generate_ihex_record(0x0000, NULL, 0, IHEX_TYPE_EOF);

	if (started_eclock) {
		eclock_stop();
	}

	usb_cdc_printf("OK: Downloaded %d bytes\r\n", len);
}

static void cmd_checksum(void)
{
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
		// Compute checksum (modulo 64k sum of bytes)
		uint32_t sum = 0;
		for (uint32_t i = 0; i < len; i++) {
			uint8_t value = memory_read_fast(addr + i);
			sum += value;
		}
		uint16_t checksum = sum & 0xFFFF; // Modulo 64k

		usb_cdc_printf("Checksum of $%04X bytes from $%04X: $%04X\r\n", len, addr,
			       checksum);
	}
}

static void cmd_scan_memory(void)
{
	// Scan memory and auto-configure memory map
	usb_cdc_send("Scanning target system memory...\r\n");

	send_command_to_emulator(EV_CMD_HALT);
	if (memory_scan_and_build_map(printf)) {
		// Show configuration
		usb_cdc_printf("\r\nMemory Configuration:\r\n");
		usb_cdc_printf("  ROM: $%04X-$%04X (%d bytes)\r\n", mem_config.rom_base,
			       mem_config.rom_base + mem_config.rom_size - 1, mem_config.rom_size);
		usb_cdc_printf("  RAM: $%04X-$%04X (%d bytes)\r\n", mem_config.ram_base,
			       mem_config.ram_base + mem_config.ram_size - 1, mem_config.ram_size);
		usb_cdc_printf("  Architecture: %s (decoded %d bits)\r\n",
			       architecture_name(mem_config.architecture), mem_config.decoded_bits);

		usb_cdc_send("\r\nMemory map and ROM contents have been saved to flash.\r\n");
		usb_cdc_send("Configuration will be used automatically on next boot.\r\n");
		usb_cdc_send("Target system can now be removed - emulator will run from stored "
			     "ROM.\r\n");

		send_command_to_emulator(EV_CMD_RESET);
	} else {
		usb_cdc_send("ERROR: Failed to scan memory\r\n");
	}
}

static void cmd_verify_memory(void)
{
	// Verify memory configuration against hardware
	usb_cdc_send("Verifying memory configuration...\r\n");

	sanity_result_t result = memory_sanity_check();

	switch (result) {
	case SANITY_OK:
		usb_cdc_send("OK: Memory configuration matches hardware\r\n");
		break;
	case SANITY_RAM_MISMATCH:
		usb_cdc_send("ERROR: RAM mismatch - run 'scan_memory' to update\r\n");
		break;
	case SANITY_ROM_UNEXPECTED:
		usb_cdc_send(
			"WARNING: ROM/RAM differs from saved - run 'scan_memory' to update\r\n");
		break;
	case SANITY_NO_SAVED_MAP:
		usb_cdc_send("ERROR: No saved memory map - run 'scan_memory' first\r\n");
		break;
	}
}

static void cmd_help(void)
{
	// Send as single string to avoid buffer overflow
	usb_cdc_send("MC6800 Emulator Commands (all numbers in hex):\r\n"
		     "  load                      - Load Intel HEX\r\n"
		     "  end                       - End Intel HEX\r\n"
		     "  config                    - Show memory configuration\r\n"
		     "  checksum <addr> <len>     - Calculate checksum of memory range\r\n"
		     "  download <addr> <len>     - Download memory as Intel HEX\r\n"
		     "  read <addr> <len>         - Read memory from shadow or bus\r\n"
		     "  readb <addr> <len>        - Read data from hardware bus\r\n"
		     "  write <addr> <data>       - Write memory to shadow or bus\r\n"
		     "  writeb <addr> <data...>   - Write data to hardware bus\r\n"
		     "  status                    - Display CPU status\r\n"
		     "  run                       - Start CPU execution\r\n"
		     "  halt                      - Stop CPU execution\r\n"
		     "  reset                     - Reset CPU\r\n"
		     "  map show                  - Show ROM/RAM mapping state\r\n"
		     "  map clear                 - Clear all ROM/RAM mappings\r\n"
		     "  map rom <b> <s>           - Configure ROM region\r\n"
		     "  map ram <b> <s>           - Configure RAM region\r\n"
		     "  scan_memory               - Auto-detect and configure memory map\r\n"
		     "  verify_memory             - Verify memory configuration\r\n"
#if COUNT_INSTRUCTIONS
		     "  count print               - Print instruction execution counts\r\n"
		     "  count reset               - Reset instruction execution counts\r\n"
		     "  count on                  - Enable instruction counting\r\n"
		     "  count off                 - Disable instruction counting\r\n"
#endif
		     "  debug on/off              - Enable/disable SPI debug output\r\n"
		     "  clock internal            - Use PIO-generated E clock (default)\r\n"
		     "  clock external            - Use external E clock input\r\n"
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
		     "  bus_info                  - Show bus configuration\r\n"
		     "  bootloader                - Enter bootloader mode\r\n"
		     "  help                      - Show this help\r\n");
}

//--------------------------------------------------------------------+
// Command Tokenizer and Dispatcher
//--------------------------------------------------------------------+

static int tokenize_command(char *cmd)
{
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
	{"debug on", cmd_debug_on, false, false},
	{"debug off", cmd_debug_off, false, false},
	{"clock internal", cmd_clock_internal, false, false},
	{"clock external", cmd_clock_external, false, false},
	{"clock test", cmd_clock_test, false, false},
	{"break clear", cmd_break_clear, true, false}, // needs optional <addr>
	{"break list", cmd_break_list, false, false},
	{"reg pc", cmd_reg_pc, true, true},   // needs <value>
	{"reg a", cmd_reg_a, true, true},     // needs <value>
	{"reg b", cmd_reg_b, true, true},     // needs <value>
	{"reg x", cmd_reg_x, true, true},     // needs <value>
	{"reg sp", cmd_reg_sp, true, true},   // needs <value>
	{"reg ccr", cmd_reg_ccr, true, true}, // needs <value>
	{"map show", cmd_map_show, false, false},
	{"map clear", cmd_map_clear, false, false},
	{"map ram", cmd_map_ram, true, true}, // needs <base> <size>
	{"map rom", cmd_map_rom, true, true}, // needs <base> <size>

#if COUNT_INSTRUCTIONS
	{"count print", cmd_print_instruction_counts, false, false},
	{"count reset", cmd_reset_instruction_counts, false, false},
	{"count on", cmd_count_on, false, false},
	{"count off", cmd_count_off, false, false},
#endif

	// Single-word commands
	{"load", cmd_load, false, false},
	{"end", cmd_end, false, false},
	{"config", cmd_config_show, false, false},
	{"checksum", cmd_checksum, true, true}, // needs <addr> <len>
	{"download", cmd_download, true, true}, // needs <addr> <len>
	{"read", cmd_read, true, true},         // needs <addr> <len>
	{"write", cmd_write, true, true},       // needs <addr> <data...>
	{"status", cmd_status, false, false},
	{"run", cmd_run, false, false},
	{"halt", cmd_halt, false, false},
	{"reset", cmd_reset, false, false},
	{"bootloader", cmd_bootloader, false, false},
	{"boot", cmd_bootloader, false, false},      // Alias for bootloader
	{"break", cmd_break_set, true, false},       // needs <addr>
	{"readb", cmd_bus_read_block, true, true},   // needs <addr> <len>
	{"writeb", cmd_bus_write_block, true, true}, // needs <addr> <data...>
	{"bus_write", cmd_bus_write, true, true},    // needs <addr> <data>
	{"bus_read", cmd_bus_read, true, true},      // needs <addr>
	{"bus_info", cmd_bus_info, false, false},
	{"scan_memory", cmd_scan_memory, false, false},
	{"verify_memory", cmd_verify_memory, false, true},
	{"help", cmd_help, false, false},
	{NULL, NULL, false, false} // Terminator
};

static void dispatch_command(void)
{
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

			printf("--- Start Command %s\r\n", command_table[i].name);
			if (command_table[i].pause_emulator) {
				if (!pause_emulator()) {
					return;
				}
			}

			printf("running handler\r\n");

			// for debugger (JLink reset type == 12)
			jlink_reset_strategy_12_marker();

			// Call the handler
			command_table[i].handler();

			if (command_table[i].pause_emulator) {
				resume_emulator();
			}
			printf("--- End Command %s\r\n", command_table[i].name);
			return;
		}
	}

	// No match found
	usb_cdc_send("ERROR: Unknown command. Type 'help' for help.\r\n");
}

// Process a complete command line
static void process_command(char *cmd)
{
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
void usb_cdc_init(void)
{
	init_temperature_sensor();

	// Initialize TinyUSB device stack
	tusb_init();

	cmd_pos = 0;
	in_hex_mode = false;
	printf("USB CDC interface initialized\n");
}

// Process USB events
void usb_cdc_task(void)
{
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
				if (hex_pos >= 4 &&
				    strncmp(&hex_buffer[hex_pos - 4], "end", 3) == 0) {
					// Remove 'end' from buffer and process
					hex_pos -= 4;
					process_command("end");
				}
				// Check for Intel HEX EOF record (:00000001FF)
				else if (hex_pos >= 12 && strncmp(&hex_buffer[hex_pos - 12],
								  ":00000001FF", 11) == 0) {
					// Found EOF record - auto-exit hex mode and process
					usb_cdc_send(
						"EOF record detected, processing HEX data...\r\n");
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
void usb_cdc_send(const char *str)
{
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
int usb_cdc_printf(const char *restrict fmt, ...)
{
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
void tud_mount_cb(void)
{
	printf("USB mounted\n");
}

// Invoked when device is unmounted
void tud_umount_cb(void)
{
	printf("USB unmounted\n");
}

// Invoked when CDC line state changes (DTR/RTS)
void tud_cdc_line_state_cb(uint8_t itf, bool dtr, bool rts)
{
	(void)itf;
	(void)rts;

	if (dtr) {
		printf("USB CDC connected\n");
	} else {
		printf("USB CDC disconnected\n");
	}
}
