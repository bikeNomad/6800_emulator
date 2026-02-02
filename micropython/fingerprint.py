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

import struct
import gc
from micropython import const
import modbus_crc16
from pio_bus_access import (
    eclock_start,
    eclock_stop,
    bus_read_block,
    bus_write_block,
    bus_read_byte,
    bus_write_byte,
)


PAGE_SIZE = const(256)
PAGE_ADDRESS_MASK = const(0xFF00)
NUM_PAGES = const(256)
PRA_OFFSET = const(0)
DDRA_OFFSET = const(0)
CRA_OFFSET = const(1)
PRB_OFFSET = const(2)
DDRB_OFFSET = const(2)
CRB_OFFSET = const(3)
SELECT_PR_BIT = const(0x04)  # set this bit in CRx to access PRx instead of DDRx

# Page classification types (matching C enum)
PAGE_UNMAPPED = const(0)
PAGE_EMPTY = const(1)
PAGE_ROM = const(2)
PAGE_RAM = const(3)
PAGE_PIA = const(4)
PAGE_WMS_CMOS = const(5)
PAGE_BALLY_CMOS = const(6)
PAGE_BALLY_ZERO_PAGE = const(7)

# Architecture types
ARCH_UNKNOWN = const(0)
ARCH_EARLY_BALLY = const(1)
ARCH_WILLIAMS_SYS3_6 = const(2)
ARCH_WILLIAMS_SYS7 = const(3)
ARCH_WILLIAMS_SYS9 = const(4)
ARCH_WILLIAMS_SYS11 = const(5)

TEST_DATA = b"This is a test of the bus"

# Scan results storage - one per 256-byte page
results = [PAGE_UNMAPPED] * NUM_PAGES

# Legacy dict for compatibility
DETECTED = {}


def page_type_to_string(page_type):
    """Convert page type constant to string"""
    names = {
        PAGE_UNMAPPED: "UNMAPPED",
        PAGE_EMPTY: "EMPTY",
        PAGE_ROM: "ROM",
        PAGE_RAM: "RAM",
        PAGE_PIA: "PIA",
        PAGE_WMS_CMOS: "WMS CMOS (4-bit)",
        PAGE_BALLY_CMOS: "BALLY CMOS (4-bit)",
        PAGE_BALLY_ZERO_PAGE: "BALLY ZERO PAGE",
    }
    return names.get(page_type, "UNKNOWN")


def architecture_name(arch):
    """Convert architecture constant to string"""
    names = {
        ARCH_UNKNOWN: "Unknown",
        ARCH_EARLY_BALLY: "Early Bally or Stern",
        ARCH_WILLIAMS_SYS3_6: "Williams System 3-6",
        ARCH_WILLIAMS_SYS7: "Williams System 7",
        ARCH_WILLIAMS_SYS9: "Williams System 9",
        ARCH_WILLIAMS_SYS11: "Williams System 11",
    }
    return names.get(arch, "Unknown")


# Return integer
def crc16(data):
    crc = modbus_crc16.crc16(data)
    # convert from bytes to integer
    return struct.unpack("H", crc)[0]


def _is_empty(data):
    return all(map(lambda x: x == 0xFF, data)) or all(map(lambda x: x == 0x00, data))


def detect_empty(address):
    """Return PAGE_EMPTY if the given address range is empty (all 0xFF or all 0x00)"""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    if _is_empty(block_data):
        return PAGE_EMPTY
    return None


def detect_sys3_7_cmos(address):
    """Return PAGE_WMS_CMOS if a System 3-7 CMOS RAM is at the address.
    This RAM reads 0xF in the high nybble and stores the low nybble."""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    # Check for CMOS pattern (high nybble = 0xF)
    for i in range(PAGE_SIZE):
        if (block_data[i] & 0xF0) != 0xF0:
            return None
    # Write test data
    bus_write_block(address, TEST_DATA)
    # Read data back
    block_data_readback = bus_read_block(address, len(TEST_DATA))
    # Restore original data
    bus_write_block(address, block_data)
    # Check that low nybbles match
    for i in range(len(TEST_DATA)):
        if (block_data_readback[i] & 0x0F) != (TEST_DATA[i] & 0x0F):
            return None
    return PAGE_WMS_CMOS


