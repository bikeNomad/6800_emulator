#include "emu_api.h"
#include "bus.h"
#include "clock.h"
#include "cpu_state.h"
#include "emulator.h"
#include "memory.h"
#include "memory_map.h"
#include "debug_spi.h"
#include "pc_actions.h"
#include "pico/time.h"
#include <ctype.h>
#include <string.h>
#include <strings.h>

// Host-core side of the emulator control API. See emu_api.h.

// How long to wait for the emulator core to acknowledge a command. The core
// polls for commands between instructions (and every 1 ms while in reset), so
// replies normally arrive within microseconds.
#define EMU_CMD_TIMEOUT_US 100000

static uint8_t next_seq;
static uint32_t lock_depth;

// ---------------------------------------------------------------------------
// Names

const char *emu_result_str(emu_result_t r)
{
	switch (r) {
	case EMU_OK:
		return "OK";
	case EMU_ERR_TIMEOUT:
		return "timeout waiting for emulator";
	case EMU_ERR_QUEUE:
		return "command queue full";
	case EMU_ERR_STATE:
		return "not allowed in current state";
	case EMU_ERR_LOCKED:
		return "emulator is locked";
	case EMU_ERR_RANGE:
		return "out of range";
	case EMU_ERR_FULL:
		return "no room";
	case EMU_ERR_EXISTS:
		return "already exists";
	case EMU_ERR_NOT_FOUND:
		return "not found";
	case EMU_ERR_ARG:
		return "invalid argument";
	case EMU_ERR_BUSY:
		return "in use";
	}
	return "unknown error";
}

const char *emu_state_name(emu_state_t s)
{
	switch (s) {
	case EMU_STATE_INITIALIZING:
		return "s_initializing";
	case EMU_STATE_RESETTING:
		return "s_resetting";
	case EMU_STATE_RUNNING:
		return "s_running";
	case EMU_STATE_WAITING:
		return "s_waiting_for_interrupt";
	case EMU_STATE_HALTED:
		return "s_halted";
	case EMU_STATE_LOADING:
		return "s_loading";
	}
	return "Unknown";
}

static const char *const reg_names[] = {
	[EMU_REG_PC] = "pc", [EMU_REG_A] = "a",   [EMU_REG_B] = "b",
	[EMU_REG_X] = "x",   [EMU_REG_SP] = "sp", [EMU_REG_CCR] = "ccr",
};

const char *emu_reg_name(emu_reg_t reg)
{
	return (reg <= EMU_REG_CCR) ? reg_names[reg] : "?";
}

