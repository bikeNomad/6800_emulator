"""
Standalone PIO-based MC6800 Bus Test Demo
Demonstrates the PIO-based read/write operations that match the behavior of 
src/bus_cycle.pio and src/bus.c for cycle-accurate bus testing.

This program provides a complete demonstration of the PIO-based bus interface
and can be run independently to test the hardware connection.
"""

import machine
import time
import gc
from pio_bus_test import PIOBusTester, set_board_type, BOARD_NED_SYS7


def print_banner():
    """Print program banner"""
    print("=" * 60)
    print("PIO-based MC6800 Bus Test Demo")
    print("=" * 60)
    print("This program demonstrates PIO-based bus operations")
    print("that match the timing of src/bus_cycle.pio and src/bus.c")
    print()


def demo_basic_operations():
    """Demonstrate basic read/write operations"""
    print("1. Basic Read/Write Operations")
    print("-" * 30)

    # Test a few addresses
    test_addresses = [0x0000, 0x0001, 0x0002, 0x0003]

    for addr in test_addresses:
        try:
            # Read current value
            data = tester.read_byte(addr)
            print("Read  0x{:04X}: 0x{:02X}".format(addr, data))

            # Write test pattern
            test_data = 0xAA
            tester.write_byte(addr, test_data)
            print("Write 0x{:04X}: 0x{:02X}".format(addr, test_data))

            # Read back
            read_back = tester.read_byte(addr)
            print("Read  0x{:04X}: 0x{:02X} {}".format(
                addr, read_back, "✓" if read_back == test_data else "✗"))

            # Restore original value
            tester.write_byte(addr, data)

        except Exception as e:
            print("Error at 0x{:04X}: {}".format(addr, e))

        time.sleep_ms(100)  # Small delay between operations


def demo_block_operations():
    """Demonstrate block read/write operations"""
    print("\n2. Block Read/Write Operations")
    print("-" * 30)

    # Test block operations
    start_addr = 0x0010
    block_size = 16

    try:
        # Create test data
        test_data = list(range(block_size))
        print("Writing block at 0x{:04X} ({} bytes): {}".format(
            start_addr, block_size, [hex(x) for x in test_data]))

        # Write block
        tester.write_block(start_addr, test_data)

        # Read back
        read_data = tester.read_block(start_addr, block_size)
        print("Read back: {}".format([hex(x) for x in read_data]))

        # Verify
        if read_data == test_data:
            print("Block operation: ✓ PASS")
        else:
            print("Block operation: ✗ FAIL")

        # Restore original data
        tester.write_block(start_addr, [0x00] * block_size)

    except Exception as e:
        print("Block operation error: {}".format(e))


def demo_memory_dump():
    """Demonstrate memory dump functionality"""
    print("\n3. Memory Dump")
    print("-" * 30)

    try:
        # Dump first 64 bytes of memory
        print("Dumping first 64 bytes of memory:")
        tester.dump_block(0x0000, 64)

    except Exception as e:
        print("Memory dump error: {}".format(e))


def demo_interrupt_monitoring():
    """Demonstrate interrupt line monitoring"""
    print("\n4. Interrupt Line Monitoring")
    print("-" * 30)

    try:
        # Monitor interrupt lines for a few seconds
        print("Monitoring interrupt lines for 3 seconds...")
        print("Press buttons/lines to see changes")

        for i in range(30):  # 3 seconds at 10Hz
            irq = tester.read_irq()
            nmi = tester.read_nmi()
            reset = tester.read_reset()

            print("IRQ: {}, NMI: {}, RESET: {}".format(irq, nmi, reset), end='\r')
            time.sleep_ms(100)

        print()  # New line after progress

    except Exception as e:
        print("Interrupt monitoring error: {}".format(e))


def demo_timing_test():
    """Demonstrate timing accuracy"""
    print("\n5. Timing Test")
    print("-" * 30)

    try:
        # Test timing by performing many operations
        start_time = time.ticks_us()
        num_operations = 100

        for i in range(num_operations):
            tester.read_byte(0x0000)

        end_time = time.ticks_us()
        elapsed = time.ticks_diff(end_time, start_time)

        avg_time = elapsed / num_operations
        print("Performed {} read operations".format(num_operations))
        print("Total time: {} µs".format(elapsed))
        print("Average time per operation: {:.2f} µs".format(avg_time))

    except Exception as e:
        print("Timing test error: {}".format(e))


