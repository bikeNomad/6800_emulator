#!/usr/bin/env python3
"""
Download memory ranges from MC6800 emulator via USB CDC to Intel HEX file.

This tool connects to the MC6800 emulator and downloads one or more memory
ranges, saving them to an Intel HEX file. Multiple ranges can be specified
to create a single HEX file with non-contiguous memory regions.

Usage:
    download_memory.py <serial_port> <output_file> <addr1:len1> [addr2:len2] [...]

Examples:
    # Download ROM region
    download_memory.py /dev/tty.usbmodem14201 rom_backup.hex 5000:3000

    # Download multiple regions
    download_memory.py /dev/ttyACM0 full_backup.hex 0000:1400 5000:3000

    # Download entire address space
    download_memory.py /dev/tty.usbmodem14201 full_dump.hex 0000:10000

    # Download CMOS region
    download_memory.py /dev/tty.usbmodem14201 cmos_backup.hex 0100:100
"""

import sys
import serial
import time
import os
from typing import List, Tuple, Optional

class MemoryDownloader:
    """Handles downloading memory from MC6800 emulator."""

    def __init__(self, port: str, timeout: float = 5.0):
        """
        Initialize downloader.

        Args:
            port: Serial port path (e.g., /dev/tty.usbmodem14201)
            timeout: Timeout in seconds for serial operations
        """
        self.port = port
        self.timeout = timeout
        self.ser: Optional[serial.Serial] = None

    def connect(self) -> bool:
        """
        Connect to emulator via serial port.

        Returns:
            True if connection successful, False otherwise
        """
        try:
            self.ser = serial.Serial(self.port, 115200, timeout=self.timeout)
            print(f"✓ Connected to {self.port}")

            # Give device time to initialize
            time.sleep(0.5)

            # Flush any pending data
            self.ser.reset_input_buffer()
            self.ser.reset_output_buffer()

            return True
        except serial.SerialException as e:
            print(f"✗ Error opening serial port: {e}", file=sys.stderr)
            return False
        except Exception as e:
            print(f"✗ Unexpected error: {e}", file=sys.stderr)
            return False

    def close(self):
        """Close serial port connection."""
        if self.ser and self.ser.is_open:
            self.ser.close()
            print("✓ Serial port closed")

    def send_command(self, command: str) -> str:
        """
        Send command to emulator and read response.

        Args:
            command: Command string to send

        Returns:
            Response string from emulator
        """
        if not self.ser or not self.ser.is_open:
            raise RuntimeError("Serial port not open")

        # Send command
        self.ser.write(f"{command}\r\n".encode('utf-8'))
        self.ser.flush()

        # Read response with timeout
        response_lines = []
        timeout_time = time.time() + self.timeout

        while time.time() < timeout_time:
            if self.ser.in_waiting:
                line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                if line:
                    response_lines.append(line)

                    # Check for completion markers
                    if line.startswith('OK:') or line.startswith('ERROR:'):
                        break
                    # Check for EOF record in Intel HEX
                    if line.startswith(':00000001FF'):
                        # Read the OK line that follows
                        time.sleep(0.1)
                        if self.ser.in_waiting:
                            final_line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                            if final_line:
                                response_lines.append(final_line)
                        break
            else:
                time.sleep(0.01)

        return '\n'.join(response_lines)

    def download_range(self, address: int, length: int) -> List[str]:
        """
        Download a memory range as Intel HEX records.

        Args:
            address: Start address (hex)
            length: Number of bytes to download

        Returns:
            List of Intel HEX record lines (without EOF)

        Raises:
            RuntimeError: If download fails
        """
        print(f"  Downloading ${address:04X}:${length:04X} ({length} bytes)...", end='', flush=True)

        # Send download command
        command = f"download {address:04X} {length:04X}"
        response = self.send_command(command)

        # Parse response
        hex_records = []
        found_data = False
        error_occurred = False

        for line in response.split('\n'):
            # Skip informational lines
            if line.startswith('Downloading'):
                continue

            # Check for errors
            if line.startswith('ERROR:'):
                error_occurred = True
                print(f" ✗\n  {line}", file=sys.stderr)
                break

            # Check for success
            if line.startswith('OK:'):
                if found_data:
                    print(" ✓")
                else:
                    print(" ✗ (no data received)", file=sys.stderr)
                    error_occurred = True
                break

            # Collect Intel HEX records (but not EOF)
            if line.startswith(':'):
                if not line.startswith(':00000001FF'):  # Skip EOF record
                    hex_records.append(line)
                    found_data = True

        if error_occurred:
            raise RuntimeError("Download failed")

        if not found_data:
            raise RuntimeError("No data received from emulator")

        return hex_records

    def download_ranges(self, ranges: List[Tuple[int, int]]) -> List[str]:
        """
        Download multiple memory ranges.

        Args:
            ranges: List of (address, length) tuples

        Returns:
            List of all Intel HEX record lines (without EOF)

        Raises:
            RuntimeError: If any download fails
        """
        all_records = []

        print(f"Downloading {len(ranges)} memory range(s):")

        for i, (address, length) in enumerate(ranges, 1):
            try:
                records = self.download_range(address, length)
                all_records.extend(records)
            except RuntimeError as e:
                raise RuntimeError(f"Failed to download range {i}: {e}")

        return all_records


