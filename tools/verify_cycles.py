#!/usr/bin/env python3
"""
Script to verify cycle counts from instruction counter output against reference data.

Usage: python verify_cycles.py [instruction_counter_file]
If no file is provided, reads from stdin.
"""

import sys
import re
from typing import Dict, List, Tuple, Optional

def load_reference_cycles(tsv_file: Optional[str] = None) -> Dict[str, int]:
    """Load reference cycle counts from TSV file."""
    reference = {}
    
    # Try to find the reference file in the tools directory
    if tsv_file is None:
        import os
        script_dir = os.path.dirname(os.path.abspath(__file__))
        tsv_file = os.path.join(script_dir, 'opcodes_and_cycles.tsv')
    
    try:
        with open(tsv_file, 'r') as f:
            for line in f:
                line = line.strip()
                if not line:
                    continue
                
                # Parse TSV format: "MNEMONIC (MODE)\t0xHH\tCYCLES"
                parts = line.split('\t')
                if len(parts) != 3:
                    continue
                
                mnemonic_mode = parts[0]
                opcode_hex = parts[1]
                cycles = int(parts[2])
                
                # Extract just the hex value without 0x prefix
                opcode_key = opcode_hex[2:].upper()
                reference[opcode_key] = cycles
                
    except FileNotFoundError:
        print(f"Error: Reference file '{tsv_file}' not found.")
        sys.exit(1)
    except ValueError as e:
        print(f"Error parsing reference file: {e}")
        sys.exit(1)
    
    return reference

def parse_instruction_counter_line(line: str) -> Optional[Tuple[str, int, str]]:
    """Parse a line from instruction counter output.
    
    Expected format: "$A6   5  LDAA (IND)             829511"
    Returns: (opcode_hex, cycles_from_output, mnemonic_mode) or None if invalid
    """
    # Remove extra whitespace and split
    parts = line.strip().split()
    if len(parts) < 4:
        return None
    
    # First part should be opcode like "$A6"
    opcode_part = parts[0]
    if not opcode_part.startswith('$'):
        return None
    
    opcode_hex = opcode_part[1:].upper()
    
    # Second part should be cycle count
    try:
        cycles_from_output = int(parts[1])
    except ValueError:
        return None
    
    # Remaining parts form the mnemonic and mode
    mnemonic_mode = ' '.join(parts[2:])
    
    return opcode_hex, cycles_from_output, mnemonic_mode

def main():
    # Load reference data
    reference_cycles = load_reference_cycles()
    print(f"Loaded {len(reference_cycles)} reference cycle counts from opcodes_and_cycles.tsv")
    
    # Determine input source
    if len(sys.argv) > 1:
        # Read from file
        try:
            with open(sys.argv[1], 'r') as f:
                lines = f.readlines()
        except FileNotFoundError:
            print(f"Error: File '{sys.argv[1]}' not found.")
            sys.exit(1)
    else:
        # Read from stdin
        lines = sys.stdin.readlines()
    
    mismatches = []
    unknown_opcodes = []
    total_instructions = 0
    
    print("\nAnalyzing instruction counter output...")
    print("=" * 60)
    
    for line in lines:
        result = parse_instruction_counter_line(line)
        if result is None:
            continue
            
        opcode_hex, cycles_from_output, mnemonic_mode = result
        total_instructions += 1
        
        # Check if we have reference data for this opcode
        if opcode_hex not in reference_cycles:
            unknown_opcodes.append((opcode_hex, cycles_from_output, mnemonic_mode))
            continue
        
        reference_cycle_count = reference_cycles[opcode_hex]
        
        # Check for mismatch
        if cycles_from_output != reference_cycle_count:
            mismatches.append({
                'opcode': opcode_hex,
                'mnemonic': mnemonic_mode,
                'reference_cycles': reference_cycle_count,
                'output_cycles': cycles_from_output,
                'difference': cycles_from_output - reference_cycle_count
            })
    
    # Report results
    print(f"\nProcessed {total_instructions} instructions")
    
    if unknown_opcodes:
        print(f"\n{len(unknown_opcodes)} instructions with unknown opcodes:")
        for opcode, cycles, mnemonic in unknown_opcodes:
            print(f"  ${opcode}: {cycles} cycles - {mnemonic}")
    
    if mismatches:
        print(f"\n{len(mismatches)} instructions with cycle count mismatches:")
        print(f"{'Opcode':<6} {'Mnemonic':<20} {'Ref':<4} {'Out':<4} {'Diff':<5} {'Details'}")
        print("-" * 60)
        
        for mismatch in mismatches:
            print(f"${mismatch['opcode']:<4} {mismatch['mnemonic']:<20} "
                  f"{mismatch['reference_cycles']:<4} {mismatch['output_cycles']:<4} "
                  f"{mismatch['difference']:+4d}   "
                  f"Expected {mismatch['reference_cycles']}, got {mismatch['output_cycles']}")
        
        print(f"\nMismatch summary:")
        print(f"  - {len([m for m in mismatches if m['difference'] > 0])} instructions took MORE cycles than expected")
        print(f"  - {len([m for m in mismatches if m['difference'] < 0])} instructions took FEWER cycles than expected")
        
    else:
        print("\n✓ All instruction cycle counts match the reference data!")
    
    if unknown_opcodes or mismatches:
        print(f"\nTotal issues found: {len(unknown_opcodes) + len(mismatches)}")
        return 1
    else:
        return 0

if __name__ == "__main__":
    sys.exit(main())
