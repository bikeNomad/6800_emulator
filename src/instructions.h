/*
 * SPDX-License-Identifier: MIT
 *
 * Copyright 2026 Ned Konz <ned@metamagix.tech>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/**
 * MC6800 Instruction Decoder and Executor
 * Implements cycle-accurate MC6800 instruction execution
 */

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include "emulator.h"
#include "hardware/sync.h"

// Execute one instruction (cycle-accurate)
void instruction_execute(void);

// Get instruction mnemonic for disassembly
const char *instruction_get_mnemonic(uint8_t opcode);

#if COUNT_INSTRUCTIONS
typedef struct instruction_count_info {
	uint32_t count;
	uint8_t opcode;
	uint8_t cycles;
} instruction_count_info;

extern volatile instruction_count_info instruction_counts[256];
extern volatile bool instruction_counting;

static inline bool instruction_count_enabled(void)
{
	__dmb();
	__dsb();
	bool enabled = instruction_counting;
	__dmb();
	__dsb();
	return enabled;
}

static inline bool instruction_count_enable(bool enable)
{
	__dmb();
	__dsb();
	bool old = instruction_counting;
	instruction_counting = enable;
	__dmb();
	__dsb();
	return old;
}

static inline void instruction_count_increment(uint8_t opcode)
{
	if (!instruction_count_enabled()) {
		return;
	}
	uint32_t cnt = instruction_counts[opcode].count;
	// Prevent overflow
	if (cnt == 0xFFFFFFFFU) {
		instruction_count_enable(false);
		return;
	}
	instruction_counts[opcode].count = cnt + 1;
}

void instruction_count_initialize(void);

void instruction_count_report(printf_func_t printf_func);

#endif // COUNT_INSTRUCTIONS

#endif // INSTRUCTIONS_H
