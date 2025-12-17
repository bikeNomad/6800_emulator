#!/usr/bin/env python3
"""
Interpret PIA (MC6821) reads and writes from SPI log data.

The MC6821 PIA has two I/O ports (A and B), each with:
- A data register (for peripheral data)
- A data direction register (DDR) - 0=input, 1=output per bit
- A control register (CRA/CRB)

Register map for each PIA (base address + offset):
  +0: Port A Data/DDR (selected by CRA bit 2: 1=Data, 0=DDR)
  +1: Control Register A (CRA)
  +2: Port B Data/DDR (selected by CRB bit 2: 1=Data, 0=DDR)
  +3: Control Register B (CRB)

Control Register bits:
  Bit 7: IRQA1/IRQB1 flag
  Bit 6: IRQA2/IRQB2 flag
  Bit 5: CA2/CB2 control
  Bit 4: CA2/CB2 control
  Bit 3: CA1/CB1 control (interrupt enable)
  Bit 2: DDR access control (0=DDR, 1=Data register)
  Bit 1: CA1/CB1 interrupt flag
  Bit 0: CA1/CB1 active transition
"""

import sys
import os


class PIAPort:
    """Represents one port (A or B) of a PIA."""
    
    def __init__(self, name):
        self.name = name
        self.ddr = 0x00  # Data direction register (0=input, 1=output)
        self.data_out = 0x00  # Last value written to outputs
        self.data_in = 0x00  # Last value read from port
        self.control = 0x00  # Control register
        
    def is_ddr_selected(self):
        """Return True if DDR is selected (CRA/CRB bit 2 = 0)."""
        return (self.control & 0x04) == 0
    
    def format_bits(self, value):
        """Format a byte as binary string."""
        return f"{value:08b}"
    
    def format_masked_bits(self, value, mask):
        """Format a byte as binary string with 'x' for masked-out bits.
        
        Args:
            value: The byte value to format
            mask: Bit mask where 1 = show bit, 0 = show as 'x'
        """
        result = []
        for bit in range(7, -1, -1):  # MSB to LSB
            bit_mask = 1 << bit
            if mask & bit_mask:
                result.append('1' if (value & bit_mask) else '0')
            else:
                result.append('x')
        return ''.join(result)
    
    def get_io_description(self):
        """Get description of which bits are inputs/outputs."""
        desc = []
        for bit in range(8):
            mask = 1 << bit
            if self.ddr & mask:
                desc.append(f"O")
            else:
                desc.append(f"I")
        return ''.join(reversed(desc))  # MSB first


class PIA:
    """Represents a complete MC6821 PIA."""
    
    def __init__(self, base_addr):
        self.base_addr = base_addr
        self.port_a = PIAPort("A")
        self.port_b = PIAPort("B")
    
    def get_port_and_reg(self, offset):
        """
        Determine which port and register an offset refers to.
        Returns (port, reg_name) where:
          port is PIAPort object
          reg_name is "DDR", "DATA", or "CONTROL"
        """
        if offset == 0:
            if self.port_a.is_ddr_selected():
                return (self.port_a, "DDR")
            else:
                return (self.port_a, "DATA")
        elif offset == 1:
            return (self.port_a, "CONTROL")
        elif offset == 2:
            if self.port_b.is_ddr_selected():
                return (self.port_b, "DDR")
            else:
                return (self.port_b, "DATA")
        elif offset == 3:
            return (self.port_b, "CONTROL")
        else:
            return (None, None)
    
    def handle_write(self, offset, data):
        """Handle a write to this PIA and return annotation."""
        port, reg_name = self.get_port_and_reg(offset)
        
        if port is None:
            return f"PIA {self.base_addr:04X}: Invalid offset {offset}"
        
        annotations = []
        
        if reg_name == "DDR":
            port.ddr = data
            annotations.append(f"PIA {self.base_addr:04X} Port {port.name}: Set DDR = {data:02X} ({port.format_bits(data)}) [{port.get_io_description()}]")
        
        elif reg_name == "DATA":
            # Only output bits (DDR=1) are affected by writes
            output_bits = port.ddr
            port.data_out = data
            
            # Show masked value with 'x' for input bits
            masked_bits = port.format_masked_bits(data, output_bits)
            
            annotation = f"PIA {self.base_addr:04X} Port {port.name}: Write {data:02X} ({masked_bits})"
            
            if output_bits == 0xFF:
                annotations.append(annotation + " [all outputs]")
            elif output_bits == 0x00:
                annotations.append(annotation + " [all inputs - write has no effect]")
            else:
                output_part = data & output_bits
                annotations.append(annotation + f" [effective={output_part:02X}]")
        
        elif reg_name == "CONTROL":
            old_control = port.control
            port.control = data
            ddr_bit_changed = (old_control & 0x04) != (data & 0x04)
            
            annotation = f"PIA {self.base_addr:04X} Port {port.name}: Set CR{port.name} = {data:02X} ({port.format_bits(data)})"
            
            if ddr_bit_changed:
                if port.is_ddr_selected():
                    annotations.append(annotation + " [DDR now accessible]")
                else:
                    annotations.append(annotation + " [DATA now accessible]")
            else:
                annotations.append(annotation)
        
        return " | ".join(annotations)
    
    def handle_read(self, offset, data):
        """Handle a read from this PIA and return annotation."""
        port, reg_name = self.get_port_and_reg(offset)
        
        if port is None:
            return f"PIA {self.base_addr:04X}: Invalid offset {offset}"
        
        annotations = []
        
        if reg_name == "DDR":
            annotations.append(f"PIA {self.base_addr:04X} Port {port.name}: Read DDR = {data:02X} ({port.format_bits(data)})")
        
        elif reg_name == "DATA":
            port.data_in = data
            
            # Check for output readback discrepancies
            output_bits = port.ddr
            input_bits = ~output_bits & 0xFF
            
            annotation = f"PIA {self.base_addr:04X} Port {port.name}: Read {data:02X} ({port.format_bits(data)})"
            
            if output_bits == 0x00:
                annotations.append(annotation + " [all inputs]")
            elif output_bits == 0xFF:
                # All outputs - check if readback matches what was written
                if data != port.data_out:
                    annotations.append(annotation + f" [all outputs - MISMATCH! wrote {port.data_out:02X}, read {data:02X}]")
                else:
                    annotations.append(annotation + " [all outputs - matches written value]")
            else:
                # Mixed inputs and outputs
                output_readback = data & output_bits
                input_readback = data & input_bits
                expected_output = port.data_out & output_bits
                
                parts = []
                parts.append(f"outputs={output_readback:02X}")
                parts.append(f"inputs={input_readback:02X}")
                
                if output_readback != expected_output:
                    parts.append(f"OUTPUT MISMATCH! wrote {expected_output:02X}")
                
                annotations.append(annotation + f" [{', '.join(parts)}]")
        
        elif reg_name == "CONTROL":
            annotations.append(f"PIA {self.base_addr:04X} Port {port.name}: Read CR{port.name} = {data:02X} ({port.format_bits(data)})")
        
        return " | ".join(annotations)