def demo_bus_info():
    """Display bus configuration information"""
    print("\n6. Bus Configuration Information")
    print("-" * 30)

    try:
        info = tester.get_bus_info()
        for key, value in info.items():
            print("{}: {}".format(key, value))

    except Exception as e:
        print("Bus info error: {}".format(e))


def demo_checksum():
    """Demonstrate checksum calculation"""
    print("\n7. Checksum Calculation")
    print("-" * 30)

    try:
        # Calculate checksum of first 256 bytes
        addr = 0x0000
        length = 256
        checksum = tester.checksum(addr, length)
        print("Checksum of 0x{:04X}-0x{:04X}: {}".format(addr, addr + length - 1, checksum))

    except Exception as e:
        print("Checksum error: {}".format(e))


def interactive_menu():
    """Interactive menu for manual testing"""
    print("\n8. Interactive Testing")
    print("-" * 30)
    print("Interactive mode - type 'help' for commands")
    print("Type 'quit' to exit")

    while True:
        try:
            cmd = input("> ").strip().lower()

            if cmd == 'quit':
                break
            elif cmd == 'help':
                print("Commands:")
                print("  read <addr>     - Read byte from address")
                print("  write <addr> <data> - Write byte to address")
                print("  dump <addr> <len> - Dump memory block")
                print("  info            - Show bus information")
                print("  irq             - Read interrupt lines")
                print("  checksum <addr> <len> - Calculate checksum")
                print("  quit            - Exit")
            elif cmd.startswith('read '):
                parts = cmd.split()
                if len(parts) == 2:
                    addr = int(parts[1], 16)
                    data = tester.read_byte(addr)
                    print("Read  0x{:04X}: 0x{:02X}".format(addr, data))
                else:
                    print("Usage: read <addr>")
            elif cmd.startswith('write '):
                parts = cmd.split()
                if len(parts) == 3:
                    addr = int(parts[1], 16)
                    data = int(parts[2], 16)
                    tester.write_byte(addr, data)
                    print("Write 0x{:04X}: 0x{:02X}".format(addr, data))
                else:
                    print("Usage: write <addr> <data>")
            elif cmd.startswith('dump '):
                parts = cmd.split()
                if len(parts) == 3:
                    addr = int(parts[1], 16)
                    length = int(parts[2])
                    tester.dump_block(addr, length)
                else:
                    print("Usage: dump <addr> <len>")
            elif cmd == 'info':
                info = tester.get_bus_info()
                for key, value in info.items():
                    print("{}: {}".format(key, value))
            elif cmd == 'irq':
                irq = tester.read_irq()
                nmi = tester.read_nmi()
                reset = tester.read_reset()
                print("IRQ: {}, NMI: {}, RESET: {}".format(irq, nmi, reset))
            elif cmd.startswith('checksum '):
                parts = cmd.split()
                if len(parts) == 3:
                    addr = int(parts[1], 16)
                    length = int(parts[2])
                    checksum = tester.checksum(addr, length)
                    print("Checksum: {}".format(checksum))
                else:
                    print("Usage: checksum <addr> <len>")
            else:
                print("Unknown command. Type 'help' for available commands.")

        except KeyboardInterrupt:
            print("\nExiting...")
            break
        except Exception as e:
            print("Error: {}".format(e))


def main():
    """Main demo function"""
    global tester

    print_banner()

    # Initialize the PIO bus tester
    print("Initializing PIO bus tester...")
    tester = PIOBusTester()
    tester.init()

    try:
        # Run demonstrations
        demo_bus_info()
        demo_basic_operations()
        demo_block_operations()
        demo_memory_dump()
        demo_interrupt_monitoring()
        demo_timing_test()
        demo_checksum()

        # Interactive mode
        interactive_menu()

    except KeyboardInterrupt:
        print("\nDemo interrupted by user")
    except Exception as e:
        print("Demo error: {}".format(e))
    finally:
        # Clean up
        print("\nCleaning up...")
        tester.cleanup()
        print("Demo complete!")


if __name__ == "__main__":
    main()
</file_content>

<task_progress>
- [x] Analyze requirements and existing code
- [x] Create comprehensive plan
- [x] Create PIO-based bus test module (pio_bus_test.py)
- [x] Create standalone test program (pio_bus_demo.py)
- [ ] Test the implementation
- [ ] Verify compatibility with existing code
- [ ] Document the implementation
</task_progress>
</write_to_file>