def parse_range(range_spec: str) -> Tuple[int, int]:
    """
    Parse a range specification like "5000:3000".

    Args:
        range_spec: String in format "addr:len" (hex values)

    Returns:
        Tuple of (address, length) as integers

    Raises:
        ValueError: If format is invalid
    """
    parts = range_spec.split(':')
    if len(parts) != 2:
        raise ValueError(f"Invalid range format '{range_spec}'. Expected 'addr:len'")

    try:
        address = int(parts[0], 16)
        length = int(parts[1], 16)
    except ValueError:
        raise ValueError(f"Invalid hex values in range '{range_spec}'")

    # Validate range
    if address > 0xFFFF:
        raise ValueError(f"Address ${address:X} exceeds 64K address space")
    if length < 1 or length > 0x10000:
        raise ValueError(f"Length ${length:X} must be 1-65536 bytes")
    if address + length > 0x10000:
        raise ValueError(f"Range ${address:04X}:${length:04X} exceeds address space")

    return (address, length)


def save_hex_file(filename: str, hex_records: List[str]) -> bool:
    """
    Save Intel HEX records to file.

    Args:
        filename: Output filename
        hex_records: List of Intel HEX record lines

    Returns:
        True if successful, False otherwise
    """
    try:
        with open(filename, 'w') as f:
            for record in hex_records:
                f.write(record + '\n')
            # Add EOF record
            f.write(':00000001FF\n')

        print(f"✓ Saved {len(hex_records)} records to {filename}")

        # Show file size
        file_size = os.path.getsize(filename)
        print(f"  File size: {file_size} bytes")

        return True
    except IOError as e:
        print(f"✗ Error writing file: {e}", file=sys.stderr)
        return False


def calculate_data_bytes(hex_records: List[str]) -> int:
    """
    Calculate total data bytes in Intel HEX records.

    Args:
        hex_records: List of Intel HEX record lines

    Returns:
        Total number of data bytes
    """
    total = 0
    for record in hex_records:
        if record.startswith(':') and len(record) >= 9:
            byte_count = int(record[1:3], 16)
            total += byte_count
    return total


def print_summary(ranges: List[Tuple[int, int]], hex_records: List[str]):
    """Print download summary."""
    print("\nDownload Summary:")
    print(f"  Ranges downloaded: {len(ranges)}")

    total_requested = sum(length for _, length in ranges)
    total_received = calculate_data_bytes(hex_records)

    print(f"  Bytes requested:   {total_requested} (${total_requested:X})")
    print(f"  Bytes received:    {total_received} (${total_received:X})")
    print(f"  Intel HEX records: {len(hex_records)}")

    if total_requested != total_received:
        print("  ⚠ Warning: Byte count mismatch!", file=sys.stderr)

    print("\nMemory Ranges:")
    for address, length in ranges:
        end_addr = address + length - 1
        print(f"  ${address:04X}-${end_addr:04X}  ({length} bytes)")


def main():
    """Main entry point."""
    # Parse command line arguments
    if len(sys.argv) < 4:
        print(__doc__, file=sys.stderr)
        sys.exit(1)

    port = sys.argv[1]
    output_file = sys.argv[2]
    range_specs = sys.argv[3:]

    # Parse ranges
    try:
        ranges = [parse_range(spec) for spec in range_specs]
    except ValueError as e:
        print(f"✗ Error parsing ranges: {e}", file=sys.stderr)
        sys.exit(1)

    # Validate ranges don't overlap
    sorted_ranges = sorted(ranges, key=lambda x: x[0])
    for i in range(len(sorted_ranges) - 1):
        addr1, len1 = sorted_ranges[i]
        addr2, len2 = sorted_ranges[i + 1]
        if addr1 + len1 > addr2:
            print(f"✗ Warning: Ranges ${addr1:04X}:${len1:04X} and ${addr2:04X}:${len2:04X} overlap",
                  file=sys.stderr)

    # Connect to emulator
    downloader = MemoryDownloader(port)

    try:
        if not downloader.connect():
            sys.exit(1)

        # Download ranges
        try:
            hex_records = downloader.download_ranges(ranges)
        except RuntimeError as e:
            print(f"✗ Download failed: {e}", file=sys.stderr)
            sys.exit(1)

        # Save to file
        if not save_hex_file(output_file, hex_records):
            sys.exit(1)

        # Print summary
        print_summary(ranges, hex_records)

        print("\n✓ Download complete!")

    except KeyboardInterrupt:
        print("\n\n✗ Interrupted by user", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"\n✗ Unexpected error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)
    finally:
        downloader.close()


if __name__ == '__main__':
    main()
