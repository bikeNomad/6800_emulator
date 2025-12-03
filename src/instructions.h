/**
 * MC6800 Instruction Decoder and Executor
 * Implements cycle-accurate MC6800 instruction execution
 */

#ifndef INSTRUCTIONS_H
#define INSTRUCTIONS_H

#include <stdint.h>
#include <stdbool.h>

// Execute one instruction (cycle-accurate)
void instruction_execute(void);

// Get instruction mnemonic for disassembly
const char* instruction_get_mnemonic(uint8_t opcode);

#endif // INSTRUCTIONS_H
