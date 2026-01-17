#!/usr/bin/env python3
"""
Convert binary ROM files to Intel HEX format.

Usage:
    rom2hex.py <start_address> <rom_file1> [rom_file2] [...] > output.hex

Example:
    rom2hex.py D000 rom1.bin rom2.bin > pinball.hex
"""

import sys
import os


def generate_intel_hex_record(address, data):
    """Generate a single Intel HEX record."""
    byte_count = len(data)
    addr_high = (address >> 8) & 0xFF
    addr_low = address & 0xFF
    record_type = 0x00  # Data record

    # Start with byte count, address, and record type
    checksum = byte_count + addr_high + addr_low + record_type

    # Add data bytes to checksum
    for byte in data:
        checksum += byte

    # Two's complement of checksum
    checksum = (~checksum + 1) & 0xFF

    # Format record
    record = f":{byte_count:02X}{addr_high:02X}{addr_low:02X}{record_type:02X}"
    for byte in data:
        record += f"{byte:02X}"
    record += f"{checksum:02X}"

    return record


def generate_eof_record():
    """Generate Intel HEX EOF record."""
    return ":00000001FF"


def check_and_truncate_duplication(binary_data):
    """Check if ROM data has duplication (second half identical to first half).
    If so, return only the first half. Otherwise return original data."""
    data_len = len(binary_data)

    # Check if data length is even and at least 2 bytes
    if data_len < 2 or data_len % 2 != 0:
        return binary_data

    # Split into two halves
    half_len = data_len // 2
    first_half = binary_data[:half_len]
    second_half = binary_data[half_len:]

    # Check if second half is identical to first half
    if first_half == second_half:
        print(
            f"# Detected data duplication: second half identical to first half",
            file=sys.stderr,
        )
        print(f"# Truncating from {data_len} to {half_len} bytes", file=sys.stderr)
        return first_half

    return binary_data


def binary_to_intel_hex(binary_data, start_address, bytes_per_line=16):
    """Convert binary data to Intel HEX format."""
    hex_lines = []
    offset = 0

    while offset < len(binary_data):
        # Get chunk of data (up to bytes_per_line)
        chunk_size = min(bytes_per_line, len(binary_data) - offset)
        chunk = binary_data[offset : offset + chunk_size]

        # Generate record for this chunk
        address = start_address + offset
        hex_lines.append(generate_intel_hex_record(address, chunk))

        offset += chunk_size

    # Add EOF record
    hex_lines.append(generate_eof_record())

    return "\n".join(hex_lines)


def main():
    if len(sys.argv) < 3:
        print(
            "Usage: rom2hex.py <start_address_hex> <rom_file1> [rom_file2] [...]",
            file=sys.stderr,
        )
        print(
            "\nExample: rom2hex.py D000 rom1.bin rom2.bin > output.hex", file=sys.stderr
        )
        sys.exit(1)

    # Parse start address
    try:
        start_address = int(sys.argv[1], 16)
    except ValueError:
        print(f"Error: Invalid hex address '{sys.argv[1]}'", file=sys.stderr)
        sys.exit(1)

    # Read and concatenate all ROM files
    rom_files = sys.argv[2:]
    binary_data = bytearray()

    for rom_file in rom_files:
        if not os.path.exists(rom_file):
            print(f"Error: File '{rom_file}' not found", file=sys.stderr)
            sys.exit(1)

        with open(rom_file, "rb") as f:
            file_data = f.read()
            binary_data.extend(file_data)
            print(f"# Loaded {len(file_data)} bytes from {rom_file}", file=sys.stderr)

    # Check for data duplication and truncate if necessary
    binary_data = check_and_truncate_duplication(binary_data)

    print(
        f"# Total: {len(binary_data)} bytes at address ${start_address:04X}",
        file=sys.stderr,
    )
    print(
        f"# Address range: ${start_address:04X}-${start_address + len(binary_data) - 1:04X}",
        file=sys.stderr,
    )

    # Generate Intel HEX
    hex_output = binary_to_intel_hex(binary_data, start_address)
    print(hex_output)


if __name__ == "__main__":
    main()
