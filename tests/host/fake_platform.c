// SPDX-License-Identifier: MIT
//
// Host-test stand-ins for the hardware-facing parts of the emulator: a flat
// 64 KiB memory, a toy instruction set, fake interrupt pins and time.

#include "clock.h"
#include "cpu_state.h"
#include "hardware/structs/timer.h"
#include "instructions.h"
#include "interrupts.h"
#include "memory.h"
#include <time.h>
#include <unistd.h>

uint8_t fake_mem[65536];

uint32_t cycle_count, pending_cycles, last_pio_cycles;
int32_t cycle_overage, cycle_underage;
bool fake_eclock_running;
volatile bool fake_reset_pin;

// ---------------------------------------------------------------------------
// Time

uint64_t fake_time_us(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

void busy_wait_us_32(uint32_t us)
{
	usleep(us);
}

static fake_timer_t the_timer;

fake_timer_t *fake_timer(void)
{
	the_timer.timerawl = (uint32_t)fake_time_us();
	return &the_timer;
}

// ---------------------------------------------------------------------------
// Clock

void eclock_start(void)
{
	fake_eclock_running = true;
	cycle_count = 0;
}

bool eclock_stop(void)
{
	bool was = fake_eclock_running;
	fake_eclock_running = false;
	return was;
}

// ---------------------------------------------------------------------------
// Memory

uint8_t memory_read_fast(uint16_t address)
{
	pending_cycles++;
	return fake_mem[address];
}

void memory_write_fast(uint16_t address, uint8_t value)
{
	pending_cycles++;
	if (address < 0x8000) {
		fake_mem[address] = value;
	}
}

bool memory_range_has_unmapped(uint16_t address, uint16_t len)
{
	(void)address;
	(void)len;
	return false;
}

memory_type_t memory_get_type(uint16_t address)
{
	return address >= 0x8000 ? MEM_TYPE_ROM : MEM_TYPE_RAM;
}

void led_all_off(void)
{
}

void bus_read_block_with_eclock(uint16_t address, uint16_t length, uint8_t *buffer)
{
	for (uint16_t i = 0; i < length; i++) {
		buffer[i] = fake_mem[(uint16_t)(address + i)];
	}
}

void bus_write_block_with_eclock(uint16_t address, const uint8_t *buffer, uint16_t length)
{
	for (uint16_t i = 0; i < length; i++) {
		fake_mem[(uint16_t)(address + i)] = buffer[i];
	}
}

// ---------------------------------------------------------------------------
// Interrupts

void interrupts_init(void)
{
}

void interrupt_service_reset(void)
{
	cpu.pc = (uint16_t)((fake_mem[VECTOR_RESET] << 8) | fake_mem[VECTOR_RESET + 1]);
	cpu.a = cpu.b = 0;
	cpu.x = cpu.sp = 0;
	cpu.ccr = CCR_FIXED | CCR_I;
	cpu.wai_state = false;
	cpu.halted = true;
	cpu.running = false;
	cpu.instruction_count = 0;
}

interrupt_t interrupt_check(void)
{
	return fake_reset_pin ? INT_RESET : INT_NONE;
}

// ---------------------------------------------------------------------------
// A toy instruction set, just enough to exercise the state machine:
//   $01 NOP     $4C INCA      $20 rr BRA rel
//   $DD HCF     anything else: unimplemented

void instruction_execute(void)
{
	cpu.last_opcode_address = cpu.pc;
	uint8_t opcode = memory_read_fast(cpu.pc++);
	cpu.instruction_count++;
	switch (opcode) {
	case 0x01:
		break;
	case 0x4C:
		cpu.a++;
		break;
	case 0x20: {
		int8_t rel = (int8_t)memory_read_fast(cpu.pc++);
		cpu.pc = (uint16_t)(cpu.pc + rel);
		break;
	}
	case 0xDD:
		cpu.stop_requested = true;
		cpu.stop_reason = EMU_HALT_HCF;
		cpu.stop_opcode = opcode;
		break;
	default:
		cpu.stop_requested = true;
		cpu.stop_reason = EMU_HALT_ILLEGAL;
		cpu.stop_opcode = opcode;
		break;
	}
	cycle_count += pending_cycles;
	pending_cycles = 0;
	// Slow the toy CPU down a little so the host thread gets a look in
	usleep(1);
}

const char *instruction_get_mnemonic(uint8_t opcode)
{
	(void)opcode;
	return "?";
}
