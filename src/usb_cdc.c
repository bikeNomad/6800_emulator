/**
 * USB CDC Interface Implementation
 */

#include "usb_cdc.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "debug_spi.h"
#include "emu_api.h"
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
#include <stdlib.h>

// Command buffer
#define CMD_BUFFER_SIZE 4096
static char cmd_buffer[CMD_BUFFER_SIZE];
static uint32_t cmd_pos = 0;
static bool in_hex_mode = false;

// HEX data buffer (256KB to handle large ROM files)
#define HEX_BUFFER_SIZE (262144)
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
	bool needs_args;    // True if command needs remaining tokens as arguments
	bool lock_emulator; // True if the emulator core must be parked (emu_lock) during the
			    // handler
} command_entry_t;

// Report an API error (if any) to the console. Returns true on success.
static bool check(emu_result_t r)
{
	if (r != EMU_OK) {
		usb_cdc_printf("ERROR: %s\r\n", emu_result_str(r));
		return false;
	}
	return true;
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
	if (!check(emu_load_begin())) {
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

	check(emu_reset());
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
	if (!check(emu_run())) {
		return;
	}
	usb_cdc_send("OK: CPU started\r\n");
}

static void cmd_halt(void)
{
	// Stop CPU execution
	if (!check(emu_halt())) {
		return;
	}
	usb_cdc_send("OK: CPU halted\r\n");
}

static void cmd_reset(void)
{
	if (!check(emu_reset())) {
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
	// Enable debug SPI output (not while trig actions use its pins)
	emu_result_t r = emu_debug_spi_enable(true);
	if (r == EMU_ERR_BUSY) {
		usb_cdc_send("ERROR: trig actions use GPIO 33-36; remove them first ('trig list')\r\n");
		return;
	}
	if (check(r)) {
		usb_cdc_send("OK: Debug SPI enabled\r\n");
	}
}

static void cmd_debug_off(void)
{
	// Disable debug SPI output
	(void)emu_debug_spi_enable(false);
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

// Print registers in the format the web interface parses ("PC: $xxxx", ...).
// PC is the next instruction to execute; "Last" is the one just executed.
static void print_regs(const emu_regs_t *r)
{
	usb_cdc_printf("  PC: $%04X  (%s)\r\n", r->pc, cpu_mnemonic_at(r->pc));
	usb_cdc_printf("  Last: $%04X  (%s)\r\n", r->prev_pc, cpu_mnemonic_at(r->prev_pc));
	usb_cdc_printf("  A:  $%02X\r\n", r->a);
	usb_cdc_printf("  B:  $%02X\r\n", r->b);
	usb_cdc_printf("  X:  $%04X\r\n", r->x);
	usb_cdc_printf("  SP: $%04X\r\n", r->sp);
	usb_cdc_printf("  CCR: $%02X [%c%c%c%c%c%c]\r\n", r->ccr, (r->ccr & CCR_H) ? 'H' : '-',
		       (r->ccr & CCR_I) ? 'I' : '-', (r->ccr & CCR_N) ? 'N' : '-',
		       (r->ccr & CCR_Z) ? 'Z' : '-', (r->ccr & CCR_V) ? 'V' : '-',
		       (r->ccr & CCR_C) ? 'C' : '-');
}

static void cmd_regs(void)
{
	emu_regs_t regs;
	if (check(emu_get_regs(&regs))) {
		print_regs(&regs);
	}
}

static void cmd_step(void)
{
	// step [count]
	unsigned int count = 1;
	if (cmd_token_count >= 1 && (sscanf(cmd_tokens[0], "%x", &count) != 1 || count == 0)) {
		usb_cdc_send("ERROR: Usage: step [count_hex]\r\n");
		return;
	}
	// Completion is reported by the HALTED/STEP_DONE event
	check(emu_step(count));
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

	// Take a consistent snapshot first, then print (printing is slow and
	// the emulator should not stay parked for it).
	emu_status_t st;
	if (!check(emu_get_status(&st))) {
		return;
	}
	uint32_t cycle_cnt = st.cycle_count;
	uint32_t eclock_pio_cycles = st.pio_cycles;

	usb_cdc_printf("Emulator State: %s\r\n", emu_state_name(st.state));
	usb_cdc_printf("CPU Status:\r\n");
	print_regs(&st.regs);
	usb_cdc_printf("  Running: %s\r\n", st.running ? "YES" : "NO");
	usb_cdc_printf("  Halted: %s\r\n", st.halted ? "YES" : "NO");
	if (st.steps_pending) {
		usb_cdc_printf("  Steps pending: %lu\r\n", st.steps_pending);
	}
	usb_cdc_printf("  Instructions: %llu\r\n", (unsigned long long)st.instruction_count);
	usb_cdc_printf("  Cycle Count: %lu\r\n", cycle_cnt);
	usb_cdc_printf("  PIO Cycles: %lu\r\n", eclock_pio_cycles);
	usb_cdc_printf("  Overage: %ld\r\n", (long)st.cycle_overage);
	usb_cdc_printf("  Underage: %ld\r\n", (long)st.cycle_underage);
	usb_cdc_printf("  Speed ratio: %fx\r\n",
		       (cycle_cnt + (int32_t)st.cycle_underage) /
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
	usb_cdc_printf("  E Clock: %s (%s)\r\n", st.eclock_running ? "running" : "stopped",
		       st.eclock_external ? "external" : "internal");
	uint32_t drops = emu_events_dropped();
	if (drops) {
		usb_cdc_printf("  Events dropped: %lu\r\n", drops);
	}
}

static void hex_dump(uint16_t addr, const uint8_t *buf, uint32_t len)
{
	for (uint32_t i = 0; i < len; i++) {
		if (i % 16 == 0) {
			usb_cdc_printf("%04X: ", (unsigned)(addr + i));
		}
		usb_cdc_printf("%02X ", buf[i]);
		if (i % 16 == 15 || i == len - 1) {
			usb_cdc_send("\r\n");
		}
	}
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
		// static: core 1 has only a 2 KiB stack
		static uint8_t buffer[1024];
		if (!check(emu_mem_read((uint16_t)addr, buffer, len))) {
			return;
		}
		usb_cdc_printf("Reading $%04X bytes from $%04X:\r\n", len, addr);
		hex_dump((uint16_t)addr, buffer, len);
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
	// static: core 1 has only a 2 KiB stack
	static uint8_t buffer[1024]; // Max block size
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

	if (check(emu_mem_write((uint16_t)addr, buffer, count))) {
		usb_cdc_send("OK\r\n");
	}
}

static void cmd_break_set(void)
{
	// Set breakpoint: break <addr>
	// Expects tokens: [break] <addr>
	unsigned int addr;
	if (cmd_token_count < 1 || sscanf(cmd_tokens[0], "%x", &addr) != 1 || addr > MAX_ADDRESS) {
		usb_cdc_send("ERROR: Usage: break <addr_hex>\r\n");
		return;
	}
	emu_result_t r = emu_bp_add((uint16_t)addr);
	if (r == EMU_OK) {
		usb_cdc_printf("OK: Breakpoint set at $%04X\r\n", addr);
	} else if (r == EMU_ERR_FULL) {
		usb_cdc_printf("ERROR: Failed to set breakpoint at $%04X (max %u breakpoints)\r\n",
			       addr, (unsigned)emu_bp_max());
	} else if (r == EMU_ERR_EXISTS) {
		usb_cdc_printf("ERROR: Breakpoint already set at $%04X\r\n", addr);
	} else {
		check(r);
	}
}

static void cmd_break_clear(void)
{
	// Clear breakpoint(s): break clear [<addr>]
	// Expects tokens: [<addr>]
	if (cmd_token_count == 0) {
		if (check(emu_bp_clear())) {
			usb_cdc_send("OK: All breakpoints cleared\r\n");
		}
		return;
	}
	unsigned int addr;
	if (sscanf(cmd_tokens[0], "%x", &addr) != 1 || addr > MAX_ADDRESS) {
		usb_cdc_send("ERROR: Usage: break clear <addr_hex>\r\n");
		return;
	}
	emu_result_t r = emu_bp_remove((uint16_t)addr);
	if (r == EMU_OK) {
		usb_cdc_printf("OK: Breakpoint at $%04X cleared\r\n", addr);
	} else if (r == EMU_ERR_NOT_FOUND) {
		usb_cdc_printf("ERROR: No breakpoint at $%04X\r\n", addr);
	} else {
		check(r);
	}
}

static void cmd_break_list(void)
{
	static uint16_t bps[64];
	size_t count = emu_bp_list(bps, sizeof(bps) / sizeof(bps[0]));
	if (count > sizeof(bps) / sizeof(bps[0])) {
		count = sizeof(bps) / sizeof(bps[0]);
	}
	if (count == 0) {
		usb_cdc_send("No breakpoints set\r\n");
		return;
	}
	usb_cdc_send("Breakpoints:\r\n");
	for (size_t i = 0; i < count; i++) {
		usb_cdc_printf("  $%04X\r\n", bps[i]);
	}
}

// ---------------------------------------------------------------------------
// PC actions: trig, hits, next, until

// Strict number parsing: the whole token must be a number <= max.
static bool parse_num(const char *tok, int base, uint32_t max, uint32_t *out)
{
	char *end;
	if (tok == NULL || *tok == '\0') {
		return false;
	}
	unsigned long v = strtoul(tok, &end, base);
	if (*end != '\0' || v > max) {
		return false;
	}
	*out = (uint32_t)v;
	return true;
}

static bool parse_addr(const char *tok, uint16_t *addr)
{
	uint32_t v;
	if (!parse_num(tok, 16, MAX_ADDRESS, &v)) {
		return false;
	}
	*addr = (uint16_t)v;
	return true;
}

static void print_trigger_pins(void)
{
	uint8_t pins[32];
	size_t n = emu_trigger_pins(pins, sizeof(pins));
	usb_cdc_send("Trigger pins (GPIO, decimal):");
	for (size_t i = 0; i < n && i < sizeof(pins); i++) {
		usb_cdc_printf(" %u%s", pins[i], emu_trigger_pin_is_spi_debug(pins[i]) ? "*" : "");
	}
	usb_cdc_printf("\r\n  * shared with the SPI debug output (J3); usable while 'debug off'"
		       " (now %s)\r\n",
		       emu_debug_spi_is_enabled() ? "ON" : "off");
}

static const char trig_usage[] =
	"ERROR: Usage: trig <addr> set|clr|tog <pin> | trig <addr> pulse <pin> [width]"
	" | trig <addr> count|notify\r\n";

static void cmd_trig_add(void)
{
	// trig <addr> <action> [<pin> [<width>]]   (addr, width hex; pin decimal)
	emu_action_t a = {0};
	emu_action_type_t type;
	if (cmd_token_count < 2 || !parse_addr(cmd_tokens[0], &a.addr) ||
	    !emu_action_from_name(cmd_tokens[1], &type) || type == EMU_ACT_BREAK) {
		usb_cdc_send(trig_usage);
		return;
	}
	a.type = (uint8_t)type;
	bool gpio = (type == EMU_ACT_SET || type == EMU_ACT_CLR || type == EMU_ACT_TOGGLE ||
		     type == EMU_ACT_PULSE);
	int expected = gpio ? 3 : 2;
	uint32_t v;
	if (gpio) {
		if (cmd_token_count < 3 || !parse_num(cmd_tokens[2], 10, 255, &v)) {
			usb_cdc_send(trig_usage);
			return;
		}
		a.pin = (uint8_t)v;
	}
	if (type == EMU_ACT_PULSE) {
		a.width = 1;
		if (cmd_token_count >= 4) {
			if (!parse_num(cmd_tokens[3], 16, 0xFFFF, &v) || v == 0) {
				usb_cdc_send("ERROR: pulse width is 1-FFFF E cycles (hex)\r\n");
				return;
			}
			a.width = (uint16_t)v;
			expected = 4;
		}
	}
	if (cmd_token_count > expected) {
		usb_cdc_send(trig_usage);
		return;
	}

	emu_result_t r = emu_action_add(&a);
	switch (r) {
	case EMU_OK:
		if (type == EMU_ACT_PULSE) {
			usb_cdc_printf("OK: $%04X pulse GPIO %u for $%X E cycles\r\n", a.addr, a.pin,
				       a.width);
		} else if (gpio) {
			usb_cdc_printf("OK: $%04X %s GPIO %u\r\n", a.addr, emu_action_name(type),
				       a.pin);
		} else {
			usb_cdc_printf("OK: $%04X %s\r\n", a.addr, emu_action_name(type));
		}
		break;
	case EMU_ERR_RANGE:
		usb_cdc_printf("ERROR: GPIO %u is not a trigger pin\r\n", a.pin);
		print_trigger_pins();
		break;
	case EMU_ERR_BUSY:
		usb_cdc_printf("ERROR: GPIO %u belongs to the SPI debug output; 'debug off' first\r\n",
			       a.pin);
		break;
	case EMU_ERR_EXISTS:
		if (gpio) {
			usb_cdc_printf("ERROR: $%04X already has an action on GPIO %u\r\n", a.addr,
				       a.pin);
		} else {
			usb_cdc_printf("ERROR: $%04X already has %s\r\n", a.addr,
				       emu_action_name(type));
		}
		break;
	case EMU_ERR_FULL:
		usb_cdc_printf("ERROR: action table full (%u actions, %u addresses)\r\n",
			       (unsigned)emu_action_max(), (unsigned)emu_bp_max());
		break;
	default:
		check(r);
		break;
	}
}

static void cmd_trig_del(void)
{
	// trig del <addr> <action> [<pin>]
	emu_action_t a = {0};
	emu_action_type_t type;
	uint32_t v;
	if (cmd_token_count < 2 || !parse_addr(cmd_tokens[0], &a.addr) ||
	    !emu_action_from_name(cmd_tokens[1], &type) || type == EMU_ACT_BREAK) {
		usb_cdc_send("ERROR: Usage: trig del <addr> set|clr|tog|pulse <pin> | "
			     "trig del <addr> count|notify\r\n");
		return;
	}
	a.type = (uint8_t)type;
	if (type == EMU_ACT_SET || type == EMU_ACT_CLR || type == EMU_ACT_TOGGLE ||
	    type == EMU_ACT_PULSE) {
		if (cmd_token_count < 3 || !parse_num(cmd_tokens[2], 10, 255, &v)) {
			usb_cdc_send("ERROR: GPIO actions need a pin: trig del <addr> <action> <pin>\r\n");
			return;
		}
		a.pin = (uint8_t)v;
	}
	emu_result_t r = emu_action_remove(&a);
	if (r == EMU_OK) {
		usb_cdc_send("OK: Action removed\r\n");
	} else if (r == EMU_ERR_NOT_FOUND) {
		usb_cdc_send("ERROR: No such action\r\n");
	} else {
		check(r);
	}
}

static void cmd_trig_clear(void)
{
	// trig clear [<addr>]: all actions except breakpoints
	int32_t addr = -1;
	if (cmd_token_count >= 1) {
		uint16_t a16;
		if (!parse_addr(cmd_tokens[0], &a16)) {
			usb_cdc_send("ERROR: Usage: trig clear [<addr>]\r\n");
			return;
		}
		addr = a16;
	}
	emu_result_t r = emu_action_clear(addr);
	if (r == EMU_OK || r == EMU_ERR_NOT_FOUND) {
		if (addr < 0) {
			usb_cdc_send("OK: All trigger actions cleared (breakpoints kept)\r\n");
		} else {
			usb_cdc_printf("OK: Trigger actions at $%04X cleared (breakpoint kept)\r\n",
				       (unsigned)addr);
		}
	} else {
		check(r);
	}
}

static void cmd_trig_list(void)
{
	static emu_action_t acts[128];
	size_t n = emu_action_list(acts, sizeof(acts) / sizeof(acts[0]));
	if (n > sizeof(acts) / sizeof(acts[0])) {
		n = sizeof(acts) / sizeof(acts[0]);
	}
	if (n == 0) {
		usb_cdc_send("No PC actions set\r\n");
		return;
	}
	usb_cdc_printf("PC actions (%u of %u):\r\n", (unsigned)n, (unsigned)emu_action_max());
	for (size_t i = 0; i < n;) {
		uint16_t addr = acts[i].addr;
		uint32_t hits = 0;
		(void)emu_hits_get(addr, &hits);
		usb_cdc_printf("  $%04X  hits=%-10lu", addr, (unsigned long)hits);
		for (; i < n && acts[i].addr == addr; i++) {
			const emu_action_t *a = &acts[i];
			if (a->type == EMU_ACT_PULSE) {
				usb_cdc_printf(" pulse:%u/$%X", a->pin, a->width);
			} else if (a->type == EMU_ACT_SET || a->type == EMU_ACT_CLR ||
				   a->type == EMU_ACT_TOGGLE) {
				usb_cdc_printf(" %s:%u", emu_action_name(a->type), a->pin);
			} else {
				usb_cdc_printf(" %s", emu_action_name(a->type));
			}
		}
		usb_cdc_send("\r\n");
	}
}

static void cmd_trig_pins(void)
{
	print_trigger_pins();
}

static void cmd_hits(void)
{
	// hits [<addr>]
	if (cmd_token_count >= 1) {
		uint16_t addr;
		uint32_t hits;
		if (!parse_addr(cmd_tokens[0], &addr)) {
			usb_cdc_send("ERROR: Usage: hits [<addr>]\r\n");
			return;
		}
		if (emu_hits_get(addr, &hits) != EMU_OK) {
			usb_cdc_printf("ERROR: No action at $%04X (use 'trig %04X count')\r\n", addr,
				       addr);
			return;
		}
		usb_cdc_printf("$%04X hits=%lu\r\n", addr, (unsigned long)hits);
		return;
	}
	static emu_action_t acts[128];
	size_t n = emu_action_list(acts, sizeof(acts) / sizeof(acts[0]));
	if (n > sizeof(acts) / sizeof(acts[0])) {
		n = sizeof(acts) / sizeof(acts[0]);
	}
	if (n == 0) {
		usb_cdc_send("No PC actions set\r\n");
		return;
	}
	for (size_t i = 0; i < n; i++) {
		if (i > 0 && acts[i].addr == acts[i - 1].addr) {
			continue;
		}
		uint32_t hits = 0;
		(void)emu_hits_get(acts[i].addr, &hits);
		usb_cdc_printf("$%04X hits=%lu\r\n", acts[i].addr, (unsigned long)hits);
	}
}

static void cmd_hits_reset(void)
{
	// hits reset [<addr>]
	int32_t addr = -1;
	if (cmd_token_count >= 1) {
		uint16_t a16;
		if (!parse_addr(cmd_tokens[0], &a16)) {
			usb_cdc_send("ERROR: Usage: hits reset [<addr>]\r\n");
			return;
		}
		addr = a16;
	}
	emu_result_t r = emu_hits_reset(addr);
	if (r == EMU_OK) {
		usb_cdc_send("OK: Hit counts reset\r\n");
	} else if (r == EMU_ERR_NOT_FOUND) {
		usb_cdc_printf("ERROR: No action at $%04X\r\n", (unsigned)addr);
	} else {
		check(r);
	}
}

static void cmd_next(void)
{
	// Step over a JSR/BSR/SWI; completion is reported by the HALTED event
	check(emu_step_over());
}

static void cmd_until(void)
{
	uint16_t addr;
	if (cmd_token_count != 1 || !parse_addr(cmd_tokens[0], &addr)) {
		usb_cdc_send("ERROR: Usage: until <addr_hex>\r\n");
		return;
	}
	if (check(emu_run_until(addr))) {
		usb_cdc_printf("OK: Running until $%04X\r\n", addr);
	}
}

// reg <name> <value>: the register name is consumed by the command table
static void set_reg_cmd(emu_reg_t reg)
{
	unsigned int value;
	if (cmd_token_count < 1 || sscanf(cmd_tokens[0], "%x", &value) != 1 || value > 0xFFFF) {
		usb_cdc_printf("ERROR: Usage: reg %s <value_hex>\r\n", emu_reg_name(reg));
		return;
	}
	emu_result_t r = emu_set_reg(reg, (uint16_t)value);
	if (r == EMU_ERR_RANGE) {
		usb_cdc_send("ERROR: Invalid register value\r\n");
		return;
	}
	if (!check(r)) {
		return;
	}
	emu_regs_t regs;
	if (!check(emu_get_regs(&regs))) {
		return;
	}
	switch (reg) {
	case EMU_REG_PC:
		usb_cdc_printf("OK: PC set to $%04X\r\n", regs.pc);
		break;
	case EMU_REG_A:
		usb_cdc_printf("OK: A set to $%02X\r\n", regs.a);
		break;
	case EMU_REG_B:
		usb_cdc_printf("OK: B set to $%02X\r\n", regs.b);
		break;
	case EMU_REG_X:
		usb_cdc_printf("OK: X set to $%04X\r\n", regs.x);
		break;
	case EMU_REG_SP:
		usb_cdc_printf("OK: SP set to $%04X\r\n", regs.sp);
		break;
	case EMU_REG_CCR:
		usb_cdc_printf("OK: CCR set to $%02X\r\n", regs.ccr);
		break;
	}
}

static void cmd_reg_pc(void)
{
	set_reg_cmd(EMU_REG_PC);
}

static void cmd_reg_a(void)
{
	set_reg_cmd(EMU_REG_A);
}

static void cmd_reg_b(void)
{
	set_reg_cmd(EMU_REG_B);
}

static void cmd_reg_x(void)
{
	set_reg_cmd(EMU_REG_X);
}

static void cmd_reg_sp(void)
{
	set_reg_cmd(EMU_REG_SP);
}

static void cmd_reg_ccr(void)
{
	set_reg_cmd(EMU_REG_CCR);
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
			uint8_t data;
			if (check(emu_bus_read((uint16_t)address, &data, 1))) {
				usb_cdc_printf("%02X\r\n", data);
			}
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
		uint8_t byte = (uint8_t)data;
		if (check(emu_bus_write((uint16_t)address, &byte, 1))) {
			usb_cdc_send("OK\r\n");
		}
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
		// static: core 1 has only a 2 KiB stack
		static uint8_t buffer[1024]; // Max block size
		if (!check(emu_bus_read((uint16_t)address, buffer, length))) {
			return;
		}
		usb_cdc_printf("Reading $%04X bytes from $%04X:\r\n", length, address);
		hex_dump((uint16_t)address, buffer, length);
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
	// static: core 1 has only a 2 KiB stack
	static uint8_t buffer[1024]; // Max block size
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

	if (check(emu_bus_write((uint16_t)address, buffer, count))) {
		usb_cdc_send("OK\r\n");
	}
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
	check(emu_halt());
	memory_clear_map();
	usb_cdc_send("OK: All memory pages unmapped\r\n");
	check(emu_reset());
}

// Helper function to generate a single Intel HEX record
static void generate_ihex_record(uint16_t address, uint8_t *data, uint8_t byte_count,
				 uint8_t record_type)
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
		uint8_t bytes_this_record =
			(remaining >= BYTES_PER_RECORD) ? BYTES_PER_RECORD : remaining;

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

	check(emu_halt());
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

		check(emu_reset());
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
	usb_cdc_send(
		"MC6800 Emulator Commands (all numbers in hex):\r\n"
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
		"  step [n]                  - Execute n instructions (default 1) while halted\r\n"
		"  regs                      - Show CPU registers\r\n"
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
		"  trig <a> set|clr|tog <p>  - At PC=a, set/clear/toggle GPIO p (decimal)\r\n"
		"  trig <a> pulse <p> [w]    - At PC=a, pulse GPIO p high for w E cycles (hex, default 1)\r\n"
		"  trig <a> count|notify     - At PC=a, just count hits / report each hit\r\n"
		"  trig del <a> <act> [p]    - Remove one action\r\n"
		"  trig clear [<a>]          - Remove trig actions (all, or at a); keeps breakpoints\r\n"
		"  trig list                 - List all PC actions with hit counts\r\n"
		"  trig pins                 - List usable trigger pins\r\n"
		"  hits [<a>]                - Show hit counts\r\n"
		"  hits reset [<a>]          - Reset hit counts\r\n"
		"  next                      - Step over JSR/BSR/SWI (else step 1)\r\n"
		"  until <a>                 - Run until PC=a\r\n"
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
	{"trig del", cmd_trig_del, true, false},     // needs <addr> <action> [pin]
	{"trig clear", cmd_trig_clear, true, false}, // optional <addr>
	{"trig list", cmd_trig_list, false, false},
	{"trig pins", cmd_trig_pins, false, false},
	{"hits reset", cmd_hits_reset, true, false}, // optional <addr>
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
	{"count reset", cmd_reset_instruction_counts, false, true},
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
	{"step", cmd_step, true, false}, // optional <count>
	{"regs", cmd_regs, false, false},
	{"reset", cmd_reset, false, false},
	{"bootloader", cmd_bootloader, false, false},
	{"boot", cmd_bootloader, false, false},      // Alias for bootloader
	{"break", cmd_break_set, true, false},       // needs <addr>
	{"trig", cmd_trig_add, true, false},         // needs <addr> <action> ...
	{"hits", cmd_hits, true, false},             // optional <addr>
	{"next", cmd_next, false, false},
	{"until", cmd_until, true, false},           // needs <addr>
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
			if (command_table[i].lock_emulator) {
				if (!check(emu_lock())) {
					return;
				}
			}

			printf("running handler\r\n");

			// for debugger (JLink reset type == 12)
			jlink_reset_strategy_12_marker();

			// Call the handler
			command_table[i].handler();

			if (command_table[i].lock_emulator) {
				emu_unlock();
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

// Print one-line register summary (used after a step or breakpoint)
static void print_regs_line(void)
{
	emu_regs_t r;
	if (emu_get_regs(&r) != EMU_OK) {
		return;
	}
	usb_cdc_printf("  PC=$%04X (%s) A=$%02X B=$%02X X=$%04X SP=$%04X CCR=$%02X\r\n", r.pc,
		       cpu_mnemonic_at(r.pc), r.a, r.b, r.x, r.sp, r.ccr);
}

// Report asynchronous events from the emulator core (breakpoints, illegal
// opcodes, completed steps, resets).
static void drain_emulator_events(void)
{
	static uint32_t reported_drops;
	emu_event_t ev;

	while (emu_poll_event(&ev)) {
		if (ev.type == EMU_EV_RESET) {
			usb_cdc_printf("CPU reset: PC=$%04X\r\n", ev.pc);
			continue;
		}
		if (ev.type == EMU_EV_PC_HIT) {
			usb_cdc_printf("HIT: PC=$%04X hits=%lu Prior PC=$%04X\r\n", ev.pc,
				       (unsigned long)ev.hits, ev.prev_pc);
			continue;
		}
		if (ev.type != EMU_EV_HALTED) {
			continue;
		}
		switch ((emu_halt_reason_t)ev.reason) {
		case EMU_HALT_BREAKPOINT:
			usb_cdc_printf("CPU halted at breakpoint at PC=$%04X Prior PC=$%04X\r\n",
				       ev.pc, ev.prev_pc);
			print_regs_line();
			break;
		case EMU_HALT_ILLEGAL:
			usb_cdc_printf(
				"*** UNIMPLEMENTED OPCODE: $%02X at PC=$%04X *** CPU halted\r\n",
				ev.opcode, ev.prev_pc);
			break;
		case EMU_HALT_HCF:
			usb_cdc_printf("HCF ($%02X) at PC=$%04X: CPU halted\r\n", ev.opcode,
				       ev.prev_pc);
			break;
		case EMU_HALT_STEP_DONE:
			print_regs_line();
			break;
		case EMU_HALT_UNTIL:
			usb_cdc_printf("CPU stopped at PC=$%04X\r\n", ev.pc);
			print_regs_line();
			break;
		case EMU_HALT_USER:
			usb_cdc_printf("CPU halted at PC=$%04X\r\n", ev.pc);
			break;
		}
	}

	uint32_t drops = emu_events_dropped();
	if (drops != reported_drops) {
		usb_cdc_printf("(%lu emulator events dropped)\r\n", drops - reported_drops);
		reported_drops = drops;
	}
}

// Process USB events
void usb_cdc_task(void)
{
	// Process TinyUSB events
	tud_task();

	if (!in_hex_mode) {
		drain_emulator_events();
	}

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
					check(emu_reset());
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
