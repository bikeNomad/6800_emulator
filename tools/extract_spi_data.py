#!/usr/bin/env python3
"""
Extract SPI data from CSV files and display PC/CCR and bus access traces.

This script reads CSV files containing SPI trace data and extracts
the mosi field values, interpreting them as either:
- PC/CCR pairs (instruction execution)
- Bus access traces (unmapped address reads/writes)
"""

import csv
import sys
import os


def extract_spi_data(input_file, output_file=None):
    """
    Extract SPI data from CSV and format PC/CCR or bus access traces.
    
    Args:
        input_file: Path to input CSV file
        output_file: Path to output file (defaults to input_spi.txt)
    """
    # Generate output filename if not provided
    if output_file is None:
        base_name = os.path.splitext(input_file)[0]
        output_file = f"{base_name}_spi.txt"
    
    # Read CSV and extract mosi values from "result" rows
    mosi_values = []
    
    try:
        with open(input_file, 'r') as csvfile:
            reader = csv.DictReader(csvfile)
            
            for row in reader:
                if row['type'] == 'result':
                    mosi = row['mosi'].strip()
                    # Remove '0x' prefix if present
                    if mosi.startswith('0x') or mosi.startswith('0X'):
                        mosi = mosi[2:]
                    mosi_values.append(mosi.upper())
        
        print(f"Extracted {len(mosi_values)} SPI values from {input_file}")
        
        # Process consecutive pairs and write to output
        with open(output_file, 'w') as outfile:
            i = 0
            while i < len(mosi_values) - 1:
                word0 = mosi_values[i]
                word1 = mosi_values[i + 1]
                
                # Parse as 16-bit hex values
                try:
                    addr_or_pc = int(word0, 16)
                    data_or_ccr = int(word1, 16)
                    
                    # Check if this is an unmapped bus access (address < 0x5000)
                    if addr_or_pc < 0x5000:
                        # Bus access packet format:
                        # Word 0: address
                        # Word 1: high byte = R/W flag (01=R, 00=W), low byte = data
                        address = addr_or_pc
                        rw_flag = (data_or_ccr >> 8) & 0xFF
                        data = data_or_ccr & 0xFF
                        
                        # Format as "address R/W data"
                        rw_str = 'R' if rw_flag == 0x01 else 'W'
                        outfile.write(f"{address:04X} {rw_str} {data:02X}\n")
                    else:
                        # PC/CCR packet format:
                        # Word 0: PC
                        # Word 1: CCR (low byte)
                        pc = addr_or_pc
                        ccr = data_or_ccr & 0xFF
                        
                        # Format as "pc ccr" (remove leading zeros from PC)
                        pc_str = f"{pc:X}"
                        outfile.write(f"{pc_str} {ccr:X}\n")
                    
                except ValueError:
                    # Skip malformed values
                    print(f"Warning: Skipping malformed values at index {i}: {word0}, {word1}")
                
                i += 2
        
        pairs_written = (len(mosi_values) // 2)
        print(f"Wrote {pairs_written} trace entries to {output_file}")
        
        # Warn if odd number of values
        if len(mosi_values) % 2 != 0:
            print(f"Warning: Odd number of values ({len(mosi_values)}), last value not paired")
        
        return output_file
        
    except FileNotFoundError:
        print(f"Error: Input file '{input_file}' not found", file=sys.stderr)
        sys.exit(1)
    except KeyError as e:
        print(f"Error: Expected column {e} not found in CSV", file=sys.stderr)
        sys.exit(1)
    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


def main():
    """Main entry point for the script."""
    if len(sys.argv) < 2:
        input_file = 'from_reset.csv'
        print(f"No input file specified, using default: {input_file}")
    else:
        input_file = sys.argv[1]
    
    output_file = sys.argv[2] if len(sys.argv) > 2 else None
    
    extract_spi_data(input_file, output_file)


if __name__ == '__main__':
    main()
