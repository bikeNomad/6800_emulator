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
    uint8_t  opcode;
    uint8_t  cycles;
} instruction_count_info;

extern volatile instruction_count_info instruction_counts[256];
extern volatile bool                   instruction_counting;

static inline bool instruction_count_enabled(void) {
    __dmb();
    __dsb();
    bool enabled = instruction_counting;
    __dmb();
    __dsb();
    return enabled;
}

static inline bool instruction_count_enable(bool enable) {
    __dmb();
    __dsb();
    bool old = instruction_counting;
    instruction_counting = enable;
    __dmb();
    __dsb();
    return old;
}

static inline void instruction_count_increment(uint8_t opcode) {
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

#endif  // COUNT_INSTRUCTIONS

#endif  // INSTRUCTIONS_H
