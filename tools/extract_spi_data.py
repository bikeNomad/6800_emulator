#!/usr/bin/env python3
"""
Extract SPI data from CSV files and pair PC/CCR values.

This script reads CSV files containing SPI trace data and extracts
the mosi field values, pairing consecutive PC and CCR values on single lines.
"""

import csv
import sys
import os


def extract_spi_data(input_file, output_file=None):
    """
    Extract SPI data from CSV and pair PC/CCR values.
    
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
        
        # Pair consecutive values (PC, CCR) and write to output
        with open(output_file, 'w') as outfile:
            for i in range(0, len(mosi_values) - 1, 2):
                pc = mosi_values[i].lstrip('0') or '0'
                ccr = mosi_values[i + 1].lstrip('0') or '0'
                outfile.write(f"{pc} {ccr}\n")
        
        pairs_written = (len(mosi_values) // 2)
        print(f"Wrote {pairs_written} PC/CCR pairs to {output_file}")
        
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
