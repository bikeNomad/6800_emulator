#pragma once
#include <stdint.h>
void instruction_execute(void);
const char *instruction_get_mnemonic(uint8_t opcode);
