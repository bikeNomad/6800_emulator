/**
 * MC6800 Instruction Implementation
 * Core instruction set for Phase 1
 */

#include "instructions.h"
#include "cpu_state.h"
#include "memory.h"
#include "clock.h"
#include <stdio.h>

// Instruction mnemonics
static const char* mnemonics[256] = {
    [0x01] = "NOP",
    [0x08] = "INX",
    [0x09] = "DEX",
    [0x0A] = "CLV",
    [0x0B] = "SEV",
    [0x0C] = "CLC",
    [0x0D] = "SEC",
    [0x0E] = "CLI",
    [0x0F] = "SEI",
    [0x11] = "CBA",
    [0x16] = "TAB",
    [0x17] = "TBA",
    [0x1B] = "ABA",
    [0x20] = "BRA",
    [0x22] = "BHI",
    [0x23] = "BLS",
    [0x24] = "BCC",
    [0x25] = "BCS",
    [0x26] = "BNE",
    [0x27] = "BEQ",
    [0x28] = "BVC",
    [0x29] = "BVS",
    [0x2A] = "BPL",
    [0x2B] = "BMI",
    [0x2C] = "BGE",
    [0x2D] = "BLT",
    [0x2E] = "BGT",
    [0x2F] = "BLE",
    [0x30] = "TSX",
    [0x31] = "INS",
    [0x34] = "DES",
    [0x35] = "TXS",
    [0x36] = "PSHA",
    [0x37] = "PSHB",
    [0x32] = "PULA",
    [0x33] = "PULB",
    [0x39] = "RTS",
    [0x3B] = "RTI",
    [0x3E] = "WAI",
    [0x44] = "LSRA",
    [0x48] = "ASLA",
    [0x4A] = "DECA",
    [0x4C] = "INCA",
    [0x4D] = "TSTA",
    [0x4F] = "CLRA",
    [0x53] = "COMB",
    [0x54] = "LSRB",
    [0x58] = "ASLB",
    [0x59] = "ROLB",
    [0x5A] = "DECB",
    [0x5C] = "INCB",
    [0x5D] = "TSTB",
    [0x5F] = "CLRB",
    [0x6E] = "JMP (IND)",
    [0x6F] = "CLR (IND)",
    [0x7A] = "DEC (EXT)",
    [0x7E] = "JMP (EXT)",
    [0x80] = "SUBA (IMM)",
    [0x81] = "CMPA (IMM)",
    [0x84] = "ANDA (IMM)",
    [0x86] = "LDAA (IMM)",
    [0x8A] = "ORAA (IMM)",
    [0x8B] = "ADDA (IMM)",
    [0x8C] = "CPX (IMM)",
    [0x8D] = "BSR",
    [0x8E] = "LDS (IMM)",
    [0x90] = "SUBA (DIR)",
    [0x94] = "ANDA (DIR)",
    [0x96] = "LDAA (DIR)",
    [0x97] = "STAA (DIR)",
    [0x99] = "ADCA (DIR)",
    [0x9A] = "ORAA (DIR)",
    [0x9B] = "ADDA (DIR)",
    [0x9C] = "CPX (DIR)",
    [0x9E] = "LDS (DIR)",
    [0x9F] = "STS (DIR)",
    [0xA0] = "SUBA (IND)",
    [0xA4] = "ANDA (IND)",
    [0xA6] = "LDAA (IND)",
    [0xA7] = "STAA (IND)",
    [0xAA] = "ORAA (IND)",
    [0xAB] = "ADDA (IND)",
    [0xAD] = "JSR (IND)",
    [0xAE] = "LDS (IND)",
    [0xAF] = "STS (IND)",
    [0xB0] = "SUBA (EXT)",
    [0xB4] = "ANDA (EXT)",
    [0xB6] = "LDAA (EXT)",
    [0xB7] = "STAA (EXT)",
    [0xBA] = "ORAA (EXT)",
    [0xBB] = "ADDA (EXT)",
    [0xBD] = "JSR (EXT)",
    [0xBE] = "LDS (EXT)",
    [0xBF] = "STS (EXT)",
    [0xC0] = "SUBB (IMM)",
    [0xC1] = "CMPB (IMM)",
    [0xC4] = "ANDB (IMM)",
    [0xC5] = "BITB (DIR)",
    [0xC6] = "LDAB (IMM)",
    [0xCA] = "ORAB (IMM)",
    [0xCB] = "ADDB (IMM)",
    [0xCE] = "LDX (IMM)",
    [0xD0] = "SUBB (DIR)",
    [0xD4] = "ANDB (DIR)",
    [0xD6] = "LDAB (DIR)",
    [0xD7] = "STAB (DIR)",
    [0xDA] = "ORAB (DIR)",
    [0xDB] = "ADDB (DIR)",
    [0xDE] = "LDX (DIR)",
    [0xDF] = "STX (DIR)",
    [0xE0] = "SUBB (IND)",
    [0xE4] = "ANDB (IND)",
    [0xE6] = "LDAB (IND)",
    [0xE7] = "STAB (IND)",
    [0xEA] = "ORAB (IND)",
    [0xEB] = "ADDB (IND)",
    [0xEE] = "LDX (IND)",
    [0xEF] = "STX (IND)",
    [0xF0] = "SUBB (EXT)",
    [0xF4] = "ANDB (EXT)",
    [0xF6] = "LDAB (EXT)",
    [0xF7] = "STAB (EXT)",
    [0xFA] = "ORAB (EXT)",
    [0xFB] = "ADDB (EXT)",
    [0xFE] = "LDX (EXT)",
    [0xFF] = "STX (EXT)",
};

