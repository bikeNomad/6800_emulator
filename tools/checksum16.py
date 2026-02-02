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
Simple 16-bit checksum calculator for files.
Computes a 16-bit checksum by summing all bytes in each file.
"""

import sys


def compute_checksum16(filepath):
    """
    Compute a simple 16-bit checksum of a file.
    
    Args:
        filepath: Path to the file to checksum
        
    Returns:
        16-bit checksum value (0x0000 - 0xFFFF)
        
    Raises:
        FileNotFoundError: If the file doesn't exist
        IOError: If there's an error reading the file
    """
    checksum = 0
    
    with open(filepath, 'rb') as f:
        while True:
            chunk = f.read(4096)  # Read in 4KB chunks for efficiency
            if not chunk:
                break
            # Sum all bytes in the chunk
            for byte in chunk:
                checksum += byte
    
    # Keep only the lower 16 bits
    return checksum & 0xFFFF


def main():
    if len(sys.argv) < 2:
        print("Usage: checksum16.py <file1> [file2] [file3] ...", file=sys.stderr)
        print("\nComputes a simple 16-bit checksum for one or more files.", file=sys.stderr)
        sys.exit(1)
    
    files = sys.argv[1:]
    errors = False
    
    for filepath in files:
        try:
            checksum = compute_checksum16(filepath)
            print(f"{checksum:04x}  {filepath}")
        except FileNotFoundError:
            print(f"Error: File not found: {filepath}", file=sys.stderr)
            errors = True
        except IOError as e:
            print(f"Error reading {filepath}: {e}", file=sys.stderr)
            errors = True
    
    if errors:
        sys.exit(1)


if __name__ == "__main__":
    main()
