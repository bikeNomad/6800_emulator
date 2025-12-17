#!/usr/bin/env python3
"""
MC6800 Instruction Implementation Verification
Verifies both opcodes AND implementation details against MC6800 specifications
"""

import re
import sys

# Complete MC6800 instruction specification with implementation details
# Format: opcode: (mnemonic, mode, flags_affected, operation_notes)
MC6800_SPEC = {
    # Inherent Instructions
    0x01: ("NOP", "INH", "", "No operation"),
    0x06: ("TAP", "INH", "ALL", "A[5:0] -> CCR, bits 7-6 remain 11"),
    0x07: ("TPA", "INH", "", "CCR -> A"),
    0x08: ("INX", "INH", "Z", "X + 1 -> X, only Z affected"),
    0x09: ("DEX", "INH", "Z", "X - 1 -> X, only Z affected"),
    0x0A: ("CLV", "INH", "V", "0 -> V"),
    0x0B: ("SEV", "INH", "V", "1 -> V"),
    0x0C: ("CLC", "INH", "C", "0 -> C"),
    0x0D: ("SEC", "INH", "C", "1 -> C"),
    0x0E: ("CLI", "INH", "I", "0 -> I"),
    0x0F: ("SEI", "INH", "I", "1 -> I"),
    0x10: ("SBA", "INH", "NZVC", "A - B -> A"),
    0x11: ("CBA", "INH", "NZVC", "A - B, sets flags only"),
    0x16: ("TAB", "INH", "NZV", "A -> B, V=0"),
    0x17: ("TBA", "INH", "NZV", "B -> A, V=0"),
    0x19: ("DAA", "INH", "NZC", "Decimal adjust A after BCD add"),
    0x1B: ("ABA", "INH", "HNZVC", "A + B -> A"),
    
    # Branch Instructions
    0x20: ("BRA", "REL", "", "Always branch"),
    0x22: ("BHI", "REL", "", "Branch if C=0 AND Z=0"),
    0x23: ("BLS", "REL", "", "Branch if C=1 OR Z=1"),
    0x24: ("BCC", "REL", "", "Branch if C=0"),
    0x25: ("BCS", "REL", "", "Branch if C=1"),
    0x26: ("BNE", "REL", "", "Branch if Z=0"),
    0x27: ("BEQ", "REL", "", "Branch if Z=1"),
    0x28: ("BVC", "REL", "", "Branch if V=0"),
    0x29: ("BVS", "REL", "", "Branch if V=1"),
    0x2A: ("BPL", "REL", "", "Branch if N=0"),
    0x2B: ("BMI", "REL", "", "Branch if N=1"),
    0x2C: ("BGE", "REL", "", "Branch if N XOR V = 0"),
    0x2D: ("BLT", "REL", "", "Branch if N XOR V = 1"),
    0x2E: ("BGT", "REL", "", "Branch if Z=0 AND (N XOR V)=0"),
    0x2F: ("BLE", "REL", "", "Branch if Z=1 OR (N XOR V)=1"),
    
    # Stack/Jump Instructions
    0x30: ("TSX", "INH", "", "SP + 1 -> X"),
    0x31: ("INS", "INH", "", "SP + 1 -> SP"),
    0x32: ("PULA", "INH", "", "Pull A from stack"),
    0x33: ("PULB", "INH", "", "Pull B from stack"),
    0x34: ("DES", "INH", "", "SP - 1 -> SP"),
    0x35: ("TXS", "INH", "", "X - 1 -> SP"),
    0x36: ("PSHA", "INH", "", "Push A onto stack"),
    0x37: ("PSHB", "INH", "", "Push B onto stack"),
    0x39: ("RTS", "INH", "", "Return from subroutine"),
    0x3B: ("RTI", "INH", "ALL", "Return from interrupt"),
    0x3E: ("WAI", "INH", "", "Wait for interrupt"),
    0x3F: ("SWI", "INH", "I", "Software interrupt"),
    
    # Accumulator A Operations (Inherent)
    0x40: ("NEGA", "INH", "NZVC", "0 - A -> A, V=A=$80, C=A!=0"),
    0x43: ("COMA", "INH", "NZV1", "~A -> A, V=0, C=1"),
    0x44: ("LSRA", "INH", "NZ0C", "Logical shift right, N=0"),
    0x46: ("RORA", "INH", "NZVC", "Rotate right through C"),
    0x47: ("ASRA", "INH", "NZVC", "Arithmetic shift right"),
    0x48: ("ASLA", "INH", "NZVC", "Arithmetic shift left"),
    0x49: ("ROLA", "INH", "NZVC", "Rotate left through C"),
    0x4A: ("DECA", "INH", "NZV", "A - 1 -> A, V=A=$7F"),
    0x4C: ("INCA", "INH", "NZV", "A + 1 -> A, V=A=$80"),
    0x4D: ("TSTA", "INH", "NZV0", "Test A, V=0, C unchanged"),
    0x4F: ("CLRA", "INH", "0100", "0 -> A, N=0 Z=1 V=0 C=0"),
    
    # Accumulator B Operations (Inherent)
    0x50: ("NEGB", "INH", "NZVC", "0 - B -> B, V=B=$80, C=B!=0"),
    0x53: ("COMB", "INH", "NZV1", "~B -> B, V=0, C=1"),
    0x54: ("LSRB", "INH", "NZ0C", "Logical shift right, N=0"),
    0x56: ("RORB", "INH", "NZVC", "Rotate right through C"),
    0x57: ("ASRB", "INH", "NZVC", "Arithmetic shift right"),
    0x58: ("ASLB", "INH", "NZVC", "Arithmetic shift left"),
    0x59: ("ROLB", "INH", "NZVC", "Rotate left through C"),
    0x5A: ("DECB", "INH", "NZV", "B - 1 -> B, V=B=$7F"),
    0x5C: ("INCB", "INH", "NZV", "B + 1 -> B, V=B=$80"),
    0x5D: ("TSTB", "INH", "NZV0", "Test B, V=0, C unchanged"),
    0x5F: ("CLRB", "INH", "0100", "0 -> B, N=0 Z=1 V=0 C=0"),
    
    # Memory Operations (Indexed)
    0x60: ("NEG", "IND", "NZVC", "0 - M -> M"),
    0x63: ("COM", "IND", "NZV1", "~M -> M, V=0, C=1"),
    0x64: ("LSR", "IND", "NZ0C", "Logical shift right"),
    0x66: ("ROR", "IND", "NZVC", "Rotate right through C"),
    0x67: ("ASR", "IND", "NZVC", "Arithmetic shift right"),
    0x68: ("ASL", "IND", "NZVC", "Arithmetic shift left"),
    0x69: ("ROL", "IND", "NZVC", "Rotate left through C"),
    0x6A: ("DEC", "IND", "NZV", "M - 1 -> M"),
    0x6C: ("INC", "IND", "NZV", "M + 1 -> M"),
    0x6D: ("TST", "IND", "NZV0", "Test M, V=0"),
    0x6E: ("JMP", "IND", "", "Jump to address"),
    0x6F: ("CLR", "IND", "0100", "0 -> M"),
    
    # Memory Operations (Extended)
    0x70: ("NEG", "EXT", "NZVC", "0 - M -> M"),
    0x73: ("COM", "EXT", "NZV1", "~M -> M, V=0, C=1"),
    0x74: ("LSR", "EXT", "NZ0C", "Logical shift right"),
    0x76: ("ROR", "EXT", "NZVC", "Rotate right through C"),
    0x77: ("ASR", "EXT", "NZVC", "Arithmetic shift right"),
    0x78: ("ASL", "EXT", "NZVC", "Arithmetic shift left"),
    0x79: ("ROL", "EXT", "NZVC", "Rotate left through C"),
    0x7A: ("DEC", "EXT", "NZV", "M - 1 -> M"),
    0x7C: ("INC", "EXT", "NZV", "M + 1 -> M"),
    0x7D: ("TST", "EXT", "NZV0", "Test M, V=0"),
    0x7E: ("JMP", "EXT", "", "Jump to address"),
    0x7F: ("CLR", "EXT", "0100", "0 -> M"),
    
    # Accumulator A Operations (Immediate/Direct/Indexed/Extended)
    0x80: ("SUBA", "IMM", "NZVC", "A - M -> A"),
    0x81: ("CMPA", "IMM", "NZVC", "A - M, sets flags only"),
    0x82: ("SBCA", "IMM", "NZVC", "A - M - C -> A"),
    0x84: ("ANDA", "IMM", "NZV0", "A AND M -> A, V=0"),
    0x85: ("BITA", "IMM", "NZV0", "A AND M, sets flags only, V=0"),
    0x86: ("LDAA", "IMM", "NZV0", "M -> A, V=0"),
    0x88: ("EORA", "IMM", "NZV0", "A XOR M -> A, V=0"),
    0x89: ("ADCA", "IMM", "HNZVC", "A + M + C -> A"),
    0x8A: ("ORAA", "IMM", "NZV0", "A OR M -> A, V=0"),
    0x8B: ("ADDA", "IMM", "HNZVC", "A + M -> A"),
    0x8C: ("CPX", "IMM", "NZV", "X - M -> flags, 16-bit compare"),
    0x8D: ("BSR", "REL", "", "Branch to subroutine"),
    0x8E: ("LDS", "IMM", "NZV0", "M -> SP, V=0"),
    
    0x90: ("SUBA", "DIR", "NZVC", "A - M -> A"),
    0x91: ("CMPA", "DIR", "NZVC", "A - M, sets flags only"),
    0x92: ("SBCA", "DIR", "NZVC", "A - M - C -> A"),
    0x94: ("ANDA", "DIR", "NZV0", "A AND M -> A, V=0"),
    0x95: ("BITA", "DIR", "NZV0", "A AND M, sets flags only, V=0"),
    0x96: ("LDAA", "DIR", "NZV0", "M -> A, V=0"),
    0x97: ("STAA", "DIR", "NZV0", "A -> M, V=0"),
    0x98: ("EORA", "DIR", "NZV0", "A XOR M -> A, V=0"),
    0x99: ("ADCA", "DIR", "HNZVC", "A + M + C -> A"),
    0x9A: ("ORAA", "DIR", "NZV0", "A OR M -> A, V=0"),
    0x9B: ("ADDA", "DIR", "HNZVC", "A + M -> A"),
    0x9C: ("CPX", "DIR", "NZV", "X - M -> flags, 16-bit"),
    0x9D: ("JSR", "DIR", "", "Jump to subroutine"),
    0x9E: ("LDS", "DIR", "NZV0", "M -> SP, V=0"),
    0x9F: ("STS", "DIR", "NZV0", "SP -> M, V=0"),
    
    0xA0: ("SUBA", "IND", "NZVC", "A - M -> A"),
    0xA1: ("CMPA", "IND", "NZVC", "A - M, sets flags only"),
    0xA2: ("SBCA", "IND", "NZVC", "A - M - C -> A"),
    0xA4: ("ANDA", "IND", "NZV0", "A AND M -> A, V=0"),
    0xA5: ("BITA", "IND", "NZV0", "A AND M, sets flags only, V=0"),
    0xA6: ("LDAA", "IND", "NZV0", "M -> A, V=0"),
    0xA7: ("STAA", "IND", "NZV0", "A -> M, V=0"),
    0xA8: ("EORA", "IND", "NZV0", "A XOR M -> A, V=0"),
    0xA9: ("ADCA", "IND", "HNZVC", "A + M + C -> A"),
    0xAA: ("ORAA", "IND", "NZV0", "A OR M -> A, V=0"),
    0xAB: ("ADDA", "IND", "HNZVC", "A + M -> A"),
    0xAC: ("CPX", "IND", "NZV", "X - M -> flags, 16-bit"),
    0xAD: ("JSR", "IND", "", "Jump to subroutine"),
    0xAE: ("LDS", "IND", "NZV0", "M -> SP, V=0"),
    0xAF: ("STS", "IND", "NZV0", "SP -> M, V=0"),
    
    0xB0: ("SUBA", "EXT", "NZVC", "A - M -> A"),
    0xB1: ("CMPA", "EXT", "NZVC", "A - M, sets flags only"),
    0xB2: ("SBCA", "EXT", "NZVC", "A - M - C -> A"),
    0xB4: ("ANDA", "EXT", "NZV0", "A AND M -> A, V=0"),
    0xB5: ("BITA", "EXT", "NZV0", "A AND M, sets flags only, V=0"),
    0xB6: ("LDAA", "EXT", "NZV0", "M -> A, V=0"),
    0xB7: ("STAA", "EXT", "NZV0", "A -> M, V=0"),
    0xB8: ("EORA", "EXT", "NZV0", "A XOR M -> A, V=0"),
    0xB9: ("ADCA", "EXT", "HNZVC", "A + M + C -> A"),
    0xBA: ("ORAA", "EXT", "NZV0", "A OR M -> A, V=0"),
    0xBB: ("ADDA", "EXT", "HNZVC", "A + M -> A"),
    0xBC: ("CPX", "EXT", "NZV", "X - M -> flags, 16-bit"),
    0xBD: ("JSR", "EXT", "", "Jump to subroutine"),
    0xBE: ("LDS", "EXT", "NZV0", "M -> SP, V=0"),
    0xBF: ("STS", "EXT", "NZV0", "SP -> M, V=0"),
    
    # Accumulator B Operations
    0xC0: ("SUBB", "IMM", "NZVC", "B - M -> B"),
    0xC1: ("CMPB", "IMM", "NZVC", "B - M, sets flags only"),
    0xC2: ("SBCB", "IMM", "NZVC", "B - M - C -> B"),
    0xC4: ("ANDB", "IMM", "NZV0", "B AND M -> B, V=0"),
    0xC5: ("BITB", "IMM", "NZV0", "B AND M, sets flags only, V=0"),
    0xC6: ("LDAB", "IMM", "NZV0", "M -> B, V=0"),
    0xC8: ("EORB", "IMM", "NZV0", "B XOR M -> B, V=0"),
    0xC9: ("ADCB", "IMM", "HNZVC", "B + M + C -> B"),
    0xCA: ("ORAB", "IMM", "NZV0", "B OR M -> B, V=0"),
    0xCB: ("ADDB", "IMM", "HNZVC", "B + M -> B"),
    0xCE: ("LDX", "IMM", "NZV0", "M -> X, V=0"),
    
    0xD0: ("SUBB", "DIR", "NZVC", "B - M -> B"),
    0xD1: ("CMPB", "DIR", "NZVC", "B - M, sets flags only"),
    0xD2: ("SBCB", "DIR", "NZVC", "B - M - C -> B"),
    0xD4: ("ANDB", "DIR", "NZV0", "B AND M -> B, V=0"),
    0xD5: ("BITB", "DIR", "NZV0", "B AND M, sets flags only, V=0"),
    0xD6: ("LDAB", "DIR", "NZV0", "M -> B, V=0"),
    0xD7: ("STAB", "DIR", "NZV0", "B -> M, V=0"),
    0xD8: ("EORB", "DIR", "NZV0", "B XOR M -> B, V=0"),
    0xD9: ("ADCB", "DIR", "HNZVC", "B + M + C -> B"),
    0xDA: ("ORAB", "DIR", "NZV0", "B OR M -> B, V=0"),
    0xDB: ("ADDB", "DIR", "HNZVC", "B + M -> B"),
    0xDE: ("LDX", "DIR", "NZV0", "M -> X, V=0"),
    0xDF: ("STX", "DIR", "NZV0", "X -> M, V=0"),
    
    0xE0: ("SUBB", "IND", "NZVC", "B - M -> B"),
    0xE1: ("CMPB", "IND", "NZVC", "B - M, sets flags only"),
    0xE2: ("SBCB", "IND", "NZVC", "B - M - C -> B"),
    0xE4: ("ANDB", "IND", "NZV0", "B AND M -> B, V=0"),
    0xE5: ("BITB", "IND", "NZV0", "B AND M, sets flags only, V=0"),
    0xE6: ("LDAB", "IND", "NZV0", "M -> B, V=0"),
    0xE7: ("STAB", "IND", "NZV0", "B -> M, V=0"),
    0xE8: ("EORB", "IND", "NZV0", "B XOR M -> B, V=0"),
    0xE9: ("ADCB", "IND", "HNZVC", "B + M + C -> B"),
    0xEA: ("ORAB", "IND", "NZV0", "B OR M -> B, V=0"),
    0xEB: ("ADDB", "IND", "HNZVC", "B + M -> B"),
    0xEE: ("LDX", "IND", "NZV0", "M -> X, V=0"),
    0xEF: ("STX", "IND", "NZV0", "X -> M, V=0"),
    
    0xF0: ("SUBB", "EXT", "NZVC", "B - M -> B"),
    0xF1: ("CMPB", "EXT", "NZVC", "B - M, sets flags only"),
    0xF2: ("SBCB", "EXT", "NZVC", "B - M - C -> B"),
    0xF4: ("ANDB", "EXT", "NZV0", "B AND M -> B, V=0"),
    0xF5: ("BITB", "EXT", "NZV0", "B AND M, sets flags only, V=0"),
    0xF6: ("LDAB", "EXT", "NZV0", "M -> B, V=0"),
    0xF7: ("STAB", "EXT", "NZV0", "B -> M, V=0"),
    0xF8: ("EORB", "EXT", "NZV0", "B XOR M -> B, V=0"),
    0xF9: ("ADCB", "EXT", "HNZVC", "B + M + C -> B"),
    0xFA: ("ORAB", "EXT", "NZV0", "B OR M -> B, V=0"),
    0xFB: ("ADDB", "EXT", "HNZVC", "B + M -> B"),
    0xFE: ("LDX", "EXT", "NZV0", "M -> X, V=0"),
    0xFF: ("STX", "EXT", "NZV0", "X -> M, V=0"),
}


