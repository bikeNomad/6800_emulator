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
Load Intel HEX file to MC6800 emulator via USB CDC.

Usage:
    load_hex.py <serial_port> <hex_file>

Example:
    load_hex.py /dev/tty.usbmodem14201 game.hex
    load_hex.py /dev/ttyACM0 pinball.hex
"""

import sys
import serial
import time

def send_hex_file(port, hex_file):
    """Send HEX file to emulator via serial port."""

    # Read hex file
    try:
        with open(hex_file, 'r') as f:
            hex_data = f.read()
    except FileNotFoundError:
        print(f"Error: File '{hex_file}' not found", file=sys.stderr)
        return False
    except Exception as e:
        print(f"Error reading file: {e}", file=sys.stderr)
        return False

    # Open serial port
    try:
        ser = serial.Serial(port, 115200, timeout=2)
        print(f"Connected to {port}")
    except serial.SerialException as e:
        print(f"Error opening serial port: {e}", file=sys.stderr)
        return False

    try:
        # Give device time to initialize
        time.sleep(0.5)

        # Flush any pending data
        ser.reset_input_buffer()
        ser.reset_output_buffer()

        # Send load command
        print("Sending 'load' command...")
        ser.write(b'load\r\n')
        ser.flush()

        # Wait for response
        time.sleep(0.2)
        response = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
        print(response, end='')

        # Send hex data
        print(f"Sending {len(hex_data)} bytes of HEX data...")
        hex_bytes = hex_data.encode('utf-8')

        # Send in chunks to avoid overwhelming the buffer
        chunk_size = 512
        for i in range(0, len(hex_bytes), chunk_size):
            chunk = hex_bytes[i:i+chunk_size]
            ser.write(chunk)
            ser.flush()
            time.sleep(0.01)  # Small delay between chunks

            # Show progress
            progress = min(i + chunk_size, len(hex_bytes))
            print(f"\rProgress: {progress}/{len(hex_bytes)} bytes", end='', flush=True)

        print("\nHEX data sent, waiting for response...")

        # Wait for processing to complete (look for "OK" or "ERROR")
        timeout = time.time() + 10  # 10 second timeout
        response_buffer = ""

        while time.time() < timeout:
            if ser.in_waiting:
                data = ser.read(ser.in_waiting).decode('utf-8', errors='ignore')
                response_buffer += data
                print(data, end='', flush=True)

                # Check if we got a completion message
                if 'OK: EPROM loaded successfully' in response_buffer:
                    print("\n✓ Load successful!")
                    return True
                elif 'ERROR' in response_buffer:
                    print("\n✗ Load failed")
                    return False

            time.sleep(0.1)

        print("\n⚠ Timeout waiting for response")
        return False

    except KeyboardInterrupt:
        print("\n\nInterrupted by user")
        return False
    except Exception as e:
        print(f"\nError during transfer: {e}", file=sys.stderr)
        return False
    finally:
        ser.close()
        print("Serial port closed")

def main():
    if len(sys.argv) != 3:
        print("Usage: load_hex.py <serial_port> <hex_file>", file=sys.stderr)
        print("\nExamples:", file=sys.stderr)
        print("  load_hex.py /dev/tty.usbmodem14201 game.hex", file=sys.stderr)
        print("  load_hex.py /dev/ttyACM0 pinball.hex", file=sys.stderr)
        sys.exit(1)

    port = sys.argv[1]
    hex_file = sys.argv[2]

    success = send_hex_file(port, hex_file)
    sys.exit(0 if success else 1)

if __name__ == '__main__':
    main()
