#!/usr/bin/env python3
"""
MC6800 Bus Test Module
Provides Python functions to read and write via the hardware bus for testing ROMs, PIAs, etc.
"""

import serial
import serial.tools.list_ports
import time
import struct
from typing import Optional, List, Union


class BusError(Exception):
    """Exception raised for bus communication errors"""
    pass


class BusTester:
    """
    MC6800 Bus Tester
    Provides methods to read/write data via the hardware bus interface
    """

    def __init__(self, port: Optional[str] = None, baudrate: int = 115200, timeout: float = 1.0):
        """
        Initialize the bus tester.

        Args:
            port: Serial port name (auto-detect if None)
            baudrate: Serial baud rate
            timeout: Serial timeout in seconds
        """
        self.port = port or self._auto_detect_port()
        self.baudrate = baudrate
        self.timeout = timeout
        self.serial: Optional[serial.Serial] = None

    def _auto_detect_port(self) -> str:
        """Auto-detect the MC6800 emulator serial port"""
        ports = serial.tools.list_ports.comports()

        # Look for RP2350/RP2040 devices (Raspberry Pi Pico)
        for port in ports:
            if "RP2350" in port.description or "RP2040" in port.description or "Pico" in port.description:
                print(f"Auto-detected MC6800 emulator on port: {port.device}")
                return port.device

        # Fallback: look for any USB serial device
        for port in ports:
            if port.vid and port.pid:  # USB device
                print(f"Using USB serial device: {port.device}")
                return port.device

        raise BusError("Could not auto-detect MC6800 emulator serial port")

    def open(self) -> None:
        """Open the serial connection"""
        if self.serial is None:
            try:
                self.serial = serial.Serial(
                    port=self.port,
                    baudrate=self.baudrate,
                    timeout=self.timeout
                )
                time.sleep(2)  # Allow time for connection to establish
                print(f"Connected to MC6800 emulator on {self.port}")
            except serial.SerialException as e:
                raise BusError(f"Failed to open serial port: {e}")

    def close(self) -> None:
        """Close the serial connection"""
        if self.serial:
            self.serial.close()
            self.serial = None

    def __enter__(self):
        self.open()
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def _send_command(self, cmd: str, data: bytes = b"") -> str:
        """Send a command and return the response"""
        if not self.serial:
            raise BusError("Serial connection not open")

        # Send command
        full_cmd = f"{cmd}\n".encode() + data
        self.serial.write(full_cmd)
        self.serial.flush()

        # Read response
        response = b""
        start_time = time.time()

        while time.time() - start_time < self.timeout:
            if self.serial.in_waiting:
                chunk = self.serial.read(self.serial.in_waiting)
                response += chunk

                # Check if we have a complete response (ends with newline)
                if b"\n" in response:
                    break

            time.sleep(0.01)

        if not response:
            raise BusError(f"No response to command: {cmd}")

        # Parse response
        response_str = response.decode().strip()

        if response_str.startswith("ERROR:"):
            raise BusError(f"Emulator error: {response_str[6:]}")

        return response_str

    def read_byte(self, address: int) -> int:
        """
        Read a single byte from the specified address.

        Args:
            address: Address to read from (0-65535 for 16-bit, 0-127 for 7-bit)

        Returns:
            The byte value read (0-255)
        """
        if not (0 <= address <= 0xFFFF):
            raise ValueError("Address must be 0-65535")

        response = self._send_command(f"BUS_READ {address:04X}")
        try:
            return int(response, 16)
        except ValueError:
            raise BusError(f"Invalid response format: {response}")

    def write_byte(self, address: int, data: int) -> None:
        """
        Write a single byte to the specified address.

        Args:
            address: Address to write to (0-65535 for 16-bit, 0-127 for 7-bit)
            data: Data to write (0-255)
        """
        if not (0 <= address <= 0xFFFF):
            raise ValueError("Address must be 0-65535")
        if not (0 <= data <= 255):
            raise ValueError("Data must be 0-255")

        self._send_command(f"BUS_WRITE {address:04X} {data:02X}")

    def read_block(self, address: int, length: int) -> List[int]:
        """
        Read a block of bytes from the specified address range.

        Args:
            address: Starting address
            length: Number of bytes to read (max 1024)

        Returns:
            List of byte values
        """
        if not (0 <= address <= 0xFFFF):
            raise ValueError("Address must be 0-65535")
        if not (1 <= length <= 1024):
            raise ValueError("Length must be 1-1024")

        response = self._send_command(f"BUS_READ_BLOCK {address:04X} {length:04X}")

        try:
            # Response should be hex bytes separated by spaces
            hex_bytes = response.split()
            return [int(h, 16) for h in hex_bytes]
        except ValueError:
            raise BusError(f"Invalid block response format: {response}")

    def write_block(self, address: int, data: Union[List[int], bytes]) -> None:
        """
        Write a block of bytes to the specified address range.

        Args:
            address: Starting address
            data: Data to write (list of bytes or bytes object)
        """
        if not (0 <= address <= 0xFFFF):
            raise ValueError("Address must be 0-65535")
        if isinstance(data, bytes):
            data = list(data)
        if not (1 <= len(data) <= 1024):
            raise ValueError("Data length must be 1-1024")
        if not all(isinstance(b, int) and 0 <= b <= 255 for b in data):
            raise ValueError("All data values must be 0-255")

        # Convert data to hex string
        hex_data = " ".join("02X")
        self._send_command(f"BUS_WRITE_BLOCK {address:04X} {hex_data}")

    def get_bus_info(self) -> dict:
        """
        Get information about the bus configuration.

        Returns:
            Dictionary with bus information
        """
        response = self._send_command("BUS_INFO")
        info = {}

        for line in response.split('\n'):
            if ':' in line:
                key, value = line.split(':', 1)
                info[key.strip()] = value.strip()

        return info

    def test_rom(self, address: int, expected_data: Union[List[int], bytes]) -> bool:
        """
        Test a ROM by reading data and comparing with expected values.

        Args:
            address: Starting address of ROM
            expected_data: Expected ROM data

        Returns:
            True if ROM matches expected data
        """
        if isinstance(expected_data, bytes):
            expected_data = list(expected_data)

        actual_data = self.read_block(address, len(expected_data))

        return actual_data == expected_data

    def dump_memory(self, address: int, length: int, width: int = 16) -> str:
        """
        Create a hex dump of memory contents.

        Args:
            address: Starting address
            length: Number of bytes to dump
            width: Bytes per line

        Returns:
            Formatted hex dump string
        """
        data = self.read_block(address, length)
        lines = []

        for i in range(0, len(data), width):
            chunk = data[i:i + width]
            hex_part = " ".join("02X")
            ascii_part = "".join(chr(b) if 32 <= b <= 126 else "." for b in chunk)

            lines.append("04X")

        return "\n".join(lines)