// Get instruction mnemonic
const char* instruction_get_mnemonic(uint8_t opcode) {
    const char* mnemonic = mnemonics[opcode];
    return mnemonic ? mnemonic : "???";
}

// Helper: Add with carry flag update
static void add_with_carry(uint8_t *dest, uint8_t operand) {
    uint16_t result = *dest + operand;
    cpu_update_nzv(result & 0xFF, *dest, operand, false);
    cpu_set_flag(CCR_C, result > 0xFF);
    cpu_set_flag(CCR_H, ((*dest & 0x0F) + (operand & 0x0F)) > 0x0F);
    *dest = result & 0xFF;
}

// Helper: Subtract with carry flag update
static void sub_with_carry(uint8_t *dest, uint8_t operand) {
    uint16_t result = *dest - operand;
    cpu_update_nzv(result & 0xFF, *dest, operand, true);
    cpu_set_flag(CCR_C, result > 0xFF);
    *dest = result & 0xFF;
}

// Execute one instruction
void instruction_execute(void) {
    // Fetch opcode
    uint8_t opcode = memory_read(cpu.pc++);

    // Increment instruction counter
    cpu.instruction_count++;

    // Decode and execute based on opcode
    switch (opcode) {
        // NOP - No Operation
        case 0x01:
            // 2 cycles total (fetch + execute)
            break;

        // TAP - Transfer A to Processor Status (CCR)
        case 0x06:
            cpu.ccr = (cpu.a & 0x3F) | CCR_FIXED;  // Only lower 6 bits, preserve bits 7-6
            break;

        // TPA - Transfer Processor Status (CCR) to A
        case 0x07:
            cpu.a = cpu.ccr;
            break;

        // INX - Increment Index Register X
        case 0x08:
            eclock_consume_cycles(3);  // Internal: 16-bit increment
            cpu.x++;
            cpu_set_flag(CCR_Z, cpu.x == 0);
            break;

        // DEX - Decrement Index Register X
        case 0x09:
            eclock_consume_cycles(3);  // Internal: 16-bit decrement
            cpu.x--;
            cpu_set_flag(CCR_Z, cpu.x == 0);
            break;

        // CLV - Clear Overflow Flag
        case 0x0A:
            cpu_set_flag(CCR_V, false);
            break;

        // SEV - Set Overflow Flag
        case 0x0B:
            cpu_set_flag(CCR_V, true);
            break;

        // CLC - Clear Carry Flag
        case 0x0C:
            cpu_set_flag(CCR_C, false);
            break;

        // SEC - Set Carry Flag
        case 0x0D:
            cpu_set_flag(CCR_C, true);
            break;

        // CLI - Clear Interrupt Mask
        case 0x0E:
            cpu_set_flag(CCR_I, false);
            break;

        // SEI - Set Interrupt Mask
        case 0x0F:
            cpu_set_flag(CCR_I, true);
            break;

        // SBA - Subtract B from A
        case 0x10: {
            uint16_t result = cpu.a - cpu.b;
            cpu_update_nzv(result & 0xFF, cpu.a, cpu.b, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.a = result & 0xFF;
            break;
        }

        // DAA - Decimal Adjust Accumulator A
        case 0x19: {
            uint8_t correction = 0;
            uint8_t lower_nibble = cpu.a & 0x0F;
            uint8_t upper_nibble = (cpu.a >> 4) & 0x0F;

            // Check lower nibble
            if (cpu_get_flag(CCR_H) || lower_nibble > 9) {
                correction += 0x06;
            }

            // Check upper nibble
            if (cpu_get_flag(CCR_C) || upper_nibble > 9 || (upper_nibble > 8 && lower_nibble > 9)) {
                correction += 0x60;
            }

            // Apply correction
            uint16_t result = cpu.a + correction;
            cpu.a = result & 0xFF;

            // Update flags
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_C, result > 0xFF || cpu_get_flag(CCR_C));
            break;
        }

        // CBA - Compare Accumulators (A with B)
        case 0x11: {
            uint16_t result = cpu.a - cpu.b;
            cpu_update_nzv(result & 0xFF, cpu.a, cpu.b, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // TAB - Transfer A to B
        case 0x16:
            cpu.b = cpu.a;
            cpu_update_nz(cpu.b);
            break;

        // TBA - Transfer B to A
        case 0x17:
            cpu.a = cpu.b;
            cpu_update_nz(cpu.a);
            break;

        // ABA - Add B to A
        case 0x1B: {
            uint16_t result = cpu.a + cpu.b;
            cpu_update_nzv(result & 0xFF, cpu.a, cpu.b, false);
            cpu_set_flag(CCR_C, result > 0xFF);  // Set carry if overflow
            cpu_set_flag(CCR_H, ((cpu.a & 0x0F) + (cpu.b & 0x0F)) > 0x0F);  // Half carry
            cpu.a = result & 0xFF;
            break;
        }

        // BRA - Branch Always
        case 0x20: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            cpu.pc += offset;
            break;
        }

        // BHI - Branch if Higher (C=0 AND Z=0)
        case 0x22: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (!cpu_get_flag(CCR_C) && !cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BLS - Branch if Lower or Same (C=1 OR Z=1)
        case 0x23: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_C) || cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BCC - Branch if Carry Clear
        case 0x24: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (!cpu_get_flag(CCR_C)) {
                cpu.pc += offset;
            }
            break;
        }

        // BCS - Branch if Carry Set
        case 0x25: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_C)) {
                cpu.pc += offset;
            }
            break;
        }

        // BNE - Branch if Not Equal (Z=0)
        case 0x26: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (!cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BEQ - Branch if Equal (Z=1)
        case 0x27: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BVC - Branch if Overflow Clear (V=0)
        case 0x28: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (!cpu_get_flag(CCR_V)) {
                cpu.pc += offset;
            }
            break;
        }

        // BVS - Branch if Overflow Set (V=1)
        case 0x29: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_V)) {
                cpu.pc += offset;
            }
            break;
        }

        // BPL - Branch if Plus (N=0)
        case 0x2A: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (!cpu_get_flag(CCR_N)) {
                cpu.pc += offset;
            }
            break;
        }

        // BMI - Branch if Minus (N=1)
        case 0x2B: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_N)) {
                cpu.pc += offset;
            }
            break;
        }

        // BGE - Branch if Greater or Equal (N XOR V = 0)
        case 0x2C: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_N) == cpu_get_flag(CCR_V)) {
                cpu.pc += offset;
            }
            break;
        }

        // BLT - Branch if Less Than (N XOR V = 1)
        case 0x2D: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_N) != cpu_get_flag(CCR_V)) {
                cpu.pc += offset;
            }
            break;
        }

        // BGT - Branch if Greater Than (Z=0 AND (N XOR V)=0)
        case 0x2E: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (!cpu_get_flag(CCR_Z) && (cpu_get_flag(CCR_N) == cpu_get_flag(CCR_V))) {
                cpu.pc += offset;
            }
            break;
        }

        // BLE - Branch if Less or Equal (Z=1 OR (N XOR V)=1)
        case 0x2F: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: branch decision and address calculation
            if (cpu_get_flag(CCR_Z) || (cpu_get_flag(CCR_N) != cpu_get_flag(CCR_V))) {
                cpu.pc += offset;
            }
            break;
        }

        // TSX - Transfer Stack Pointer to X
        case 0x30:
            eclock_consume_cycles(3);  // Internal: transfer operation
            cpu.x = cpu.sp + 1;
            break;

        // INS - Increment Stack Pointer
        case 0x31:
            eclock_consume_cycles(3);  // Internal: stack pointer operation
            cpu.sp++;
            break;

        // DES - Decrement Stack Pointer
        case 0x34:
            eclock_consume_cycles(3);  // Internal: stack pointer operation
            cpu.sp--;
            break;

        // TXS - Transfer X to Stack Pointer
        case 0x35:
            eclock_consume_cycles(3);  // Internal: transfer operation
            cpu.sp = cpu.x - 1;
            break;

        // PSHA - Push A onto stack
        case 0x36:
            eclock_consume_cycles(1);  // Internal: setup
            cpu_push(cpu.a);
            eclock_consume_cycles(1);  // Internal: cleanup
            break;

        // PSHB - Push B onto stack
        case 0x37:
            eclock_consume_cycles(1);  // Internal: setup
            cpu_push(cpu.b);
            eclock_consume_cycles(1);  // Internal: cleanup
            break;

        // PULA - Pull A from stack
        case 0x32:
            eclock_consume_cycles(2);  // Internal: setup
            cpu.a = cpu_pull();
            break;

        // PULB - Pull B from stack
        case 0x33:
            eclock_consume_cycles(2);  // Internal: setup
            cpu.b = cpu_pull();
            break;

        // RTS - Return from Subroutine
        case 0x39:
            eclock_consume_cycles(2);  // Internal: setup
            cpu.pc = cpu_pull16();
            break;

        // RTI - Return from Interrupt
        case 0x3B:
            eclock_consume_cycles(2);  // Internal: setup
            cpu.ccr = cpu_pull();
            cpu.b = cpu_pull();
            cpu.a = cpu_pull();
            cpu.x = cpu_pull16();
            cpu.pc = cpu_pull16();
            break;

        // WAI - Wait for Interrupt
        case 0x3E:
            eclock_consume_cycles(1);  // Internal: setup
            cpu.halted = true;
            // Push registers
            cpu_push16(cpu.pc);
            cpu_push16(cpu.x);
            cpu_push(cpu.a);
            cpu_push(cpu.b);
            cpu_push(cpu.ccr);
            break;

        // SWI - Software Interrupt
        case 0x3F:
            eclock_consume_cycles(1);  // Internal: setup
            // Push registers onto stack
            cpu_push16(cpu.pc);
            cpu_push16(cpu.x);
            cpu_push(cpu.a);
            cpu_push(cpu.b);
            cpu_push(cpu.ccr);
            eclock_consume_cycles(1);  // Internal: setup
            // Set interrupt mask
            cpu_set_flag(CCR_I, true);
            // Load PC from SWI vector (0xFFFA-0xFFFB)
            uint8_t pch = memory_read(0xFFFA);
            uint8_t pcl = memory_read(0xFFFB);
            cpu.pc = (pch << 8) | pcl;
            break;

        // NEGA - Negate A (two's complement)
        case 0x40: {
            cpu.a = (~cpu.a) + 1;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, cpu.a == 0x80);
            cpu_set_flag(CCR_C, cpu.a != 0);
            break;
        }

        // COMA - Complement A (one's complement)
        case 0x43:
            cpu.a = ~cpu.a;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, true);
            break;

        // LSRA - Logical Shift Right A
        case 0x44: {
            uint8_t old_bit0 = cpu.a & 0x01;
            cpu.a >>= 1;
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_set_flag(CCR_N, false);  // N always cleared for logical shift right
            cpu_set_flag(CCR_Z, cpu.a == 0);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_C));  // V = N XOR C = 0 XOR C = C
            break;
        }

        // RORA - Rotate Right A through Carry
        case 0x46: {
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x80 : 0;
            uint8_t old_bit0 = cpu.a & 0x01;
            cpu.a = (cpu.a >> 1) | old_carry;
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASRA - Arithmetic Shift Right A
        case 0x47: {
            uint8_t old_bit0 = cpu.a & 0x01;
            uint8_t sign_bit = cpu.a & 0x80;
            cpu.a = (cpu.a >> 1) | sign_bit;
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASLA - Arithmetic Shift Left A
        case 0x48: {
            uint8_t old_bit7 = (cpu.a & 0x80) >> 7;
            cpu.a <<= 1;
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(cpu.a);
            // V = N XOR C (overflow if sign changed incorrectly)
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ROLA - Rotate Left A through Carry
        case 0x49: {
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x01 : 0;
            uint8_t old_bit7 = (cpu.a & 0x80) >> 7;
            cpu.a = (cpu.a << 1) | old_carry;
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // DECA - Decrement A
        case 0x4A:
            cpu.a--;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, cpu.a == 0x7F);
            break;

        // INCA - Increment A
        case 0x4C:
            cpu.a++;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, cpu.a == 0x80);
            break;

        // TSTA - Test Accumulator A
        case 0x4D:
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;

        // CLRA - Clear Accumulator A
        case 0x4F:
            cpu.a = 0x00;
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, true);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, false);
            break;

        // NEGB - Negate B (two's complement)
        case 0x50: {
            cpu.b = (~cpu.b) + 1;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, cpu.b == 0x80);
            cpu_set_flag(CCR_C, cpu.b != 0);
            break;
        }

        // COMB - Complement B (one's complement)
        case 0x53:
            cpu.b = ~cpu.b;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, true);
            break;

        // LSRB - Logical Shift Right B
        case 0x54: {
            uint8_t old_bit0 = cpu.b & 0x01;
            cpu.b >>= 1;
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_set_flag(CCR_N, false);  // N always cleared for logical shift right
            cpu_set_flag(CCR_Z, cpu.b == 0);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_C));  // V = N XOR C = 0 XOR C = C
            break;
        }

        // RORB - Rotate Right B through Carry
        case 0x56: {
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x80 : 0;
            uint8_t old_bit0 = cpu.b & 0x01;
            cpu.b = (cpu.b >> 1) | old_carry;
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASRB - Arithmetic Shift Right B
        case 0x57: {
            uint8_t old_bit0 = cpu.b & 0x01;
            uint8_t sign_bit = cpu.b & 0x80;
            cpu.b = (cpu.b >> 1) | sign_bit;
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASLB - Arithmetic Shift Left B
        case 0x58: {
            uint8_t old_bit7 = (cpu.b & 0x80) >> 7;
            cpu.b <<= 1;
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(cpu.b);
            // V = N XOR C (overflow if sign changed incorrectly)
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ROLB - Rotate Left B
        case 0x59: {
            uint8_t old_bit7 = (cpu.b & 0x80) >> 7;
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 1 : 0;
            cpu.b = (cpu.b << 1) | old_carry;
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // DECB - Decrement B
        case 0x5A:
            cpu.b--;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, cpu.b == 0x7F);
            break;

        // INCB - Increment B
        case 0x5C:
            cpu.b++;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, cpu.b == 0x80);
            break;

        // TSTB - Test Accumulator B
        case 0x5D:
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;

        // CLRB - Clear Accumulator B
        case 0x5F:
            cpu.b = 0x00;
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, true);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, false);
            break;

        // NEG - Negate Memory (Indexed)
        case 0x60: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            value = (~value) + 1;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, value == 0x80);
            cpu_set_flag(CCR_C, value != 0);
            break;
        }

        // COM - Complement Memory (Indexed)
        case 0x63: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            value = ~value;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, true);
            break;
        }

        // LSR - Logical Shift Right (Indexed)
        case 0x64: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            uint8_t old_bit0 = value & 0x01;
            value >>= 1;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, value == 0);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_C));
            break;
        }

        // ROR - Rotate Right (Indexed)
        case 0x66: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x80 : 0;
            uint8_t old_bit0 = value & 0x01;
            value = (value >> 1) | old_carry;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASR - Arithmetic Shift Right (Indexed)
        case 0x67: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            uint8_t old_bit0 = value & 0x01;
            uint8_t sign_bit = value & 0x80;
            value = (value >> 1) | sign_bit;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASL - Arithmetic Shift Left (Indexed)
        case 0x68: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            uint8_t old_bit7 = (value & 0x80) >> 7;
            value <<= 1;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ROL - Rotate Left (Indexed)
        case 0x69: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x01 : 0;
            uint8_t old_bit7 = (value & 0x80) >> 7;
            value = (value << 1) | old_carry;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // DEC - Decrement Memory (Indexed)
        case 0x6A: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            value--;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, value == 0x7F);
            break;
        }

        // INC - Increment Memory (Indexed)
        case 0x6C: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: modify operation
            value++;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, value == 0x80);
            break;
        }

        // TST - Test Memory (Indexed)
        case 0x6D: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t value = memory_read(addr);
            eclock_consume_cycles(1);  // Internal: test operation
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // JMP (Indexed)
        case 0x6E: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            cpu.pc = cpu.x + offset;
            break;
        }

        // CLR - Clear Memory (Indexed)
        case 0x6F: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(4);  // Internal: address calculation
            memory_write(cpu.x + offset, 0x00);
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, true);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, false);
            break;
        }

        // NEG - Negate Memory (Extended)
        case 0x70: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            value = (~value) + 1;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, value == 0x80);
            cpu_set_flag(CCR_C, value != 0);
            break;
        }

        // COM - Complement Memory (Extended)
        case 0x73: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            value = ~value;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, true);
            break;
        }

        // LSR - Logical Shift Right (Extended)
        case 0x74: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            uint8_t old_bit0 = value & 0x01;
            value >>= 1;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, value == 0);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_C));
            break;
        }

        // ROR - Rotate Right (Extended)
        case 0x76: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x80 : 0;
            uint8_t old_bit0 = value & 0x01;
            value = (value >> 1) | old_carry;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASR - Arithmetic Shift Right (Extended)
        case 0x77: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            uint8_t old_bit0 = value & 0x01;
            uint8_t sign_bit = value & 0x80;
            value = (value >> 1) | sign_bit;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit0 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ASL - Arithmetic Shift Left (Extended)
        case 0x78: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            uint8_t old_bit7 = (value & 0x80) >> 7;
            value <<= 1;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // ROL - Rotate Left (Extended)
        case 0x79: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            uint8_t old_carry = cpu_get_flag(CCR_C) ? 0x01 : 0;
            uint8_t old_bit7 = (value & 0x80) >> 7;
            value = (value << 1) | old_carry;
            memory_write(addr, value);
            cpu_set_flag(CCR_C, old_bit7 != 0);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, cpu_get_flag(CCR_N) != cpu_get_flag(CCR_C));
            break;
        }

        // DEC - Decrement Memory (Extended)
        case 0x7A: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            value--;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, value == 0x7F);  // V set if value was 0x80 (overflow from negative to positive)
            break;
        }

        // INC - Increment Memory (Extended)
        case 0x7C: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            value++;
            memory_write(addr, value);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, value == 0x80);
            break;
        }

        // TST - Test Memory (Extended)
        case 0x7D: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t value = memory_read(addr);
            cpu_update_nz(value);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // JMP (Extended)
        case 0x7E: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            cpu.pc = (high << 8) | low;
            break;
        }

        // CLR - Clear Memory (Extended)
        case 0x7F: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            memory_write(addr, 0x00);
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, true);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, false);
            break;
        }

        // SUBA (Immediate)
        case 0x80: {
            uint8_t operand = memory_read(cpu.pc++);
            sub_with_carry(&cpu.a, operand);
            break;
        }

        // CMPA - Compare A (Immediate)
        case 0x81: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.a - operand;
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCA - Subtract with Carry A (Immediate)
        case 0x82: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.a - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.a = result & 0xFF;
            break;
        }

        // ANDA (Immediate)
        case 0x84: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.a &= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITA - Bit Test A (Immediate)
        case 0x85: {
            uint8_t operand = memory_read(cpu.pc++);
            uint8_t result = cpu.a & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAA (Immediate)
        case 0x86: {
            cpu.a = memory_read(cpu.pc++);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORA - Exclusive OR A (Immediate)
        case 0x88: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.a ^= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCA - Add with Carry A (Immediate)
        case 0x89: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.a + operand + (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.a, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.a & 0x0F) + (operand & 0x0F) + (cpu_get_flag(CCR_C) ? 1 : 0)) > 0x0F);
            cpu.a = result & 0xFF;
            break;
        }

        // ORAA (Immediate)
        case 0x8A: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.a |= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDA (Immediate)
        case 0x8B: {
            uint8_t operand = memory_read(cpu.pc++);
            add_with_carry(&cpu.a, operand);
            break;
        }

        // CPX - Compare X (Immediate)
        case 0x8C: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t imm_val = (high << 8) | low;

            // Perform subtraction for comparison
            uint32_t result = cpu.x - imm_val;

            // Update flags
            cpu_set_flag(CCR_N, (result & 0x8000) != 0);
            cpu_set_flag(CCR_Z, (result & 0xFFFF) == 0);
            cpu_set_flag(CCR_V, ((cpu.x ^ imm_val) & (cpu.x ^ result) & 0x8000) != 0);
            break;
        }

        // BSR - Branch to Subroutine
        case 0x8D: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            eclock_consume_cycles(1);  // Internal: address calculation
            cpu_push16(cpu.pc);
            eclock_consume_cycles(3);  // Internal: branch setup
            cpu.pc += offset;
            break;
        }

        // LDS - Load Stack Pointer (Immediate)
        case 0x8E: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            cpu.sp = (high << 8) | low;
            cpu_update_nz(high);  // Update flags based on high byte
            cpu_set_flag(CCR_V, false);  // Clear overflow
            break;
        }

        // SUBA (Direct)
        case 0x90: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            sub_with_carry(&cpu.a, operand);
            break;
        }

        // CMPA (Direct)
        case 0x91: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.a - operand;
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCA (Direct)
        case 0x92: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.a - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.a = result & 0xFF;
            break;
        }

        // ANDA (Direct)
        case 0x94: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            cpu.a &= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITA (Direct)
        case 0x95: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint8_t result = cpu.a & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAA (Direct)
        case 0x96: {
            uint8_t addr = memory_read(cpu.pc++);
            cpu.a = memory_read(addr);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAA (Direct)
        case 0x97: {
            uint8_t addr = memory_read(cpu.pc++);
            memory_write(addr, cpu.a);
            eclock_consume_cycles(1);  // Internal: address hold after write
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORA (Direct)
        case 0x98: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            cpu.a ^= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCA - Add with Carry to A (Direct)
        case 0x99: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.a + operand + carry;

            cpu_update_nzv(result & 0xFF, cpu.a, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.a & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.a = result & 0xFF;
            break;
        }

        // ORAA (Direct)
        case 0x9A: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            cpu.a |= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDA - Add to A (Direct)
        case 0x9B: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            add_with_carry(&cpu.a, operand);
            break;
        }

        // CPX - Compare X (Direct)
        case 0x9C: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t high = memory_read(addr);
            uint8_t low = memory_read(addr + 1);
            uint16_t mem_val = (high << 8) | low;

            // Perform subtraction for comparison
            uint32_t result = cpu.x - mem_val;

            // Update flags
            cpu_set_flag(CCR_N, (result & 0x8000) != 0);
            cpu_set_flag(CCR_Z, (result & 0xFFFF) == 0);
            cpu_set_flag(CCR_V, ((cpu.x ^ mem_val) & (cpu.x ^ result) & 0x8000) != 0);
            break;
        }

        // JSR - Jump to Subroutine (Direct)
        case 0x9D: {
            uint8_t addr = memory_read(cpu.pc++);
            cpu_push16(cpu.pc);
            cpu.pc = addr;
            break;
        }

        // LDS - Load Stack Pointer (Direct)
        case 0x9E: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t high = memory_read(addr);
            uint8_t low = memory_read(addr + 1);
            cpu.sp = (high << 8) | low;
            cpu_update_nz(high);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STS - Store Stack Pointer (Direct)
        case 0x9F: {
            uint8_t addr = memory_read(cpu.pc++);
            memory_write(addr, (cpu.sp >> 8) & 0xFF);
            memory_write(addr + 1, cpu.sp & 0xFF);
            eclock_consume_cycles(1);  // Internal: address hold after write
            cpu_update_nz((cpu.sp >> 8) & 0xFF);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // SUBA (Indexed)
        case 0xA0: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            sub_with_carry(&cpu.a, operand);
            break;
        }

        // CMPA (Indexed)
        case 0xA1: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.a - operand;
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCA (Indexed)
        case 0xA2: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.a - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.a = result & 0xFF;
            break;
        }

        // ANDA (Indexed)
        case 0xA4: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            cpu.a &= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITA (Indexed)
        case 0xA5: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint8_t result = cpu.a & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAA (Indexed)
        case 0xA6: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            cpu.a = memory_read(cpu.x + offset);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAA (Indexed)
        case 0xA7: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(3);  // Internal: address calculation
            memory_write(cpu.x + offset, cpu.a);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORA (Indexed)
        case 0xA8: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            cpu.a ^= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCA (Indexed)
        case 0xA9: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.a + operand + carry;
            cpu_update_nzv(result & 0xFF, cpu.a, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.a & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.a = result & 0xFF;
            break;
        }

        // ORAA (Indexed)
        case 0xAA: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            cpu.a |= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDA (Indexed)
        case 0xAB: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            add_with_carry(&cpu.a, operand);
            break;
        }

        // CPX (Indexed)
        case 0xAC: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t high = memory_read(addr);
            uint8_t low = memory_read(addr + 1);
            uint16_t mem_val = (high << 8) | low;
            uint32_t result = cpu.x - mem_val;
            cpu_set_flag(CCR_N, (result & 0x8000) != 0);
            cpu_set_flag(CCR_Z, (result & 0xFFFF) == 0);
            cpu_set_flag(CCR_V, ((cpu.x ^ mem_val) & (cpu.x ^ result) & 0x8000) != 0);
            break;
        }

        // JSR (Indexed)
        case 0xAD: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(1);  // Internal: address calculation
            cpu_push16(cpu.pc);
            eclock_consume_cycles(3);  // Internal: jump setup
            cpu.pc = cpu.x + offset;
            break;
        }

        // LDS - Load Stack Pointer (Indexed)
        case 0xAE: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t high = memory_read(addr);
            uint8_t low = memory_read(addr + 1);
            cpu.sp = (high << 8) | low;
            cpu_update_nz(high);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STS - Store Stack Pointer (Indexed)
        case 0xAF: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(3);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            memory_write(addr, (cpu.sp >> 8) & 0xFF);
            memory_write(addr + 1, cpu.sp & 0xFF);
            cpu_update_nz((cpu.sp >> 8) & 0xFF);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // SUBA (Extended)
        case 0xB0: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            sub_with_carry(&cpu.a, operand);
            break;
        }

        // CMPA (Extended)
        case 0xB1: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.a - operand;
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCA (Extended)
        case 0xB2: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.a - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.a, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.a = result & 0xFF;
            break;
        }

        // ANDA (Extended)
        case 0xB4: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            cpu.a &= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITA (Extended)
        case 0xB5: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint8_t result = cpu.a & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAA (Extended)
        case 0xB6: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            cpu.a = memory_read(addr);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAA (Extended)
        case 0xB7: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            eclock_consume_cycles(1);  // Internal
            memory_write(addr, cpu.a);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORA (Extended)
        case 0xB8: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            cpu.a ^= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCA (Extended)
        case 0xB9: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.a + operand + carry;
            cpu_update_nzv(result & 0xFF, cpu.a, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.a & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.a = result & 0xFF;
            break;
        }

        // ORAA (Extended)
        case 0xBA: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            cpu.a |= operand;
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDA (Extended)
        case 0xBB: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            add_with_carry(&cpu.a, operand);
            break;
        }

        // CPX (Extended)
        case 0xBC: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t mem_high = memory_read(addr);
            uint8_t mem_low = memory_read(addr + 1);
            uint16_t mem_val = (mem_high << 8) | mem_low;
            uint32_t result = cpu.x - mem_val;
            cpu_set_flag(CCR_N, (result & 0x8000) != 0);
            cpu_set_flag(CCR_Z, (result & 0xFFFF) == 0);
            cpu_set_flag(CCR_V, ((cpu.x ^ mem_val) & (cpu.x ^ result) & 0x8000) != 0);
            break;
        }

        // JSR (Extended)
        case 0xBD: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            eclock_consume_cycles(1);  // Internal: jump setup
            cpu_push16(cpu.pc);
            eclock_consume_cycles(3);  // Internal: jump setup
            cpu.pc = addr;
            break;
        }

        // LDS - Load Stack Pointer (Extended)
        case 0xBE: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t sp_high = memory_read(addr);
            uint8_t sp_low = memory_read(addr + 1);
            cpu.sp = (sp_high << 8) | sp_low;
            cpu_update_nz(sp_high);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STS - Store Stack Pointer (Extended)
        case 0xBF: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            eclock_consume_cycles(1);  // Internal: address hold after write
            memory_write(addr, (cpu.sp >> 8) & 0xFF);
            memory_write(addr + 1, cpu.sp & 0xFF);
            cpu_update_nz((cpu.sp >> 8) & 0xFF);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // SUBB (Immediate)
        case 0xC0: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // CMPB - Compare B (Immediate)
        case 0xC1: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCB (Immediate)
        case 0xC2: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.b - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // ANDB - Logical AND B (Immediate)
        case 0xC4: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.b &= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITB - Bit Test B (Direct)
        case 0xC5: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t mem_val = memory_read(addr);
            uint8_t result = cpu.b & mem_val;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAB (Immediate)
        case 0xC6: {
            cpu.b = memory_read(cpu.pc++);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORB (Immediate)
        case 0xC8: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.b ^= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCB (Immediate)
        case 0xC9: {
            uint8_t operand = memory_read(cpu.pc++);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.b + operand + carry;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // ORAB (Immediate)
        case 0xCA: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.b |= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDB (Immediate)
        case 0xCB: {
            uint8_t operand = memory_read(cpu.pc++);
            uint16_t result = cpu.b + operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F)) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // LDX (Immediate) - 16-bit load
        case 0xCE: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            cpu.x = (high << 8) | low;
            cpu_update_nz(high);  // Only test high byte
            cpu_set_flag(CCR_V, false);
            break;
        }

        // SUBB (Direct)
        case 0xD0: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // CMPB (Direct)
        case 0xD1: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCB (Direct)
        case 0xD2: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // ANDB (Direct)
        case 0xD4: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            cpu.b &= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITB (Direct)
        case 0xD5: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint8_t result = cpu.b & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAB (Direct)
        case 0xD6: {
            uint8_t addr = memory_read(cpu.pc++);
            cpu.b = memory_read(addr);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAB (Direct)
        case 0xD7: {
            uint8_t addr = memory_read(cpu.pc++);
            memory_write(addr, cpu.b);
            eclock_consume_cycles(1);  // Internal: address hold after write
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORB (Direct)
        case 0xD8: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            cpu.b ^= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCB (Direct)
        case 0xD9: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.b + operand + carry;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // ORAB (Direct)
        case 0xDA: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            cpu.b |= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDB (Direct)
        case 0xDB: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b + operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F)) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // LDX (Direct)
        case 0xDE: {
            uint8_t addr = memory_read(cpu.pc++);
            uint8_t high = memory_read(addr);
            uint8_t low = memory_read(addr + 1);
            cpu.x = (high << 8) | low;
            cpu_update_nz(high);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STX (Direct)
        case 0xDF: {
            uint8_t addr = memory_read(cpu.pc++);
            memory_write(addr, cpu.x >> 8);
            memory_write(addr + 1, cpu.x & 0xFF);
            eclock_consume_cycles(1);  // Internal: address hold after write
            cpu_update_nz(cpu.x >> 8);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // SUBB (Indexed)
        case 0xE0: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // CMPB (Indexed)
        case 0xE1: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCB (Indexed)
        case 0xE2: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // ANDB - Logical AND B (Indexed)
        case 0xE4: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint8_t operand = memory_read(cpu.x + offset);
            cpu.b &= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITB (Indexed)
        case 0xE5: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint8_t result = cpu.b & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAB (Indexed)
        case 0xE6: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            cpu.b = memory_read(cpu.x + offset);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAB (Indexed)
        case 0xE7: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(3);  // Internal: address calculation
            memory_write(cpu.x + offset, cpu.b);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORB (Indexed)
        case 0xE8: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            cpu.b ^= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCB (Indexed)
        case 0xE9: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.b + operand + carry;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // ORAB - Logical OR B (Indexed)
        case 0xEA: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint8_t operand = memory_read(cpu.x + offset);
            cpu.b |= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDB (Indexed)
        case 0xEB: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b + operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F)) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // LDX (Indexed)
        case 0xEE: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(2);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            uint8_t high = memory_read(addr);
            uint8_t low = memory_read(addr + 1);
            cpu.x = (high << 8) | low;
            cpu_update_nz(high);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STX (Indexed)
        case 0xEF: {
            uint8_t offset = memory_read(cpu.pc++);
            eclock_consume_cycles(3);  // Internal: address calculation
            uint16_t addr = cpu.x + offset;
            memory_write(addr, cpu.x >> 8);
            memory_write(addr + 1, cpu.x & 0xFF);
            cpu_update_nz(cpu.x >> 8);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // SUBB (Extended)
        case 0xF0: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // CMPB (Extended)
        case 0xF1: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SBCB (Extended)
        case 0xF2: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b - operand - (cpu_get_flag(CCR_C) ? 1 : 0);
            cpu_update_nzv(result & 0xFF, cpu.b, operand, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu.b = result & 0xFF;
            break;
        }

        // ANDB (Extended)
        case 0xF4: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            cpu.b &= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // BITB (Extended)
        case 0xF5: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint8_t result = cpu.b & operand;
            cpu_update_nz(result);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAB (Extended)
        case 0xF6: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            cpu.b = memory_read(addr);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAB (Extended)
        case 0xF7: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            eclock_consume_cycles(1);  // Internal: address hold after write
            memory_write(addr, cpu.b);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // EORB (Extended)
        case 0xF8: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            cpu.b ^= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADCB (Extended)
        case 0xF9: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint8_t carry = cpu_get_flag(CCR_C) ? 1 : 0;
            uint16_t result = cpu.b + operand + carry;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F) + carry) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // ORAB (Extended)
        case 0xFA: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            cpu.b |= operand;
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // ADDB (Extended)
        case 0xFB: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t operand = memory_read(addr);
            uint16_t result = cpu.b + operand;
            cpu_update_nzv(result & 0xFF, cpu.b, operand, false);
            cpu_set_flag(CCR_C, result > 0xFF);
            cpu_set_flag(CCR_H, ((cpu.b & 0x0F) + (operand & 0x0F)) > 0x0F);
            cpu.b = result & 0xFF;
            break;
        }

        // LDX (Extended)
        case 0xFE: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            uint8_t xh = memory_read(addr);
            uint8_t xl = memory_read(addr + 1);
            cpu.x = (xh << 8) | xl;
            cpu_update_nz(xh);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STX (Extended)
        case 0xFF: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            eclock_consume_cycles(1);  // Internal: address hold after write
            memory_write(addr, cpu.x >> 8);
            memory_write(addr + 1, cpu.x & 0xFF);
            cpu_update_nz(cpu.x >> 8);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // Unimplemented instruction
        default:
            printf("*** UNIMPLEMENTED OPCODE: $%02X at PC=$%04X ***\n", opcode, cpu.pc - 1);
            cpu.halted = true;
            break;
    }
}
