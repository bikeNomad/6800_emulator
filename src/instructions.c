/**
 * MC6800 Instruction Implementation
 * Core instruction set for Phase 1
 */

#include "instructions.h"
#include "cpu_state.h"
#include "memory.h"
#include <stdio.h>

// Instruction mnemonics
static const char* mnemonics[256] = {
    [0x01] = "NOP",
    [0x08] = "INX",
    [0x09] = "DEX",
    [0x0D] = "SEC",
    [0x0F] = "CLV",
    [0x11] = "CBA",
    [0x16] = "TAB",
    [0x17] = "TBA",
    [0x1B] = "ABA",
    [0x20] = "BRA",
    [0x22] = "BHI",
    [0x24] = "BCC",
    [0x25] = "BCS",
    [0x26] = "BNE",
    [0x27] = "BEQ",
    [0x2A] = "BPL",
    [0x30] = "TSX",
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
    [0x54] = "LSRB",
    [0x58] = "ASLB",
    [0x5A] = "DECB",
    [0x5C] = "INCB",
    [0x5D] = "TSTB",
    [0x6E] = "JMP (IND)",
    [0x6F] = "CLR (IND)",
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
    [0xA0] = "SUBA (IND)",
    [0xA4] = "ANDA (IND)",
    [0xA6] = "LDAA (IND)",
    [0xA7] = "STAA (IND)",
    [0xAA] = "ORAA (IND)",
    [0xAB] = "ADDA (IND)",
    [0xAD] = "JSR (IND)",
    [0xB0] = "SUBA (EXT)",
    [0xB4] = "ANDA (EXT)",
    [0xB6] = "LDAA (EXT)",
    [0xB7] = "STAA (EXT)",
    [0xBA] = "ORAA (EXT)",
    [0xBB] = "ADDA (EXT)",
    [0xBD] = "JSR (EXT)",
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

    // Decode and execute based on opcode
    switch (opcode) {
        // NOP - No Operation
        case 0x01:
            // 2 cycles total (fetch + execute)
            break;

        // INX - Increment Index Register X
        case 0x08:
            cpu.x++;
            cpu_set_flag(CCR_Z, cpu.x == 0);
            break;

        // DEX - Decrement Index Register X
        case 0x09:
            cpu.x--;
            cpu_set_flag(CCR_Z, cpu.x == 0);
            break;

        // CBA - Compare Accumulators (A with B)
        case 0x11: {
            uint16_t result = cpu.a - cpu.b;
            cpu_update_nzv(result & 0xFF, cpu.a, cpu.b, true);
            cpu_set_flag(CCR_C, result > 0xFF);
            break;
        }

        // SEC - Set Carry Flag
        case 0x0D:
            cpu_set_flag(CCR_C, true);
            break;

        // CLV - Clear Overflow Flag
        case 0x0F:
            cpu_set_flag(CCR_V, false);
            break;

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
            cpu.pc += offset;
            break;
        }

        // BHI - Branch if Higher (C=0 AND Z=0)
        case 0x22: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            if (!cpu_get_flag(CCR_C) && !cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BCC - Branch if Carry Clear
        case 0x24: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            if (!cpu_get_flag(CCR_C)) {
                cpu.pc += offset;
            }
            break;
        }

        // BCS - Branch if Carry Set
        case 0x25: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            if (cpu_get_flag(CCR_C)) {
                cpu.pc += offset;
            }
            break;
        }

        // BNE - Branch if Not Equal (Z=0)
        case 0x26: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            if (!cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BEQ - Branch if Equal (Z=1)
        case 0x27: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            if (cpu_get_flag(CCR_Z)) {
                cpu.pc += offset;
            }
            break;
        }

        // BPL - Branch if Plus (N=0)
        case 0x2A: {
            int8_t offset = (int8_t)memory_read(cpu.pc++);
            if (!cpu_get_flag(CCR_N)) {
                cpu.pc += offset;
            }
            break;
        }

        // TSX - Transfer Stack Pointer to X
        case 0x30:
            cpu.x = cpu.sp + 1;
            break;

        // TXS - Transfer X to Stack Pointer
        case 0x35:
            cpu.sp = cpu.x - 1;
            break;

        // PSHA - Push A onto stack
        case 0x36:
            cpu_push(cpu.a);
            break;

        // PSHB - Push B onto stack
        case 0x37:
            cpu_push(cpu.b);
            break;

        // PULA - Pull A from stack
        case 0x32:
            cpu.a = cpu_pull();
            break;

        // PULB - Pull B from stack
        case 0x33:
            cpu.b = cpu_pull();
            break;

        // RTS - Return from Subroutine
        case 0x39:
            cpu.pc = cpu_pull16();
            break;

        // RTI - Return from Interrupt
        case 0x3B:
            cpu.ccr = cpu_pull();
            cpu.b = cpu_pull();
            cpu.a = cpu_pull();
            cpu.x = cpu_pull16();
            cpu.pc = cpu_pull16();
            break;

        // WAI - Wait for Interrupt
        case 0x3E:
            cpu.halted = true;
            // Push registers
            cpu_push16(cpu.pc);
            cpu_push16(cpu.x);
            cpu_push(cpu.a);
            cpu_push(cpu.b);
            cpu_push(cpu.ccr);
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

        // JMP (Indexed)
        case 0x6E: {
            uint8_t offset = memory_read(cpu.pc++);
            cpu.pc = cpu.x + offset;
            break;
        }

        // CLR - Clear Memory (Indexed)
        case 0x6F: {
            uint8_t offset = memory_read(cpu.pc++);
            memory_write(cpu.x + offset, 0x00);
            cpu_set_flag(CCR_N, false);
            cpu_set_flag(CCR_Z, true);
            cpu_set_flag(CCR_V, false);
            cpu_set_flag(CCR_C, false);
            break;
        }

        // JMP (Extended)
        case 0x7E: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            cpu.pc = (high << 8) | low;
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

        // ANDA (Immediate)
        case 0x84: {
            uint8_t operand = memory_read(cpu.pc++);
            cpu.a &= operand;
            cpu_update_nz(cpu.a);
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
            cpu_push16(cpu.pc);
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

        // LDAA (Indexed)
        case 0xA6: {
            uint8_t offset = memory_read(cpu.pc++);
            cpu.a = memory_read(cpu.x + offset);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAA (Indexed)
        case 0xA7: {
            uint8_t offset = memory_read(cpu.pc++);
            memory_write(cpu.x + offset, cpu.a);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // JSR (Indexed)
        case 0xAD: {
            uint8_t offset = memory_read(cpu.pc++);
            cpu_push16(cpu.pc);
            cpu.pc = cpu.x + offset;
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
            memory_write(addr, cpu.a);
            cpu_update_nz(cpu.a);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // JSR (Extended)
        case 0xBD: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            uint16_t addr = (high << 8) | low;
            cpu_push16(cpu.pc);
            cpu.pc = addr;
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

        // LDX (Immediate) - 16-bit load
        case 0xCE: {
            uint8_t high = memory_read(cpu.pc++);
            uint8_t low = memory_read(cpu.pc++);
            cpu.x = (high << 8) | low;
            cpu_update_nz(high);  // Only test high byte
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
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
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
            cpu_update_nz(cpu.x >> 8);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDAB (Indexed)
        case 0xE6: {
            uint8_t offset = memory_read(cpu.pc++);
            cpu.b = memory_read(cpu.x + offset);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // STAB (Indexed)
        case 0xE7: {
            uint8_t offset = memory_read(cpu.pc++);
            memory_write(cpu.x + offset, cpu.b);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
            break;
        }

        // LDX (Indexed)
        case 0xEE: {
            uint8_t offset = memory_read(cpu.pc++);
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
            uint16_t addr = cpu.x + offset;
            memory_write(addr, cpu.x >> 8);
            memory_write(addr + 1, cpu.x & 0xFF);
            cpu_update_nz(cpu.x >> 8);
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
            memory_write(addr, cpu.b);
            cpu_update_nz(cpu.b);
            cpu_set_flag(CCR_V, false);
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