# Convenience functions for quick access
_default_tester: Optional[BusTester] = None

def init(port: Optional[str] = None) -> None:
    """Initialize the default bus tester"""
    global _default_tester
    _default_tester = BusTester(port)
    _default_tester.open()

def cleanup() -> None:
    """Clean up the default bus tester"""
    global _default_tester
    if _default_tester:
        _default_tester.close()
        _default_tester = None

def read(address: int) -> int:
    """Read a byte using the default tester"""
    if not _default_tester:
        raise BusError("Bus tester not initialized. Call init() first.")
    return _default_tester.read_byte(address)

def write(address: int, data: int) -> None:
    """Write a byte using the default tester"""
    if not _default_tester:
        raise BusError("Bus tester not initialized. Call init() first.")
    _default_tester.write_byte(address, data)

def read_block(address: int, length: int) -> List[int]:
    """Read a block using the default tester"""
    if not _default_tester:
        raise BusError("Bus tester not initialized. Call init() first.")
    return _default_tester.read_block(address, length)

def write_block(address: int, data: Union[List[int], bytes]) -> None:
    """Write a block using the default tester"""
    if not _default_tester:
        raise BusError("Bus tester not initialized. Call init() first.")
    _default_tester.write_block(address, data)


if __name__ == "__main__":
    # Example usage
    print("MC6800 Bus Test Module")
    print("Example usage:")
    print()
    print("import bus_test")
    print("bus_test.init()  # Connect to emulator")
    print("data = bus_test.read(0x0000)  # Read from address 0")
    print("bus_test.write(0x0000, 0xAA)  # Write 0xAA to address 0")
    print("bus_test.cleanup()  # Disconnect")
    print()
    print("Or use as context manager:")
    print("with bus_test.BusTester() as bus:")
    print("    data = bus.read_byte(0x0000)")