def detect_bally_cmos(address):
    """Return PAGE_BALLY_CMOS if Bally CMOS RAM is at the address.
    This RAM reads 0xF in the low nybble and stores the high nybble."""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    # Check for CMOS pattern (low nybble = 0xF)
    for i in range(PAGE_SIZE):
        if (block_data[i] & 0x0F) != 0x0F:
            return None
    # Write test data
    bus_write_block(address, TEST_DATA)
    # Read data back
    block_data_readback = bus_read_block(address, len(TEST_DATA))
    # Restore original data
    bus_write_block(address, block_data)
    # Check that high nybbles match
    for i in range(len(TEST_DATA)):
        if (block_data_readback[i] & 0xF0) != (TEST_DATA[i] & 0xF0):
            return None
    return PAGE_BALLY_CMOS


def detect_ram(address):
    """Return PAGE_RAM if the given address range appears to be RAM"""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    # Write test data
    bus_write_block(address, TEST_DATA)
    # Read data back
    block_data_readback = bus_read_block(address, len(TEST_DATA))
    # Restore original data
    bus_write_block(address, block_data)
    if block_data_readback == TEST_DATA:
        return PAGE_RAM
    return None


def detect_pia(address):
    """Return PAGE_PIA if a 6820 or 6821 PIA is at the given address.
    Note: Does NOT apply page mask - caller must pass exact address."""
    # Read the 4 PIA registers individually (like C version)
    pra_ddra = bus_read_byte(address + PRA_OFFSET)
    cra = bus_read_byte(address + CRA_OFFSET)
    prb_ddrb = bus_read_byte(address + PRB_OFFSET)
    crb = bus_read_byte(address + CRB_OFFSET)
    if cra & SELECT_PR_BIT:
        pra = pra_ddra
        bus_write_byte(address + CRA_OFFSET, cra & ~SELECT_PR_BIT)
        ddra = bus_read_byte(address + DDRA_OFFSET)
    else:
        ddra = pra_ddra
        bus_write_byte(address + CRA_OFFSET, cra | SELECT_PR_BIT)
        pra = bus_read_byte(address + PRA_OFFSET)

    if crb & SELECT_PR_BIT:
        prb = prb_ddrb
        bus_write_byte(address + CRB_OFFSET, crb & ~SELECT_PR_BIT)
        ddrb = bus_read_byte(address + DDRB_OFFSET)
    else:
        ddrb = prb_ddrb
        bus_write_byte(address + CRB_OFFSET, crb | SELECT_PR_BIT)
        prb = bus_read_byte(address + PRB_OFFSET)

    retval = True

    # Set both DDRs to all-inputs
    bus_write_byte(address + CRA_OFFSET, 0x00)
    bus_write_byte(address + DDRA_OFFSET, 0x00)
    ddra_readback = bus_read_byte(address + DDRA_OFFSET)

    bus_write_byte(address + CRB_OFFSET, 0x00)
    bus_write_byte(address + DDRB_OFFSET, 0x00)
    ddrb_readback = bus_read_byte(address + DDRB_OFFSET)

    # both readbacks should be 0
    if ddra_readback != 0x00 or ddrb_readback != 0x00:
        retval = False

    if retval:
        # Access both PRs
        bus_write_byte(address + CRA_OFFSET, SELECT_PR_BIT)
        bus_write_byte(address + CRB_OFFSET, SELECT_PR_BIT)
        # Check that SELECT_PR_BIT is set in both CRs
        cra_now = bus_read_byte(address + CRA_OFFSET)
        crb_now = bus_read_byte(address + CRB_OFFSET)
        if cra_now & SELECT_PR_BIT == 0 or crb_now & SELECT_PR_BIT == 0:
            retval = False

    if retval:
        # Read both PRs
        pra_now = bus_read_byte(address + PRA_OFFSET)
        prb_now = bus_read_byte(address + PRB_OFFSET)

        # try to write into the PRs
        bus_write_byte(address + PRA_OFFSET, 0xFF)
        bus_write_byte(address + PRB_OFFSET, 0xFF)
        pra_written = bus_read_byte(address + PRA_OFFSET)
        prb_written = bus_read_byte(address + PRB_OFFSET)

        # pra_now should == pra_written and
        # prb_now should == prb_written
        if pra_now != pra_written or prb_now != prb_written:
            retval = False

    # Restore state
    # Access DDRs
    bus_write_byte(address + CRA_OFFSET, cra | SELECT_PR_BIT)
    bus_write_byte(address + CRB_OFFSET, crb | SELECT_PR_BIT)
    # Write back DDRs
    bus_write_byte(address + DDRA_OFFSET, ddra)
    bus_write_byte(address + DDRB_OFFSET, ddrb)

    # Access PRs
    bus_write_byte(address + CRA_OFFSET, cra & ~SELECT_PR_BIT)
    bus_write_byte(address + CRB_OFFSET, crb & ~SELECT_PR_BIT)
    # Write back PRs
    bus_write_byte(address + PRA_OFFSET, pra)
    bus_write_byte(address + PRB_OFFSET, prb)

    # Restore CRs
    bus_write_byte(address + CRA_OFFSET, cra)
    bus_write_byte(address + CRB_OFFSET, crb)

    if retval:
        if False:
            print(
                f"cra={hex(cra)} crb={hex(crb)} pra={hex(pra)} prb={hex(prb)} ddra={hex(ddra)} ddrb={hex(ddrb)}"
            )
            print(f"cra_now={hex(cra_now)} crb_now={hex(crb_now)}")
            print(
                f"ddra_readback={hex(ddra_readback)} ddrb_readback={hex(ddrb_readback)}"
            )
            print(f"pra_now={hex(pra_now)} prb_now={hex(prb_now)}")
            print(f"pra_written={hex(pra_written)} prb_written={hex(prb_written)}")

        return PAGE_PIA
    else:
        return None


