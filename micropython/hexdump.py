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
Hex Dump Utility

Prints a nicely formatted hex dump of data starting at a specified address.
Format: ADDRESS  HEX_BYTES  |ASCII|
"""


def hexdump(data, start_address=0, bytes_per_line=16):
    """
    Print a formatted hex dump of the given data.
    
    Args:
        data: bytes or bytearray to dump
        start_address: starting address to display (default: 0)
        bytes_per_line: number of bytes to display per line (default: 16)
    """
    if not isinstance(data, (bytes, bytearray)):
        raise TypeError("data must be bytes or bytearray")
    
    if len(data) == 0:
        print("(empty)")
        return
    
    # Calculate the width needed for addresses
    max_address = start_address + len(data)
    addr_width = max(8, len(f"{max_address:X}"))
    
    for i in range(0, len(data), bytes_per_line):
        # Calculate current address
        address = start_address + i
        
        # Get the chunk of bytes for this line
        chunk = data[i:i + bytes_per_line]
        
        # Format address
        addr_str = f"{address:0{addr_width}X}"
        
        # Format hex bytes with grouping (8 bytes per group)
        hex_parts = []
        for j in range(0, bytes_per_line, 8):
            group = chunk[j:j + 8]
            hex_str = ' '.join(f"{b:02X}" for b in group)
            # Pad if incomplete group
            if len(group) < 8:
                hex_str += '   ' * (8 - len(group))
            hex_parts.append(hex_str)
        
        hex_str = '  '.join(hex_parts)
        
        # Format ASCII representation
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        
        # Print the line
        print(f"{addr_str}  {hex_str}  |{ascii_str}|")


def hexdump_from_file(filename, start_address=0, bytes_per_line=16, max_bytes=None):
    """
    Print a hex dump of a file's contents.
    
    Args:
        filename: path to file to dump
        start_address: starting address to display (default: 0)
        bytes_per_line: number of bytes to display per line (default: 16)
        max_bytes: maximum number of bytes to read (default: None = all)
    """
    try:
        with open(filename, 'rb') as f:
            if max_bytes is not None:
                data = f.read(max_bytes)
            else:
                data = f.read()
        
        print(f"Hex dump of '{filename}' ({len(data)} bytes):")
        print()
        hexdump(data, start_address, bytes_per_line)
        
    except FileNotFoundError:
        print(f"Error: File '{filename}' not found")
    except Exception as e:
        print(f"Error reading file: {e}")
