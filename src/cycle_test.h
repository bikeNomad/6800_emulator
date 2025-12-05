/**
 * MC6800 Instruction Cycle Count Testing
 * Verifies cycle-accurate timing for all instructions
 */

#ifndef CYCLE_TEST_H
#define CYCLE_TEST_H

#include <stdint.h>

// Run cycle count test for a single instruction
void cycle_test_instruction(uint8_t opcode);

// Run cycle count test for all implemented instructions
void cycle_test_all(void);

#endif // CYCLE_TEST_H
