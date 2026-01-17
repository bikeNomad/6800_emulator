#!/usr/bin/env python3
"""
Simple 32-bit checksum calculator for files.
Computes a 32-bit checksum by summing all bytes in each file.
"""

import sys


def compute_checksum16(filepath):
    """
    Compute a simple 32-bit checksum of a file.
    
    Args:
        filepath: Path to the file to checksum
        
    Returns:
        32-bit checksum value (0x0000 - 0xFFFF)
        
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
    
    # Keep only the lower 32 bits
    return checksum & 0xFFFF_FFFF


def main():
    if len(sys.argv) < 2:
        print("Usage: checksum16.py <file1> [file2] [file3] ...", file=sys.stderr)
        print("\nComputes a simple 32-bit checksum for one or more files.", file=sys.stderr)
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