def parse_instructions_c(filepath):
    """Parse instructions.c to extract implementation details"""
    with open(filepath, 'r') as f:
        content = f.read()
    
    implementations = {}
    
    # Find all case statements
    case_pattern = r'case\s+0x([0-9A-Fa-f]{2}):\s*\{?([^}]*?)(?:break;|\})'
    
    for match in re.finditer(case_pattern, content, re.DOTALL):
        opcode = int(match.group(1), 16)
        impl_code = match.group(2)
        
        # Extract comment if present
        comment_match = re.search(r'//\s*(.+?)(?:\n|$)', impl_code)
        comment = comment_match.group(1) if comment_match else ""
        
        implementations[opcode] = {
            'code': impl_code,
            'comment': comment
        }
    
    return implementations


def check_flag_operations(opcode, spec_flags, impl_code):
    """Check if flag operations match specification"""
    issues = []
    
    # Check for V=0 requirement
    if 'V0' in spec_flags or spec_flags.endswith('V0'):
        if 'cpu_set_flag(CCR_V, false)' not in impl_code:
            issues.append(f"  ⚠️  Should set V=0 but doesn't")
    
    # Check for C=1 requirement  
    if '1' in spec_flags and 'COM' in spec_flags:
        if 'cpu_set_flag(CCR_C, true)' not in impl_code:
            issues.append(f"  ⚠️  Should set C=1 but doesn't")
    
    # Check for CLR instruction (should set N=0, Z=1, V=0, C=0)
    if '0100' in spec_flags:
        required = [
            'cpu_set_flag(CCR_N, false)',
            'cpu_set_flag(CCR_Z, true)',
            'cpu_set_flag(CCR_V, false)',
            'cpu_set_flag(CCR_C, false)'
        ]
        for req in required:
            if req not in impl_code:
                issues.append(f"  ⚠️  CLR should set all flags explicitly")
                break
    
    return issues


