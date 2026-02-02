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
Example ROM testing script using the bus_test module
"""

import sys
import os

# Add parent directory to path to import bus_test
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

import bus_test


def test_rom_basic():
    """Basic ROM test - read some bytes and verify they're not all zeros"""
    print("Testing ROM basic functionality...")

    try:
        with bus_test.BusTester() as bus:
            # Test reading from a few addresses
            print("Reading test data from bus:")
            for addr in [0x0000, 0x0001, 0x0002, 0x0003]:
                try:
                    data = bus.read_byte(addr)
                    print("02X")
                except Exception as e:
                    print(f"Error reading address 0x{addr:04X}: {e}")
                    return False

            print("Basic ROM test passed!")
            return True

    except Exception as e:
        print(f"ROM test failed: {e}")
        return False


def test_pia_ports():
    """Test PIA port access (assuming PIA is mapped to addresses 0x0000-0x0003)"""
    print("Testing PIA port access...")

    try:
        with bus_test.BusTester() as bus:
            # Test writing to PIA ports
            test_values = [0x55, 0xAA, 0xFF, 0x00]

            for i, value in enumerate(test_values):
                print("02X")
                bus.write_byte(i, value)

                # Read back to verify
                read_back = bus.read_byte(i)
                if read_back != value:
                    print(f"  ERROR: Wrote 0x{value:02X}, read back 0x{read_back:02X}")
                    return False
                else:
                    print(f"  OK: Verified 0x{value:02X}")

            print("PIA port test passed!")
            return True

    except Exception as e:
        print(f"PIA test failed: {e}")
        return False


def test_memory_dump():
    """Test memory dumping functionality"""
    print("Testing memory dump...")

    try:
        with bus_test.BusTester() as bus:
            # Dump first 64 bytes
            dump = bus.dump_memory(0x0000, 64, width=16)
            print("Memory dump (first 64 bytes):")
            print(dump)
            print("Memory dump test passed!")
            return True

    except Exception as e:
        print(f"Memory dump test failed: {e}")
        return False


def test_block_operations():
    """Test block read/write operations"""
    print("Testing block operations...")

    try:
        with bus_test.BusTester() as bus:
            # Test data
            test_data = list(range(16))  # [0, 1, 2, ..., 15]

            # Write block
            print(f"Writing block of {len(test_data)} bytes...")
            bus.write_block(0x0010, test_data)

            # Read block back
            print(f"Reading block of {len(test_data)} bytes...")
            read_data = bus.read_block(0x0010, len(test_data))

            # Verify
            if read_data == test_data:
                print("Block operation test passed!")
                return True
            else:
                print(f"ERROR: Data mismatch!")
                print(f"  Wrote: {test_data}")
                print(f"  Read:  {read_data}")
                return False

    except Exception as e:
        print(f"Block operation test failed: {e}")
        return False


def main():
    """Run all tests"""
    print("MC6800 Bus Test Suite")
    print("=" * 40)

    tests = [
        ("Basic ROM Test", test_rom_basic),
        ("PIA Port Test", test_pia_ports),
        ("Memory Dump Test", test_memory_dump),
        ("Block Operations Test", test_block_operations),
    ]

    results = []
    for test_name, test_func in tests:
        print(f"\n{test_name}:")
        print("-" * len(test_name))
        success = test_func()
        results.append((test_name, success))

    # Summary
    print("\n" + "=" * 40)
    print("Test Summary:")
    passed = 0
    for test_name, success in results:
        status = "PASS" if success else "FAIL"
        print(f"  {test_name}: {status}")
        if success:
            passed += 1

    print(f"\nPassed: {passed}/{len(results)} tests")

    if passed == len(results):
        print("All tests passed! ✓")
        return 0
    else:
        print("Some tests failed! ✗")
        return 1


if __name__ == "__main__":
    sys.exit(main())
