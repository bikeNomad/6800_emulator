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
Intel HEX file generator for MC6800 test programs
Generates proper Intel HEX format with checksums
"""

def calculate_checksum(record_bytes):
    """Calculate Intel HEX checksum (two's complement of sum)"""
    checksum = sum(record_bytes) & 0xFF
    return (~checksum + 1) & 0xFF

def make_data_record(address, data):
    """Create an Intel HEX data record"""
    record_type = 0x00  # Data record
    byte_count = len(data)

    # Build record: byte_count, address (2 bytes), type, data
    record_bytes = [byte_count, (address >> 8) & 0xFF, address & 0xFF, record_type]
    record_bytes.extend(data)

    checksum = calculate_checksum(record_bytes)

    # Format as HEX string
    hex_str = ':'
    for byte in record_bytes:
        hex_str += f'{byte:02X}'
    hex_str += f'{checksum:02X}'

    return hex_str

def make_reset_vector(address):
    """Create reset vector at 0xFFFE pointing to given address"""
    data = [(address >> 8) & 0xFF, address & 0xFF]
    return make_data_record(0xFFFE, data)

def make_eof_record():
    """Create Intel HEX EOF record"""
    return ':00000001FF'

def bytes_to_hex(program, start_address, reset_address=None):
    """Convert program bytes to Intel HEX format

    Args:
        program: List of byte values
        start_address: Where to load the program in memory
        reset_address: Optional reset vector address (defaults to start_address)

    Returns:
        String containing complete Intel HEX file
    """
    if reset_address is None:
        reset_address = start_address

    lines = []

    # Split program into 16-byte chunks for data records
    offset = 0
    while offset < len(program):
        chunk_size = min(16, len(program) - offset)
        chunk = program[offset:offset + chunk_size]
        record = make_data_record(start_address + offset, chunk)
        lines.append(record)
        offset += chunk_size

    # Add reset vector
    lines.append(make_reset_vector(reset_address))

    # Add EOF
    lines.append(make_eof_record())

    return '\n'.join(lines)

def main():
    """Generate test program HEX files"""

    # Test 1: Basic Loads (Immediate Mode)
    test1 = [
        0x86, 0x42,              # LDAA #$42
        0xC6, 0x24,              # LDAB #$24
        0xCE, 0x12, 0x34,        # LDX  #$1234
        0x20, 0xFE,              # BRA  * (loop forever)
    ]
    test1_hex = bytes_to_hex(test1, 0xE000)
    with open('../tests/test1_basic.hex', 'w') as f:
        f.write(test1_hex + '\n')
    print('Created test1_basic.hex')

    # Test 2: Arithmetic & Memory
    test2 = [
        0x86, 0x10,              # LDAA #$10
        0xC6, 0x20,              # LDAB #$20
        0x1B,                    # ABA
        0x8B, 0x01,              # ADDA #$01
        0xB7, 0x01, 0x00,        # STAA $0100
        0x20, 0xFE,              # BRA  * (loop forever)
    ]
    test2_hex = bytes_to_hex(test2, 0xE000)
    with open('../tests/test2_arithmetic.hex', 'w') as f:
        f.write(test2_hex + '\n')
    print('Created test2_arithmetic.hex')

    # Test 3: Stack & Subroutines
    test3 = [
        0x8E, 0x01, 0xFF,        # LDS  #$01FF
        0x86, 0xAA,              # LDAA #$AA
        0x36,                    # PSHA
        0x86, 0x55,              # LDAA #$55
        0x32,                    # PULA
        0xBD, 0xE0, 0x0F,        # JSR  SUB ($E00F)
        0x20, 0xFE,              # BRA  * (loop forever)
        0x00,                    # padding
        0xC6, 0xBB,              # SUB: LDAB #$BB
        0x39,                    # RTS
    ]
    test3_hex = bytes_to_hex(test3, 0xE000)
    with open('../tests/test3_stack.hex', 'w') as f:
        f.write(test3_hex + '\n')
    print('Created test3_stack.hex')

    print('\nAll test HEX files created successfully!')

if __name__ == '__main__':
    main()
