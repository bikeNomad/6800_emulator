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
Test script to verify the hex loading fix works correctly.
This script loads the test hex file and checks if the data appears in the ROM shadow.
"""

import sys
import serial
import time

def test_hex_loading(port, hex_file):
    """Test hex loading and verify data appears in ROM shadow."""
    
    # Read test hex file
    try:
        with open(hex_file, 'r') as f:
            hex_data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{hex_file}' not found", file=sys.stderr)
        return False

    # Open serial port
    try:
        ser = serial.Serial(port, 115200, timeout=5)
        print(f"Connected to {port}")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}", file=sys.stderr)
        return False

    try:
        # Give device time to initialize
        time.sleep(0.5)
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Clear existing ROM
        print("Clearing existing ROM...")
        ser.write(b'clear\r\n')
        ser.flush()
        time.sleep(0.5)
        
        # Read response
        response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        print(response, end='')

        # Load hex file
        print("Loading hex file...")
        ser.write(b'load\r\n')
        ser.flush()
        time.sleep(0.2)
        
        # Read response
        response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        print(response, end='')

        # Send hex data
        hex_bytes = hex_data.encode('utf-8')
        ser.write(hex_bytes)
        ser.flush()
        time.sleep(0.1)

        # Send end command
        ser.write(b'end\r\n')
        ser.flush()
        time.sleep(0.5)

        # Read response
        response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        print(response, end='')

        if 'OK: EPROM loaded successfully' not in response:
            print("ERROR: Hex loading failed")
            return False

        # Check memory map
        print("Checking memory map...")
        ser.write(b'memory_map\r\n')
        ser.flush()
        time.sleep(0.5)
        
        response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        print(response, end='')

        # Read the reset vector area (should be 0xF000 from the hex file)
        print("Reading reset vector area...")
        ser.write(b'read fff8 8\r\n')
        ser.flush()
        time.sleep(0.5)
        
        response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        print(response, end='')

        # Check if we see the expected data (0xF000 should appear as 0x7000 due to A15 aliasing)
        if 'F0' in response or '70' in response:
            print("✓ Test PASSED: Hex data found in ROM shadow")
            return True
        else:
            print("✗ Test FAILED: Hex data not found in ROM shadow")
            return False

    except Exception as e:
        print(f"Error during test: {e}", file=sys.stderr)
        return False
    finally:
        ser.close()
        print("Serial port closed")

def main():
    if len(sys.argv) != 3:
        print("Usage: test_hex_fix.py <serial_port> <hex_file>", file=sys.stderr)
        print("Example: test_hex_fix.py /dev/tty.usbmodem14201 tests/test_program.hex", file=sys.stderr)
        sys.exit(1)

    port = sys.argv[1]
    hex_file = sys.argv[2]

    success = test_hex_loading(port, hex_file)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