def detect_bally_pia(address):
    """Detect PIA with lenient checks for Bally 4-bit CMOS interference.
    Only checks that CRs respond to SELECT_PR_BIT, ignores DDR readback issues."""
    # Read initial CR values
    cra = bus_read_byte(address + CRA_OFFSET)
    crb = bus_read_byte(address + CRB_OFFSET)

    # Clear SELECT_PR_BIT to access DDRs
    bus_write_byte(address + CRA_OFFSET, 0x00)
    bus_write_byte(address + CRB_OFFSET, 0x00)

    # Set SELECT_PR_BIT to access PRs
    bus_write_byte(address + CRA_OFFSET, SELECT_PR_BIT)
    bus_write_byte(address + CRB_OFFSET, SELECT_PR_BIT)

    # Read back CRs - SELECT_PR_BIT should be set
    cra_now = bus_read_byte(address + CRA_OFFSET)
    crb_now = bus_read_byte(address + CRB_OFFSET)

    # Restore original state
    bus_write_byte(address + CRA_OFFSET, cra)
    bus_write_byte(address + CRB_OFFSET, crb)

    # Check that SELECT_PR_BIT is set in both CRs
    # (may have CMOS interference in other bits, but bit 2 should respond)
    return (cra_now & SELECT_PR_BIT) != 0 and (crb_now & SELECT_PR_BIT) != 0


def detect_bally_page0(address):
    """Return PAGE_BALLY_ZERO_PAGE if this is an early Bally/Stern page 0.
    Bally: PIAs at 0x88 and 0x90. Stern: PIAs at 0xA0 and 0xC0.
    RAM may be 4-bit CMOS-style."""
    if address != 0:
        return None
    # Check for PIAs at expected Bally locations (use lenient detection)
    if detect_bally_pia(0x88) and detect_bally_pia(0x90):
        return PAGE_BALLY_ZERO_PAGE
    # Check for PIAs at expected Stern locations
    if detect_bally_pia(0xA0) and detect_bally_pia(0xC0):
        return PAGE_BALLY_ZERO_PAGE
    return None