class PIAInterpreter:
    """Main interpreter for PIA accesses."""
    
    def __init__(self):
        self.pias = {}  # Map base_addr -> PIA
    
    def get_pia(self, address):
        """Get or create PIA for the given address."""
        # PIAs are at multiples of 0x100
        base_addr = (address // 0x100) * 0x100
        
        if base_addr not in self.pias:
            self.pias[base_addr] = PIA(base_addr)
        
        return self.pias[base_addr], address - base_addr
    
    def is_pia_address(self, address):
        """Check if address is in PIA range (0x2000-0x4FFF)."""
        return 0x2000 <= address <= 0x4FFF
    
    def process_line(self, line):
        """Process a single line from the SPI log and return annotated version."""
        line = line.strip()
        if not line:
            return ""
        
        # Parse line: "address R/W data"
        parts = line.split()
        if len(parts) != 3:
            return line  # Return unchanged if not in expected format
        
        try:
            address = int(parts[0], 16)
            rw = parts[1]
            data = int(parts[2], 16)
        except ValueError:
            return line  # Return unchanged if parse fails
        
        # Check if this is a PIA access
        if not self.is_pia_address(address):
            return line  # Not a PIA, return unchanged
        
        # Get PIA and process
        pia, offset = self.get_pia(address)
        
        if rw == 'W':
            annotation = pia.handle_write(offset, data)
        elif rw == 'R':
            annotation = pia.handle_read(offset, data)
        else:
            return line  # Unknown R/W, return unchanged
        
        # Return original line with annotation
        return f"{line:20s} # {annotation}"
    
    def print_pia_summary(self):
        """Print summary of all PIAs."""
        if not self.pias:
            return
        
        print("\n" + "="*80)
        print("PIA STATE SUMMARY")
        print("="*80)
        
        for base_addr in sorted(self.pias.keys()):
            pia = self.pias[base_addr]
            print(f"\nPIA at {base_addr:04X}:")
            
            for port in [pia.port_a, pia.port_b]:
                print(f"  Port {port.name}:")
                print(f"    Control:      {port.control:02X} ({port.format_bits(port.control)})")
                print(f"    DDR:          {port.ddr:02X} ({port.format_bits(port.ddr)}) [{port.get_io_description()}]")
                print(f"    Last written: {port.data_out:02X} ({port.format_bits(port.data_out)})")
                print(f"    Last read:    {port.data_in:02X} ({port.format_bits(port.data_in)})")


def interpret_pia_log(input_file, output_file=None):
    """
    Read SPI log and interpret PIA accesses.
    
    Args:
        input_file: Path to input file (output from extract_spi_data.py)
        output_file: Path to output file (defaults to input_pia.txt)
    """
    # Generate output filename if not provided
    if output_file is None:
        base_name = os.path.splitext(input_file)[0]
        # Remove _spi suffix if present
        if base_name.endswith('_spi'):
            base_name = base_name[:-4]
        output_file = f"{base_name}_pia.txt"
    
    interpreter = PIAInterpreter()
    
    try:
        with open(input_file, 'r') as infile:
            lines = infile.readlines()
        
        print(f"Processing {len(lines)} lines from {input_file}")
        
        with open(output_file, 'w') as outfile:
            for line in lines:
                annotated = interpreter.process_line(line)
                outfile.write(annotated + '\n')
        
        print(f"Wrote annotated output to {output_file}")
        
        # Print summary to stdout
        interpreter.print_pia_summary()
        
        return output_file
        
    except FileNotFoundError:
        print(f"Error: Input file '{input_file}' not found", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        import traceback
        traceback.print_exc()
        sys.exit(1)


def main():
    """Main entry point for the script."""
    if len(sys.argv) < 2:
        input_file = 'from_reset_spi.txt'
        print(f"No input file specified, using default: {input_file}")
    else:
        input_file = sys.argv[1]
    
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    interpret_pia_log(input_file, output_file)


if __name__ == '__main__':
    main()