bool emu_reg_from_name(const char *name, emu_reg_t *reg)
{
	for (int i = 0; i <= EMU_REG_CCR; i++) {
		const char *a = name, *b = reg_names[i];
		while (*a && *b && tolower((unsigned char)*a) == *b) {
			a++;
			b++;
		}
		if (*a == '\0' && *b == '\0') {
			*reg = (emu_reg_t)i;
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
// Setup

void emu_api_init(void)
{
	emu_cmd_ring_init(&emu_cmd_q);
	emu_reply_ring_init(&emu_reply_q);
	emu_event_ring_init(&emu_event_q);
	emu_event_drops = 0;
	emu_steps_pending = 0;
	lock_depth = 0;
	pc_actions_init();
}

__attribute__((weak)) void emu_host_idle(void)
{
	busy_wait_us_32(10);
}

// ---------------------------------------------------------------------------
// Command transport

static emu_result_t send_cmd(emu_cmd_t cmd, uint32_t arg)
{
	// Discard any stale replies left over from a timed-out command
	emu_reply_msg_t reply;
	while (emu_reply_ring_pop(&emu_reply_q, &reply)) {
	}

	emu_cmd_msg_t m = {.cmd = (uint8_t)cmd, .seq = ++next_seq, .arg = arg};
	if (!emu_cmd_ring_push(&emu_cmd_q, &m)) {
		return EMU_ERR_QUEUE;
	}

	absolute_time_t deadline = make_timeout_time_us(EMU_CMD_TIMEOUT_US);
	for (;;) {
		if (emu_reply_ring_pop(&emu_reply_q, &reply)) {
			if (reply.seq == m.seq) {
				return (emu_result_t)reply.result;
			}
			continue; // stale
		}
		if (time_reached(deadline)) {
			return EMU_ERR_TIMEOUT;
		}
		emu_host_idle();
	}
}

static emu_result_t send_control(emu_cmd_t cmd, uint32_t arg)
{
	if (lock_depth) {
		return EMU_ERR_LOCKED;
	}
	return send_cmd(cmd, arg);
}

// ---------------------------------------------------------------------------
// Execution control

emu_state_t emu_get_state(void)
{
	return emu_core_state;
}

emu_result_t emu_run(void)
{
	return send_control(EMU_CMD_RUN, 0);
}

emu_result_t emu_halt(void)
{
	return send_control(EMU_CMD_HALT, 0);
}

emu_result_t emu_reset(void)
{
	return send_control(EMU_CMD_RESET, 0);
}

emu_result_t emu_load_begin(void)
{
	return send_control(EMU_CMD_LOAD, 0);
}

emu_result_t emu_step(uint32_t count)
{
	if (count == 0) {
		return EMU_ERR_ARG;
	}
	return send_control(EMU_CMD_STEP, count);
}

emu_result_t emu_step_wait(uint32_t timeout_ms)
{
	absolute_time_t deadline = make_timeout_time_ms(timeout_ms);
	while (emu_steps_pending != 0 || emu_core_state != EMU_STATE_HALTED) {
		if (emu_core_state != EMU_STATE_HALTED) {
			return EMU_ERR_STATE; // left HALTED (e.g. /RESET asserted)
		}
		if (time_reached(deadline)) {
			return EMU_ERR_TIMEOUT;
		}
		emu_host_idle();
	}
	return EMU_OK;
}

// ---------------------------------------------------------------------------
// Locking

emu_result_t emu_lock(void)
{
	if (lock_depth == 0) {
		emu_result_t r = send_cmd(EMU_CMD_PAUSE, 0);
		if (r != EMU_OK) {
			return r;
		}
	}
	lock_depth++;
	return EMU_OK;
}

void emu_unlock(void)
{
	if (lock_depth == 0) {
		return;
	}
	if (--lock_depth == 0) {
		(void)send_cmd(EMU_CMD_RESUME, 0);
	}
}

bool emu_is_locked(void)
{
	return lock_depth != 0;
}

// ---------------------------------------------------------------------------
// Events

bool emu_poll_event(emu_event_t *ev)
{
	if (!emu_event_ring_pop(&emu_event_q, ev)) {
		return false;
	}
	if (ev->type == EMU_EV_PC_HIT) {
		pc_actions_host_rearm(ev->pc); // allow the next NOTIFY from this address
	}
	return true;
}

uint32_t emu_events_dropped(void)
{
	return emu_event_drops;
}

// ---------------------------------------------------------------------------
// Registers

static void read_regs(emu_regs_t *regs)
{
	regs->pc = cpu.pc;
	regs->x = cpu.x;
	regs->sp = cpu.sp;
	regs->a = cpu.a;
	regs->b = cpu.b;
	regs->ccr = cpu.ccr;
	regs->prev_pc = cpu.last_opcode_address;
}

emu_result_t emu_get_regs(emu_regs_t *regs)
{
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	read_regs(regs);
	emu_unlock();
	return EMU_OK;
}

emu_result_t emu_set_reg(emu_reg_t reg, uint16_t value)
{
	bool is8 = (reg == EMU_REG_A || reg == EMU_REG_B || reg == EMU_REG_CCR);
	if (reg > EMU_REG_CCR) {
		return EMU_ERR_ARG;
	}
	if (is8 && value > 0xFF) {
		return EMU_ERR_RANGE;
	}

	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	switch (reg) {
	case EMU_REG_PC:
		cpu.pc = value;
		break;
	case EMU_REG_A:
		cpu.a = (uint8_t)value;
		break;
	case EMU_REG_B:
		cpu.b = (uint8_t)value;
		break;
	case EMU_REG_X:
		cpu.x = value;
		break;
	case EMU_REG_SP:
		cpu.sp = value;
		break;
	case EMU_REG_CCR:
		cpu.ccr = ((uint8_t)value & 0x3F) | CCR_FIXED; // bits 7-6 always 1
		break;
	}
	emu_unlock();
	return EMU_OK;
}

// ---------------------------------------------------------------------------
// Memory

static bool range_ok(uint16_t addr, size_t len)
{
	return len > 0 && (uint32_t)addr + len <= 0x10000u;
}

// The memory_*_fast() helpers charge E cycles to the emulator's cycle
// accounting. Host accesses must not disturb the emulated timing, so the
// pending count is saved and restored around them (safe: the core is parked).
emu_result_t emu_mem_read(uint16_t addr, uint8_t *buf, size_t len)
{
	if (!range_ok(addr, len)) {
		return EMU_ERR_RANGE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	uint32_t saved_pending = pending_cycles;
	bool started_eclock = false;
	if (memory_range_has_unmapped(addr, (uint16_t)len) && !eclock_is_running()) {
		eclock_start();
		started_eclock = true;
	}
	for (size_t i = 0; i < len; i++) {
		buf[i] = memory_read_fast((uint16_t)(addr + i));
	}
	if (started_eclock) {
		eclock_stop();
	}
	pending_cycles = saved_pending;
	emu_unlock();
	return EMU_OK;
}

emu_result_t emu_mem_write(uint16_t addr, const uint8_t *buf, size_t len)
{
	if (!range_ok(addr, len)) {
		return EMU_ERR_RANGE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	uint32_t saved_pending = pending_cycles;
	bool started_eclock = false;
	if (memory_range_has_unmapped(addr, (uint16_t)len) && !eclock_is_running()) {
		eclock_start();
		started_eclock = true;
	}
	for (size_t i = 0; i < len; i++) {
		memory_write_fast((uint16_t)(addr + i), buf[i]);
	}
	if (started_eclock) {
		eclock_stop();
	}
	pending_cycles = saved_pending;
	emu_unlock();
	return EMU_OK;
}

emu_result_t emu_bus_read(uint16_t addr, uint8_t *buf, size_t len)
{
	if (!range_ok(addr, len) || len > 0xFFFF) {
		return EMU_ERR_RANGE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	bus_read_block_with_eclock(addr, (uint16_t)len, buf);
	emu_unlock();
	return EMU_OK;
}

emu_result_t emu_bus_write(uint16_t addr, const uint8_t *buf, size_t len)
{
	if (!range_ok(addr, len) || len > 0xFFFF) {
		return EMU_ERR_RANGE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	bus_write_block_with_eclock(addr, buf, (uint16_t)len);
	emu_unlock();
	return EMU_OK;
}

// ---------------------------------------------------------------------------
// PC actions

static const char *const action_names[] = {
	[EMU_ACT_BREAK] = "break", [EMU_ACT_SET] = "set",     [EMU_ACT_CLR] = "clr",
	[EMU_ACT_TOGGLE] = "tog",  [EMU_ACT_PULSE] = "pulse", [EMU_ACT_COUNT] = "count",
	[EMU_ACT_NOTIFY] = "notify",
};

const char *emu_action_name(emu_action_type_t type)
{
	return (type <= EMU_ACT_NOTIFY) ? action_names[type] : "?";
}

bool emu_action_from_name(const char *name, emu_action_type_t *type)
{
	for (int i = 0; i <= EMU_ACT_NOTIFY; i++) {
		if (strcasecmp(name, action_names[i]) == 0) {
			*type = (emu_action_type_t)i;
			return true;
		}
	}
	if (strcasecmp(name, "toggle") == 0) {
		*type = EMU_ACT_TOGGLE;
		return true;
	}
	return false;
}

static bool is_gpio_action(uint8_t type)
{
	return type == EMU_ACT_SET || type == EMU_ACT_CLR || type == EMU_ACT_TOGGLE ||
	       type == EMU_ACT_PULSE;
}

emu_result_t emu_action_add(const emu_action_t *a)
{
	if (a->type > EMU_ACT_NOTIFY) {
		return EMU_ERR_ARG;
	}
	if (is_gpio_action(a->type)) {
		if (!pc_pin_allowed(a->pin)) {
			return EMU_ERR_RANGE;
		}
		if (pc_pin_is_spi_debug(a->pin) && debug_spi_is_enabled()) {
			return EMU_ERR_BUSY;
		}
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	r = pc_actions_host_add(a);
	emu_unlock();
	return r;
}

emu_result_t emu_action_remove(const emu_action_t *a)
{
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	r = pc_actions_host_remove(a);
	emu_unlock();
	return r;
}

emu_result_t emu_action_clear(int32_t addr)
{
	if (addr > 0xFFFF) {
		return EMU_ERR_RANGE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	r = pc_actions_host_clear(addr, false, false);
	emu_unlock();
	return r;
}

// The action list only changes through this API, on this core, so reading it
// needs no lock.
size_t emu_action_list(emu_action_t *out, size_t max)
{
	return pc_actions_host_list(out, max);
}

size_t emu_action_max(void)
{
	return PC_MAX_ACTIONS;
}

emu_result_t emu_hits_get(uint16_t addr, uint32_t *hits)
{
	// The table only changes on this core; a 32-bit counter read is atomic.
	const pc_entry_t *e = pc_actions_host_find(addr);
	if (!e) {
		return EMU_ERR_NOT_FOUND;
	}
	*hits = e->hits;
	return EMU_OK;
}

emu_result_t emu_hits_reset(int32_t addr)
{
	if (addr > 0xFFFF) {
		return EMU_ERR_RANGE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	r = pc_actions_host_hits_reset(addr);
	emu_unlock();
	return r;
}

size_t emu_trigger_pins(uint8_t *out, size_t max)
{
	return pc_trigger_pins(out, max);
}

bool emu_trigger_pin_is_spi_debug(uint8_t pin)
{
	return pc_pin_is_spi_debug(pin);
}

emu_result_t emu_debug_spi_enable(bool on)
{
	if (on && pc_spi_debug_pins_claimed()) {
		return EMU_ERR_BUSY;
	}
	debug_spi_enable(on);
	return EMU_OK;
}

bool emu_debug_spi_is_enabled(void)
{
	return debug_spi_is_enabled();
}

// ---------------------------------------------------------------------------
// Breakpoints

emu_result_t emu_bp_add(uint16_t addr)
{
	emu_action_t a = {.addr = addr, .type = EMU_ACT_BREAK};
	return emu_action_add(&a);
}

emu_result_t emu_bp_remove(uint16_t addr)
{
	emu_action_t a = {.addr = addr, .type = EMU_ACT_BREAK};
	return emu_action_remove(&a);
}

emu_result_t emu_bp_clear(void)
{
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	(void)pc_actions_host_clear(-1, true, true); // NOT_FOUND (none set) is fine
	emu_unlock();
	return EMU_OK;
}

size_t emu_bp_list(uint16_t *out, size_t max)
{
	static emu_action_t all[PC_MAX_ACTIONS];
	size_t n = pc_actions_host_list(all, PC_MAX_ACTIONS);
	size_t count = 0;
	for (size_t i = 0; i < n; i++) {
		if (all[i].type == EMU_ACT_BREAK) {
			if (count < max) {
				out[count] = all[i].addr;
			}
			count++;
		}
	}
	return count;
}

size_t emu_bp_max(void)
{
	return PC_MAX_ADDRS;
}

// ---------------------------------------------------------------------------
// Step over / run until

// Length of a call instruction at the start of op[], or 0 if it isn't one.
static uint16_t call_length(const uint8_t *op)
{
	switch (op[0]) {
	case 0x8D: // BSR rel
	case 0xAD: // JSR ind,X
		return 2;
	case 0xBD: // JSR ext
		return 3;
	case 0x3F: // SWI (returns with RTI)
		return 1;
	default:
		return 0;
	}
}

emu_result_t emu_step_over(void)
{
	if (lock_depth) {
		return EMU_ERR_LOCKED;
	}
	if (emu_core_state != EMU_STATE_HALTED || emu_steps_pending) {
		return EMU_ERR_STATE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	if (emu_core_state != EMU_STATE_HALTED) { // e.g. /RESET asserted meanwhile
		emu_unlock();
		return EMU_ERR_STATE;
	}
	uint16_t pc = cpu.pc;
	uint16_t sp = cpu.sp;
	uint8_t op[1];
	r = emu_mem_read(pc, op, 1);
	uint16_t len = (r == EMU_OK) ? call_length(op) : 0;
	if (len) {
		pc_temp_set((uint16_t)(pc + len), true, sp);
	}
	emu_unlock();
	if (r != EMU_OK) {
		return r;
	}
	if (!len) {
		return emu_step(1);
	}
	r = emu_run();
	if (r != EMU_OK && emu_lock() == EMU_OK) {
		pc_temp_cancel();
		emu_unlock();
	}
	return r;
}

emu_result_t emu_run_until(uint16_t addr)
{
	if (lock_depth) {
		return EMU_ERR_LOCKED;
	}
	emu_state_t st = emu_core_state;
	if (st != EMU_STATE_HALTED && st != EMU_STATE_RUNNING && st != EMU_STATE_WAITING) {
		return EMU_ERR_STATE;
	}
	if (st == EMU_STATE_HALTED && emu_steps_pending) {
		return EMU_ERR_STATE;
	}
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	// Check again now the core is parked and the state can't change.
	st = emu_core_state;
	if (st != EMU_STATE_HALTED && st != EMU_STATE_RUNNING && st != EMU_STATE_WAITING) {
		emu_unlock();
		return EMU_ERR_STATE;
	}
	bool halted = (st == EMU_STATE_HALTED);
	pc_temp_set(addr, false, 0);
	emu_unlock();
	if (!halted) {
		return EMU_OK;
	}
	r = emu_run();
	if (r != EMU_OK && emu_lock() == EMU_OK) {
		pc_temp_cancel();
		emu_unlock();
	}
	return r;
}

// ---------------------------------------------------------------------------
// Status

emu_result_t emu_get_status(emu_status_t *st)
{
	emu_result_t r = emu_lock();
	if (r != EMU_OK) {
		return r;
	}
	st->state = emu_core_state;
	read_regs(&st->regs);
	st->halted = cpu.halted;
	st->running = cpu_is_running();
	st->instruction_count = cpu.instruction_count;
	st->cycle_count = eclock_get_count();
	st->eclock_running = eclock_is_running();
	st->pio_cycles = st->eclock_running ? eclock_get_pio_cycles() : last_pio_cycles;
	st->cycle_overage = cycle_overage;
	st->cycle_underage = cycle_underage;
	st->eclock_external = (eclock_get_mode() == ECLOCK_EXTERNAL);
	st->steps_pending = emu_steps_pending;
	emu_unlock();
	return EMU_OK;
}