def test_ram_detection(address):
    """Debug function to test RAM detection at a specific address."""
    print(f"Testing RAM detection at ${address:04X}")

    # Read original data
    original = bus_read_block(address, len(TEST_DATA))
    print(f"  Original data: {original.hex()}")

    # Write test data
    bus_write_block(address, TEST_DATA)
    print(f"  Wrote: {TEST_DATA.hex()}")

    # Read back
    readback = bus_read_block(address, len(TEST_DATA))
    print(f"  Readback:      {readback.hex()}")

    # Restore
    bus_write_block(address, original)

    # Compare
    if readback == TEST_DATA:
        print("  PASS: RAM detected (full 8-bit)")
    else:
        # Check for 4-bit CMOS pattern
        high_match = all((readback[i] & 0xF0) == (TEST_DATA[i] & 0xF0)
                         for i in range(len(TEST_DATA)))
        low_match = all((readback[i] & 0x0F) == (TEST_DATA[i] & 0x0F)
                        for i in range(len(TEST_DATA)))
        if high_match and not low_match:
            print("  FAIL: 4-bit RAM (high nybble only, low reads as 0xF)")
        elif low_match and not high_match:
            print("  FAIL: 4-bit RAM (low nybble only, high reads as 0xF)")
        else:
            print("  FAIL: No RAM or unknown pattern")


def test_pia_detection(address):
    """Debug function to test PIA detection at a specific address.
    Call with eclock running: eclock_start(); test_pia_detection(0x88)"""
    print(f"Testing PIA detection at ${address:04X}")

    # Read the 4 PIA registers
    pra_ddra = bus_read_byte(address + PRA_OFFSET)
    cra = bus_read_byte(address + CRA_OFFSET)
    prb_ddrb = bus_read_byte(address + PRB_OFFSET)
    crb = bus_read_byte(address + CRB_OFFSET)
    print(f"  Initial: PRA/DDRA=${pra_ddra:02X} CRA=${cra:02X} "
          f"PRB/DDRB=${prb_ddrb:02X} CRB=${crb:02X}")

    # Try to access DDRs by clearing SELECT_PR_BIT in CRs
    bus_write_byte(address + CRA_OFFSET, 0x00)
    bus_write_byte(address + CRB_OFFSET, 0x00)

    # Write 0x00 to DDRs
    bus_write_byte(address + DDRA_OFFSET, 0x00)
    bus_write_byte(address + DDRB_OFFSET, 0x00)

    # Read back DDRs
    ddra_rb = bus_read_byte(address + DDRA_OFFSET)
    ddrb_rb = bus_read_byte(address + DDRB_OFFSET)
    print(f"  After DDR=0x00: DDRA readback=${ddra_rb:02X} "
          f"DDRB readback=${ddrb_rb:02X}")

    if ddra_rb != 0x00 or ddrb_rb != 0x00:
        print("  FAIL: DDRs did not read back as 0x00")
    else:
        print("  PASS: DDRs read back as 0x00")

    # Try setting SELECT_PR_BIT in CRs
    bus_write_byte(address + CRA_OFFSET, SELECT_PR_BIT)
    bus_write_byte(address + CRB_OFFSET, SELECT_PR_BIT)

    cra_now = bus_read_byte(address + CRA_OFFSET)
    crb_now = bus_read_byte(address + CRB_OFFSET)
    print(f"  After CR=0x04: CRA=${cra_now:02X} CRB=${crb_now:02X}")

    if (cra_now & SELECT_PR_BIT) == 0 or (crb_now & SELECT_PR_BIT) == 0:
        print("  FAIL: SELECT_PR_BIT not set in CRs")
    else:
        print("  PASS: SELECT_PR_BIT set in CRs")

    # Restore original state
    bus_write_byte(address + CRA_OFFSET, cra)
    bus_write_byte(address + CRB_OFFSET, crb)

    # Check Bally-style detection (lenient - only checks SELECT_PR_BIT)
    bally_pia = (cra_now & SELECT_PR_BIT) != 0 and (crb_now & SELECT_PR_BIT) != 0
    print(f"  Bally PIA detection: {'PASS' if bally_pia else 'FAIL'}")
    print("  State restored")


