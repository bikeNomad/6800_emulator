#!/usr/bin/env python3

# SPDX-License-Identifier: MIT
#
# Copyright 2026 Ned Konz <ned@metamagix.tech>
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""
MC6800 Opcode Verification Script
Compares instructions.c implementation against MC6800 specification
"""

# MC6800 Complete Opcode Table (from official specification)
MC6800_OPCODES = {
    # Inherent/Implied mode (single byte instructions)
    0x01: ("NOP", "INH"),
    0x06: ("TAP", "INH"),
    0x07: ("TPA", "INH"),
    0x08: ("INX", "INH"),
    0x09: ("DEX", "INH"),
    0x0A: ("CLV", "INH"),
    0x0B: ("SEV", "INH"),
    0x0C: ("CLC", "INH"),
    0x0D: ("SEC", "INH"),
    0x0E: ("CLI", "INH"),
    0x0F: ("SEI", "INH"),
    0x10: ("SBA", "INH"),
    0x11: ("CBA", "INH"),
    0x16: ("TAB", "INH"),
    0x17: ("TBA", "INH"),
    0x19: ("DAA", "INH"),
    0x1B: ("ABA", "INH"),
    
    # Branch instructions
    0x20: ("BRA", "REL"),
    0x22: ("BHI", "REL"),
    0x23: ("BLS", "REL"),
    0x24: ("BCC", "REL"),
    0x25: ("BCS", "REL"),
    0x26: ("BNE", "REL"),
    0x27: ("BEQ", "REL"),
    0x28: ("BVC", "REL"),
    0x29: ("BVS", "REL"),
    0x2A: ("BPL", "REL"),
    0x2B: ("BMI", "REL"),
    0x2C: ("BGE", "REL"),
    0x2D: ("BLT", "REL"),
    0x2E: ("BGT", "REL"),
    0x2F: ("BLE", "REL"),
    
    # Stack and Register operations
    0x30: ("TSX", "INH"),
    0x31: ("INS", "INH"),
    0x32: ("PULA", "INH"),
    0x33: ("PULB", "INH"),
    0x34: ("DES", "INH"),
    0x35: ("TXS", "INH"),
    0x36: ("PSHA", "INH"),
    0x37: ("PSHB", "INH"),
    0x39: ("RTS", "INH"),
    0x3B: ("RTI", "INH"),
    0x3E: ("WAI", "INH"),
    0x3F: ("SWI", "INH"),
    
    # Accumulator A operations - Inherent
    0x40: ("NEGA", "INH"),
    0x43: ("COMA", "INH"),
    0x44: ("LSRA", "INH"),
    0x46: ("RORA", "INH"),
    0x47: ("ASRA", "INH"),
    0x48: ("ASLA", "INH"),
    0x49: ("ROLA", "INH"),
    0x4A: ("DECA", "INH"),
    0x4C: ("INCA", "INH"),
    0x4D: ("TSTA", "INH"),
    0x4F: ("CLRA", "INH"),
    
    # Accumulator B operations - Inherent
    0x50: ("NEGB", "INH"),
    0x53: ("COMB", "INH"),
    0x54: ("LSRB", "INH"),
    0x56: ("RORB", "INH"),
    0x57: ("ASRB", "INH"),
    0x58: ("ASLB", "INH"),
    0x59: ("ROLB", "INH"),
    0x5A: ("DECB", "INH"),
    0x5C: ("INCB", "INH"),
    0x5D: ("TSTB", "INH"),
    0x5F: ("CLRB", "INH"),
    
    # Memory operations - Indexed
    0x60: ("NEG", "IND"),
    0x63: ("COM", "IND"),
    0x64: ("LSR", "IND"),
    0x66: ("ROR", "IND"),
    0x67: ("ASR", "IND"),
    0x68: ("ASL", "IND"),
    0x69: ("ROL", "IND"),
    0x6A: ("DEC", "IND"),
    0x6C: ("INC", "IND"),
    0x6D: ("TST", "IND"),
    0x6E: ("JMP", "IND"),
    0x6F: ("CLR", "IND"),
    
    # Memory operations - Extended
    0x70: ("NEG", "EXT"),
    0x73: ("COM", "EXT"),
    0x74: ("LSR", "EXT"),
    0x76: ("ROR", "EXT"),
    0x77: ("ASR", "EXT"),
    0x78: ("ASL", "EXT"),
    0x79: ("ROL", "EXT"),
    0x7A: ("DEC", "EXT"),
    0x7C: ("INC", "EXT"),
    0x7D: ("TST", "EXT"),
    0x7E: ("JMP", "EXT"),
    0x7F: ("CLR", "EXT"),
    
    # Accumulator A - Immediate
    0x80: ("SUBA", "IMM"),
    0x81: ("CMPA", "IMM"),
    0x82: ("SBCA", "IMM"),
    0x84: ("ANDA", "IMM"),
    0x85: ("BITA", "IMM"),
    0x86: ("LDAA", "IMM"),
    0x88: ("EORA", "IMM"),
    0x89: ("ADCA", "IMM"),
    0x8A: ("ORAA", "IMM"),
    0x8B: ("ADDA", "IMM"),
    0x8C: ("CPX", "IMM"),
    0x8D: ("BSR", "REL"),
    0x8E: ("LDS", "IMM"),
    
    # Accumulator A - Direct
    0x90: ("SUBA", "DIR"),
    0x91: ("CMPA", "DIR"),
    0x92: ("SBCA", "DIR"),
    0x94: ("ANDA", "DIR"),
    0x95: ("BITA", "DIR"),
    0x96: ("LDAA", "DIR"),
    0x97: ("STAA", "DIR"),
    0x98: ("EORA", "DIR"),
    0x99: ("ADCA", "DIR"),
    0x9A: ("ORAA", "DIR"),
    0x9B: ("ADDA", "DIR"),
    0x9C: ("CPX", "DIR"),
    0x9D: ("JSR", "DIR"),
    0x9E: ("LDS", "DIR"),
    0x9F: ("STS", "DIR"),
    
    # Accumulator A - Indexed
    0xA0: ("SUBA", "IND"),
    0xA1: ("CMPA", "IND"),
    0xA2: ("SBCA", "IND"),
    0xA4: ("ANDA", "IND"),
    0xA5: ("BITA", "IND"),
    0xA6: ("LDAA", "IND"),
    0xA7: ("STAA", "IND"),
    0xA8: ("EORA", "IND"),
    0xA9: ("ADCA", "IND"),
    0xAA: ("ORAA", "IND"),
    0xAB: ("ADDA", "IND"),
    0xAC: ("CPX", "IND"),
    0xAD: ("JSR", "IND"),
    0xAE: ("LDS", "IND"),
    0xAF: ("STS", "IND"),
    
    # Accumulator A - Extended
    0xB0: ("SUBA", "EXT"),
    0xB1: ("CMPA", "EXT"),
    0xB2: ("SBCA", "EXT"),
    0xB4: ("ANDA", "EXT"),
    0xB5: ("BITA", "EXT"),
    0xB6: ("LDAA", "EXT"),
    0xB7: ("STAA", "EXT"),
    0xB8: ("EORA", "EXT"),
    0xB9: ("ADCA", "EXT"),
    0xBA: ("ORAA", "EXT"),
    0xBB: ("ADDA", "EXT"),
    0xBC: ("CPX", "EXT"),
    0xBD: ("JSR", "EXT"),
    0xBE: ("LDS", "EXT"),
    0xBF: ("STS", "EXT"),
    
    # Accumulator B - Immediate
    0xC0: ("SUBB", "IMM"),
    0xC1: ("CMPB", "IMM"),
    0xC2: ("SBCB", "IMM"),
    0xC4: ("ANDB", "IMM"),
    0xC5: ("BITB", "IMM"),  # *** CRITICAL: This is IMMEDIATE, not DIRECT ***
    0xC6: ("LDAB", "IMM"),
    0xC8: ("EORB", "IMM"),
    0xC9: ("ADCB", "IMM"),
    0xCA: ("ORAB", "IMM"),
    0xCB: ("ADDB", "IMM"),
    0xCE: ("LDX", "IMM"),
    
    # Accumulator B - Direct
    0xD0: ("SUBB", "DIR"),
    0xD1: ("CMPB", "DIR"),
    0xD2: ("SBCB", "DIR"),
    0xD4: ("ANDB", "DIR"),
    0xD5: ("BITB", "DIR"),  # *** This is DIRECT mode ***
    0xD6: ("LDAB", "DIR"),
    0xD7: ("STAB", "DIR"),
    0xD8: ("EORB", "DIR"),
    0xD9: ("ADCB", "DIR"),
    0xDA: ("ORAB", "DIR"),
    0xDB: ("ADDB", "DIR"),
    0xDE: ("LDX", "DIR"),
    0xDF: ("STX", "DIR"),
    
    # Accumulator B - Indexed
    0xE0: ("SUBB", "IND"),
    0xE1: ("CMPB", "IND"),
    0xE2: ("SBCB", "IND"),
    0xE4: ("ANDB", "IND"),
    0xE5: ("BITB", "IND"),  # *** This is INDEXED mode ***
    0xE6: ("LDAB", "IND"),
    0xE7: ("STAB", "IND"),
    0xE8: ("EORB", "IND"),
    0xE9: ("ADCB", "IND"),
    0xEA: ("ORAB", "IND"),
    0xEB: ("ADDB", "IND"),
    0xEE: ("LDX", "IND"),
    0xEF: ("STX", "IND"),
    
    # Accumulator B - Extended
    0xF0: ("SUBB", "EXT"),
    0xF1: ("CMPB", "EXT"),
    0xF2: ("SBCB", "EXT"),
    0xF4: ("ANDB", "EXT"),
    0xF5: ("BITB", "EXT"),  # *** This is EXTENDED mode ***
    0xF6: ("LDAB", "EXT"),
    0xF7: ("STAB", "EXT"),
    0xF8: ("EORB", "EXT"),
    0xF9: ("ADCB", "EXT"),
    0xFA: ("ORAB", "EXT"),
    0xFB: ("ADDB", "EXT"),
    0xFE: ("LDX", "EXT"),
    0xFF: ("STX", "EXT"),
}

# Parse instructions.c to extract what's implemented
import re

def parse_instructions_c(filename):
    """Parse instructions.c to extract implemented opcodes"""
    implemented = {}
    with open(filename, 'r') as f:
        content = f.read()
        
    # Find all case statements
    case_pattern = r'case\s+0x([0-9A-Fa-f]{2}):\s*(?://.*?)?(?:{)?\s*\n\s*(?://\s*(.+?)\n)?'
    matches = re.finditer(case_pattern, content)
    
    for match in matches:
        opcode = int(match.group(1), 16)
        comment = match.group(2) if match.group(2) else ""
        implemented[opcode] = comment.strip()
    
    # Also parse the mnemonic table
    mnemonics = {}
    mnemonic_pattern = r'\[0x([0-9A-Fa-f]{2})\]\s*=\s*"([^"]+)"'
    matches = re.finditer(mnemonic_pattern, content)
    
    for match in matches:
        opcode = int(match.group(1), 16)
        mnemonic = match.group(2)
        mnemonics[opcode] = mnemonic
    
    return implemented, mnemonics

def verify_opcodes():
    """Verify all opcodes against specification"""
    print("MC6800 Opcode Verification Report")
    print("=" * 80)
    print()
    
    implemented, mnemonics = parse_instructions_c('src/instructions.c')
    
    errors = []
    warnings = []
    
    # Check each opcode in spec
    for opcode, (mnemonic, mode) in sorted(MC6800_OPCODES.items()):
        expected_label = f"{mnemonic} ({mode})"
        
        # Check if implemented
        if opcode not in implemented:
            warnings.append(f"0x{opcode:02X}: {expected_label} - NOT IMPLEMENTED in case statements")
            continue
        
        # Check mnemonic table
        if opcode in mnemonics:
            actual_label = mnemonics[opcode]
            if actual_label != expected_label:
                errors.append({
                    'opcode': opcode,
                    'expected': expected_label,
                    'actual': actual_label,
                    'mnemonic': mnemonic,
                    'mode': mode
                })
    
    # Report errors
    if errors:
        print("CRITICAL ERRORS FOUND:")
        print("-" * 80)
        for error in errors:
            print(f"0x{error['opcode']:02X}: {error['mnemonic']}")
            print(f"  Expected: {error['expected']}")
            print(f"  Actual:   {error['actual']}")
            print(f"  >>> ADDRESSING MODE MISMATCH <<<")
            print()
    
    if warnings:
        print("\nWARNINGS:")
        print("-" * 80)
        for warning in warnings:
            print(f"  {warning}")
        print()
    
    # Summary
    print("\nSUMMARY:")
    print("-" * 80)
    print(f"Total opcodes in MC6800 spec: {len(MC6800_OPCODES)}")
    print(f"Opcodes implemented: {len(implemented)}")
    print(f"Critical errors (wrong addressing mode): {len(errors)}")
    print(f"Warnings (not implemented): {len(warnings)}")
    print()
    
    if errors:
        print("=" * 80)
        print("ACTION REQUIRED: Fix addressing mode mismatches above!")
        print("=" * 80)
        return False
    else:
        print("All implemented opcodes match MC6800 specification! ✓")
        return True

if __name__ == "__main__":
    verify_opcodes()