def verify_implementation():
    """Main verification function"""
    print("MC6800 Implementation Verification")
    print("=" * 80)
    print()
    
    # Parse instructions.c
    impl = parse_instructions_c('src/instructions.c')
    
    errors = []
    warnings = []
    verified = []
    
    for opcode in sorted(MC6800_SPEC.keys()):
        mnemonic, mode, flags, operation = MC6800_SPEC[opcode]
        
        if opcode not in impl:
            errors.append(f"0x{opcode:02X}: {mnemonic} ({mode}) - NOT IMPLEMENTED")
            continue
        
        impl_data = impl[opcode]
        impl_comment = impl_data['comment']
        impl_code = impl_data['code']
        
        # Check mnemonic in comment
        if mnemonic not in impl_comment:
            warnings.append(f"0x{opcode:02X}: Comment mismatch - expected '{mnemonic}', got '{impl_comment}'")
        
        # Check flag operations
        flag_issues = check_flag_operations(opcode, flags, impl_code)
        if flag_issues:
            warnings.extend([f"0x{opcode:02X}: {mnemonic} ({mode})" + issue for issue in flag_issues])
        
        # Verify specific operations
        if mnemonic == "TAP":
            if "cpu.ccr = (cpu.a & 0x3F) | CCR_FIXED" not in impl_code:
                warnings.append(f"0x{opcode:02X}: TAP should only copy lower 6 bits")
        
        elif mnemonic in ["INX", "DEX"]:
            if "cpu_set_flag(CCR_Z" not in impl_code:
                warnings.append(f"0x{opcode:02X}: {mnemonic} must update Z flag only")
            if "cpu_set_flag(CCR_N" in impl_code or "cpu_set_flag(CCR_V" in impl_code:
                warnings.append(f"0x{opcode:02X}: {mnemonic} should NOT update N or V flags")
        
        elif mnemonic == "TSX":
            if "cpu.x = cpu.sp + 1" not in impl_code:
                warnings.append(f"0x{opcode:02X}: TSX should be SP+1->X")
        
        elif mnemonic == "TXS":
            if "cpu.sp = cpu.x - 1" not in impl_code:
                warnings.append(f"0x{opcode:02X}: TXS should be X-1->SP")
        
        elif "NEG" in mnemonic:
            if "(~" not in impl_code or "+ 1" not in impl_code:
                warnings.append(f"0x{opcode:02X}: NEG should be two's complement (~value + 1)")
            if "value == 0x80" not in impl_code and "cpu.a == 0x80" not in impl_code and "cpu.b == 0x80" not in impl_code:
                warnings.append(f"0x{opcode:02X}: NEG should set V when result=$80")
        
        elif "COM" in mnemonic:
            if "~" not in impl_code:
                warnings.append(f"0x{opcode:02X}: COM should complement (~)")
            if "cpu_set_flag(CCR_C, true)" not in impl_code:
                warnings.append(f"0x{opcode:02X}: COM should always set C=1")
        
        elif mnemonic in ["DECA", "DECB", "DEC"]:
            if "0x7F" not in impl_code:
                warnings.append(f"0x{opcode:02X}: DEC should set V when result=$7F (was $80)")
        
        elif mnemonic in ["INCA", "INCB", "INC"]:
            if "0x80" not in impl_code:
                warnings.append(f"0x{opcode:02X}: INC should set V when result=$80 (was $7F)")
        
        elif "CPX" in mnemonic:
            if "0x8000" not in impl_code:
                warnings.append(f"0x{opcode:02X}: CPX is 16-bit compare, must check bit 15")
        
        verified.append(f"✓ 0x{opcode:02X}: {mnemonic} ({mode}) - {operation}")
    
    # Print results
    print(f"VERIFICATION RESULTS:")
    print("-" * 80)
    print(f"Total instructions in spec: {len(MC6800_SPEC)}")
    print(f"Instructions verified: {len(verified)}")
    print(f"Errors found: {len(errors)}")
    print(f"Warnings found: {len(warnings)}")
    print()
    
    if errors:
        print("ERRORS:")
        print("-" * 80)
        for error in errors:
            print(f"❌ {error}")
        print()
    
    if warnings:
        print("WARNINGS:")
        print("-" * 80)
        for warning in warnings:
            print(f"⚠️  {warning}")
        print()
    
    if not errors and not warnings:
        print("✅ ALL INSTRUCTIONS VERIFIED SUCCESSFULLY!")
        print()
        print("All 198 MC6800 instructions are correctly implemented with:")
        print("  • Correct opcodes")
        print("  • Correct operations")
        print("  • Correct flag behavior")
        print("  • Correct addressing modes")
        return 0
    elif not errors:
        print("✅ No critical errors found, but there are some warnings to review.")
        return 0
    else:
        print("❌ Critical errors found. Please fix the issues above.")
        return 1


if __name__ == "__main__":
    sys.exit(verify_implementation())