def initialize():
    """Initialize results array and legacy DETECTED dict"""
    global results
    results = [PAGE_UNMAPPED] * NUM_PAGES
    for base in range(0, 0x10000, PAGE_SIZE):
        DETECTED[base] = None


def fingerprint_page(address):
    """Determine page type at address. Returns PAGE_* constant.
    Order matches C version: PIA, Bally page0, RAM, WMS CMOS, Bally CMOS, empty, ROM"""
    # Ensure page-aligned address
    address &= PAGE_ADDRESS_MASK

    # Check for early Bally/Stern page 0 FIRST
    # (must be before CMOS detection since Bally page 0 has 4-bit RAM)
    if address == 0x0000:
        result = detect_bally_page0(address)
        if result is not None:
            return result

    # PIA must be checked before RAM to avoid false RAM detection
    result = detect_pia(address)
    if result is not None:
        return result

    result = detect_ram(address)
    if result is not None:
        return result

    result = detect_sys3_7_cmos(address)
    if result is not None:
        return result

    result = detect_bally_cmos(address)
    if result is not None:
        return result

    result = detect_empty(address)
    if result is not None:
        return result

    # Default to ROM for anything else
    return PAGE_ROM


def fingerprint(address):
    """Legacy wrapper - returns string for compatibility"""
    page_type = fingerprint_page(address)
    return page_type_to_string(page_type)


def page_types_equivalent(a, b):
    """Check if two page types are equivalent for aliasing detection.
    PAGE_BALLY_ZERO_PAGE is functionally RAM for aliasing purposes."""
    if a == b:
        return True
    # PAGE_BALLY_ZERO_PAGE is equivalent to PAGE_RAM for aliasing purposes
    if (a == PAGE_BALLY_ZERO_PAGE and b == PAGE_RAM) or (
        a == PAGE_RAM and b == PAGE_BALLY_ZERO_PAGE
    ):
        return True
    return False


def count_decoded_address_bits():
    """Detect how many address bits are actually decoded by checking for repeating patterns.
    Returns number of decoded bits (8-16)."""
    # For N decoded bits: section_size = 2^(N-8) pages, num_copies = 2^(16-N)
    # If all copies match the first section, then only N bits are decoded
    for bits in range(15, 7, -1):
        section_size = 1 << (bits - 8)  # Pages per unique section
        num_copies = NUM_PAGES // section_size  # Number of copies

        all_match = True
        # Compare each copy with the first section (copy 0)
        for i in range(section_size):
            if not all_match:
                break
            for copy in range(1, num_copies):
                compare_page = copy * section_size + i
                if not page_types_equivalent(results[i], results[compare_page]):
                    all_match = False
                    break

        if not all_match:
            # Copies don't match, so at least (bits+1) bits are decoded
            return bits + 1
        # All copies match, try checking for fewer decoded bits

    return 8  # Minimum 8 bits (256 bytes per page)


