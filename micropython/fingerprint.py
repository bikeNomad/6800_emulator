import struct
import gc
from micropython import const
import modbus_crc16
from main_pio_test import (
    eclock_start,
    eclock_stop,
    bus_read_block,
    bus_write_block,
    bus_read_cycle as bus_read_byte,
    bus_write_cycle as bus_write_byte,
)


PAGE_SIZE = const(256)
PAGE_ADDRESS_MASK = const(0xFF00)
PRA_OFFSET = const(0)
DDRA_OFFSET = const(0)
CRA_OFFSET = const(1)
PRB_OFFSET = const(2)
DDRB_OFFSET = const(2)
CRB_OFFSET = const(3)
SELECT_PR_BIT = const(0x04)  # set this bit in CRx to access PRx instead of DDRx

TEST_DATA = b"This is a test of the bus"

DETECTED = {}


# Return integer
def crc16(data):
    crc = modbus_crc16.crc16(data)
    # convert from bytes to integer
    return struct.unpack("H", crc)[0]


def _is_empty(data):
    return all(map(lambda x: x == 0xFF, data)) or all(map(lambda x: x == 0x00, data))


def detect_empty(address):
    """Return 'EMPTY' if the given address range is empty and reads 0xFF"""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    if _is_empty(block_data):
        return "EMPTY"
    return None


def detect_sys3_7_cmos(address):
    """Return 'CMOS' if a System 3-7 CMOS RAM is at the address.
    This RAM reads 0xF in the high nybble and stores the low nybble."""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    # Check for CMOS pattern
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
    return "CMOS"


def detect_ram(address):
    """Return 'RAM' if the given address range appears to be RAM"""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    # Write test data
    bus_write_block(address, TEST_DATA)
    # Read data back
    block_data_readback = bus_read_block(address, len(TEST_DATA))
    # Restore original data
    bus_write_block(address, block_data)
    if block_data_readback == TEST_DATA:
        return "RAM"
    return None


def detect_pia(address):
    """Return 'PIA (crc16)' if a 6820 or 6821 PIA is at the start of the given address range"""
    address &= PAGE_ADDRESS_MASK
    block_data = bus_read_block(address, PAGE_SIZE)
    # Check if block repeats the same 4 bytes
    # if any(block_data[i] != block_data[i % 4] for i in range(PAGE_SIZE)):
    #   return False
    # Save state in order to restore it later
    pra_ddra, cra, prb_ddrb, crb = block_data[PRA_OFFSET : PRA_OFFSET + 4]
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

        pras = bytearray(pra_now, prb_now)
        crc = crc16(pras)
        return f"PIA ({crc:04x})"
    else:
        return None


def initialize():
    for base in range(0, 0x10000, PAGE_SIZE):
        DETECTED[base] = None


def fingerprint(address):
    return (
        detect_pia(address)
        or detect_ram(address)
        or detect_sys3_7_cmos(address)
        or detect_empty(address)
        or "ROM"
    )


def scan():
    gc.collect()
    was_started = eclock_start()
    for base in range(0, 0x10000, PAGE_SIZE):
        DETECTED[base] = fingerprint(base)
        gc.collect()
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


def print_range(address, length, type):
    if type == "ROM":
        label = f"{type} ({rom_crc16(address, length):04x})"
    else:
        label = type
    print(f"{address:04x}-{address + length - 1:04x}: {label}")
    return (address, length, label)


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
    # Group addresses by label
    label_to_addrs = {}
    for addr, (length, label) in summary.items():
        label_to_addrs.setdefault(label, []).append((addr, length))

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
            aliased_str = ", ".join(f"${a:04x}" for a in sorted(aliased))
            print(f"  Aliased at: {aliased_str}")
        bits_str = ", ".join(undecoded_bits)
        verb = "is" if len(undecoded_bits) == 1 else "are"
        print(f"  {bits_str} {verb} not decoded")

        # Report the ROM range size based on the first entry's length
        length = addr_list[0][1]
        print(
            f"  ROM range: ${base_addr:04x}-${base_addr + length - 1:04x} ({length // 256}KB)"
        )
        print()  # Restore original data
