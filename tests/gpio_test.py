"""
MC6800 GPIO Hardware Test
Tests all GPIO lines connected to MC6800 pins, pulsing each one HIGH for 10µs
in order of MC6800 pin number, starting with pin 4 (/IRQ).
"""

import machine
import time
import sys
import gc

# Pin mappings for NED_SYS7 board (full GPIO support)
# Ordered by MC6800 pin number (Keys represent MC6800 pin numbers)
PIN_MAPPING = {
    4: {"gpio": 27, "name": "/IRQ", "dir": machine.Pin.OUT},     # Pin 4: /IRQ (drive all pins for testing)
    5: {"gpio": 24, "name": "VMA", "dir": machine.Pin.OUT},      # Pin 5: VMA (output)
    6: {"gpio": 28, "name": "/NMI", "dir": machine.Pin.OUT},     # Pin 6: /NMI (drive for testing)
    9: {"gpio": 8, "name": "A0", "dir": machine.Pin.OUT},        # Pin 7: A0 (output)
    10: {"gpio": 9, "name": "A1", "dir": machine.Pin.OUT},        # Pin 8: A1 (output)
    11: {"gpio": 10, "name": "A2", "dir": machine.Pin.OUT},       # Pin 9: A2 (output)
    12: {"gpio": 11, "name": "A3", "dir": machine.Pin.OUT},      # Pin 10: A3 (output)
    13: {"gpio": 12, "name": "A4", "dir": machine.Pin.OUT},      # Pin 11: A4 (output)
    14: {"gpio": 13, "name": "A5", "dir": machine.Pin.OUT},      # Pin 12: A5 (output)
    15: {"gpio": 14, "name": "A6", "dir": machine.Pin.OUT},      # Pin 13: A6 (output)
    16: {"gpio": 15, "name": "A7", "dir": machine.Pin.OUT},      # Pin 14: A7 (output)
    17: {"gpio": 16, "name": "A8", "dir": machine.Pin.OUT},      # Pin 15: A8 (output)
    18: {"gpio": 17, "name": "A9", "dir": machine.Pin.OUT},      # Pin 16: A9 (output)
    19: {"gpio": 18, "name": "A10", "dir": machine.Pin.OUT},     # Pin 17: A10 (output)
    20: {"gpio": 19, "name": "A11", "dir": machine.Pin.OUT},     # Pin 18: A11 (output)
    22: {"gpio": 20, "name": "A12", "dir": machine.Pin.OUT},     # Pin 23: A12 (output)
    23: {"gpio": 21, "name": "A13", "dir": machine.Pin.OUT},     # Pin 24: A13 (output)
    24: {"gpio": 22, "name": "A14", "dir": machine.Pin.OUT},     # Pin 25: A14 (output)
    25: {"gpio": 23, "name": "A15", "dir": machine.Pin.OUT},     # Pin 25: A14 (output)
    26: {"gpio": 7, "name": "D7", "dir": machine.Pin.OUT},       # Pin 26: D7 (bidirectional, test as output)
    27: {"gpio": 6, "name": "D6", "dir": machine.Pin.OUT},       # Pin 27: D6 (bidirectional, test as output)
    28: {"gpio": 5, "name": "D5", "dir": machine.Pin.OUT},       # Pin 28: D5 (bidirectional, test as output)
    29: {"gpio": 4, "name": "D4", "dir": machine.Pin.OUT},       # Pin 29: D4 (bidirectional, test as output)
    30: {"gpio": 3, "name": "D3", "dir": machine.Pin.OUT},       # Pin 30: D3 (bidirectional, test as output)
    31: {"gpio": 2, "name": "D2", "dir": machine.Pin.OUT},       # Pin 31: D2 (bidirectional, test as output)
    32: {"gpio": 1, "name": "D1", "dir": machine.Pin.OUT},       # Pin 32: D1 (bidirectional, test as output)
    33: {"gpio": 0, "name": "D0", "dir": machine.Pin.OUT},       # Pin 33: D0 (bidirectional, test as output)
    34: {"gpio": 26, "name": "R/W", "dir": machine.Pin.OUT},     # Pin 34: R/W (output)
    37: {"gpio": 25, "name": "E", "dir": machine.Pin.OUT},       # Pin 37: E (Φ2) (output)
    40: {"gpio": 29, "name": "/RESET", "dir": machine.Pin.OUT},   # Pin 40: /RESET (drive for testing)
}

def test_gpio(pin_num, pin_config):
    """Test a single GPIO pin by pulsing it HIGH for 10µs"""
    gpio_num = pin_config["gpio"]
    name = pin_config["name"]

    try:
        # Create pin object
        pin = machine.Pin(gpio_num, pin_config["dir"])

        # Pulse HIGH then back to LOW
        pin.value(0)  # Ensure starting LOW
        time.sleep_us(1)  # Brief delay
        pin.value(1)  # Pulse HIGH
        time.sleep_us(10)  # 10µs pulse
        pin.value(0)  # Return to LOW

        print("Testing {} (GPIO{}, MC6800 pin {}): PULSED ✓".format(name, gpio_num, pin_num))

    except Exception as e:
        print("ERROR testing {} (GPIO{}): {}".format(name, gpio_num, e))

def test_all_pins():
    # Sort by MC6800 pin number (starting from pin 4 as specified)
    sorted_pins = sorted(PIN_MAPPING.items())

    for mc6800_pin, config in sorted_pins:
        test_gpio(mc6800_pin, config)
        time.sleep_us(88)  # Delay between tests

def run_test():
    print("MC6800 GPIO Hardware Test")
    print("Testing all connected pins in MC6800 pin order")
    print("=" * 50)

    try:
        while True:
            gc.collect()
            test_all_pins()
            time.sleep_ms(100)  # Delay between tests
    except KeyboardInterrupt:
        print("\nTest interrupted by user.")
    except Exception as e:
        print("ERROR: {}".format(e))
        sys.exit(1)

    print("\nTest complete! All GPIO lines exercised.")

if __name__ == "__main__":
    run_test()