def recognize_architecture():
    """Determine system architecture from scan results.
    Returns (architecture_type, decoded_bits) tuple."""
    decoded_bits = count_decoded_address_bits()

    # Early Bally/Stern
    if (
        decoded_bits < 16
        and results[0x00] == PAGE_BALLY_ZERO_PAGE
        and results[0x02] == PAGE_BALLY_CMOS
    ):
        return ARCH_EARLY_BALLY, decoded_bits

    # Count CMOS at specific addresses (System 3-7 signature)
    expected_cmos_pages = [0x01, 0x05, 0x09, 0x0D]
    cmos_count = sum(
        1 for page in expected_cmos_pages if results[page] == PAGE_WMS_CMOS
    )

    # Count RAM pages at start (System 11 signature)
    ram_count = sum(1 for i in range(0x08) if results[i] == PAGE_RAM)

    # Count PIA pages in expected places (System 3-7, 9/11)
    expected_pia_pages = [0x21, 0x22, 0x24, 0x28, 0x2C, 0x30, 0x34, 0x40]
    pia_count = sum(1 for page in expected_pia_pages if results[page] == PAGE_PIA)

    print(
        f"Recognize architecture: Bits:{decoded_bits}, RAM:{ram_count}, PIA:{pia_count}, CMOS:{cmos_count}"
    )

    # System 3-7: CMOS pattern, 15 bits decoded
    if cmos_count == 4 and results[0x01] == PAGE_WMS_CMOS and decoded_bits == 15:
        if pia_count == 5:
            return ARCH_WILLIAMS_SYS7, decoded_bits
        else:
            return ARCH_WILLIAMS_SYS3_6, decoded_bits

    # System 9/11: 2K RAM at start, no 4-bit CMOS
    if cmos_count == 0 and ram_count == 8:
        # System 9: A15 not decoded, only 4 PIAs
        if pia_count == 4 and decoded_bits == 15:
            return ARCH_WILLIAMS_SYS9, decoded_bits
        # System 11: 2K Contiguous RAM at start, no CMOS pattern, A15 decoded
        if pia_count == 6 and decoded_bits == 16:
            return ARCH_WILLIAMS_SYS11, decoded_bits

    return ARCH_UNKNOWN, decoded_bits


def scan():
    """Scan all memory pages and store results."""
    global results
    gc.collect()
    was_started = eclock_start()
    print("Scanning 256 pages...")
    for page in range(NUM_PAGES):
        addr = page << 8
        results[page] = fingerprint_page(addr)
        # Also update legacy dict
        DETECTED[addr] = page_type_to_string(results[page])
        gc.collect()
    print("Scan complete")
    if not was_started:
        eclock_stop()


def rom_crc16(address, length):
    was_started = eclock_start()
    data = bus_read_block(address, length)
    crc = crc16(data)
    if not was_started:
        eclock_stop()
    gc.collect()
    return crc


def print_scan_results():
    """Print scan results in coalesced format (matching C version)."""
    start = 0
    last_type = results[0]

    for page in range(1, NUM_PAGES + 1):
        current_type = results[page] if page < NUM_PAGES else 255  # 255 = sentinel

        if current_type != last_type or page == NUM_PAGES:
            end = (page << 8) - 1
            type_str = page_type_to_string(last_type)
            print(f"  ${start:04X}-${end:04X}: {type_str}")
            start = page << 8
            last_type = current_type


def print_range(address, length, type_str):
    if type_str == "ROM":
        label = f"{type_str} ({rom_crc16(address, length):04x})"
    else:
        label = type_str
    print(f"{address:04x}-{address + length - 1:04x}: {label}")
    return (address, length, label)


def report():
    """Print scan results with architecture recognition, return summary dict."""
    # Print raw scan results
    print_scan_results()

    # Recognize architecture
    arch, decoded_bits = recognize_architecture()
    print(f"Architecture: {architecture_name(arch)} ({decoded_bits} bits decoded)")

    # Build coalesced summary for legacy compatibility
    # For System 11 ROM space ($4000-$FFFF), coalesce consecutive ROM and EMPTY regions
    SYS11_ROM_START = 0x4000

    coalesced_regions = []
    last_type = None
    last_address = 0

    for page in range(NUM_PAGES + 1):
        base = page << 8
        page_type = results[page] if page < NUM_PAGES else 255  # sentinel
        is_rom_space = base >= SYS11_ROM_START

        # In System 11 ROM space, treat EMPTY as ROM for coalescing purposes
        if is_rom_space and page_type == PAGE_EMPTY:
            effective_type = PAGE_ROM
        else:
            effective_type = page_type

        if effective_type != last_type or page == NUM_PAGES:
            if last_type is not None and last_type != 255:
                # For ROM space regions that were coalesced, determine the correct label
                if last_type == PAGE_ROM and last_address >= SYS11_ROM_START:
                    # Check if this region contains any actual ROM data
                    has_real_rom = False
                    for check_page in range(last_address >> 8, page):
                        if results[check_page] == PAGE_ROM:
                            has_real_rom = True
                            break

                    if has_real_rom:
                        crc = rom_crc16(last_address, base - last_address)
                        label = f"ROM ({crc:04x})"
                    else:
                        label = "EMPTY"
                else:
                    label = page_type_to_string(last_type)

                coalesced_regions.append((last_address, base - last_address, label))
            last_type = effective_type
            last_address = base

    # Build summary dict
    summary = {}
    for addr, length, label in coalesced_regions:
        summary[addr] = (length, label)

    return summary


