def report():
    """Print out a report, return a summary dict keyed by address for further analysis."""
    last_type = None
    last_address = 0
    summary = {}
    for base in range(0, 0x10000 + PAGE_SIZE, PAGE_SIZE):
        type = DETECTED.get(base)
        if type != last_type:
            if last_type is not None:
                addr, length, label = print_range(
                    last_address, base - last_address, last_type
                )
                summary[addr] = (length, label)
            last_type = type
            last_address = base
    return summary


def analyze_address_decoding(summary):
    """Analyze the summary for address aliasing and incomplete address line decoding."""
    from collections import defaultdict
    
    # Group addresses by label
    label_to_addrs = defaultdict(list)
    for addr, (length, label) in summary.items():
        label_to_addrs[label].append((addr, length))
    
    for label, addr_list in label_to_addrs.items():
        if len(addr_list) < 2:
            continue
        
        # Compute XOR of all addresses to find differing bits
        xor_val = 0
        for addr, _ in addr_list:
            xor_val ^= addr
        
        if xor_val == 0:
            continue  # No differing bits, shouldn't happen
        
        # Find all undecoded bits (set bits in XOR)
        undecoded_bits = [f"A{i}" for i in range(16) if (xor_val >> i) & 1]
        
        # Base address is the smallest address
        base_addr = min(addr for addr, _ in addr_list)
        
        # Aliased addresses are the others
        aliased = [addr for addr, _ in addr_list if addr != base_addr]
        
        print(f"Address aliasing detected for label '{label}':")
        print(f"  Base address: ${base_addr:04x}")
        if aliased:
            aliased_str = ', '.join(f"${a:04x}" for a in sorted(aliased))
            print(f"  Aliased at: {aliased_str}")
        bits_str = ', '.join(undecoded_bits)
        verb = "is" if len(undecoded_bits) == 1 else "are"
        print(f"  {bits_str} {verb} not decoded")
        
        # Report the ROM range size based on the first entry's length
        length = addr_list[0][1]
        print(f"  ROM range: ${base_addr:04x}-${base_addr + length - 1:04x} ({length // 256}KB)")
        print()    # Restore original data