def analyze_address_decoding(summary):
    """Analyze the summary for address aliasing and incomplete address line decoding."""
    # Group addresses by label
    label_to_addrs = {}
    for addr, (length, label) in summary.items():
        label_to_addrs.setdefault(label, []).append((addr, length))

    for label, addr_list in label_to_addrs.items():
        if len(addr_list) < 2:
            continue

        addresses = sorted([addr for addr, _ in addr_list])

        print(f"Address decoding analysis for '{label}' ({len(addr_list)} instances):")

        # Show each address range separately
        range_strs = []
        for addr, length in addr_list:
            end_addr = addr + length - 1
            range_strs.append(f"${addr:04x}-${end_addr:04x}")
        ranges_str = ", ".join(range_strs)
        print(f"  Address ranges: {ranges_str}")

        # Analyze hierarchical address aliasing
        unique_addrs = set(addresses)
        aliased_bits = []

        # Check for aliasing starting from highest bit (A15) down to A0
        for bit in range(15, -1, -1):
            mask = 1 << bit
            lower_half = {addr for addr in unique_addrs if (addr & mask) == 0}
            upper_half = {addr for addr in unique_addrs if (addr & mask) != 0}

            # Check if upper half is exactly lower half + mask
            if lower_half and upper_half == {addr + mask for addr in lower_half}:
                aliased_bits.append(f"A{bit}")
                # Remove the aliased addresses, keep only lower half for further analysis
                unique_addrs = lower_half

        if aliased_bits:
            bits_str = ", ".join(aliased_bits)
            verb = "is" if len(aliased_bits) == 1 else "are"
            print(f"  Mirrored bits: {bits_str} {verb} not decoded")

        # Analyze remaining unique addresses for constant/varying bits
        if unique_addrs:
            unique_list = sorted(unique_addrs)

            # Compute XOR of unique addresses to find varying bits
            xor_val = 0
            for addr in unique_list:
                xor_val ^= addr

            # Compute AND of unique addresses to find constant bits
            and_val = unique_list[0]
            for addr in unique_list[1:]:
                and_val &= addr

            varying_bits = [f"A{i}" for i in range(16) if (xor_val >> i) & 1]
            constant_bits = []
            for i in range(16):
                if not ((xor_val >> i) & 1):
                    bit_val = (and_val >> i) & 1
                    constant_bits.append(f"A{i}={bit_val}")

            if varying_bits:
                bits_str = ", ".join(varying_bits)
                verb = "varies" if len(varying_bits) == 1 else "vary"
                print(f"  Within unique range: {bits_str} {verb}")

            if constant_bits:
                bits_str = ", ".join(constant_bits)
                print(f"  Constant bits: {bits_str}")

        # Show the specific addresses
        addr_strs = [f"${addr:04x}" for addr in addresses]
        if len(addr_strs) <= 8:
            print(f"  Addresses: {', '.join(addr_strs)}")
        else:
            addr_summary = f"{', '.join(addr_strs[:8])}... ({len(addr_strs)} total)"
            print(f"  Addresses: {addr_summary}")

        # Report the device size based on the first instance's length
        length = addr_list[0][1]
        print(f"  Device size: {length} bytes per instance")
        print()
