from time import ticks_ms, ticks_diff, ticks_us, sleep_us
from pia_tester import PIA
from pio_bus_access import eclock_start, eclock_stop, eclock_set_mode, ECLOCK_EXTERNAL
from array import array


U10 = PIA(0x88)  # U10
U11 = PIA(0x90)  # U11


def scan_once():
    """Generate pulses from all the pins of U10 and U11 (PA0-7, CA2, PB0-7, CB2)"""
    U10.prb = 0
    U11.prb = 0
    U10.ca2 = 0
    U11.ca2 = 0
    U10.cb2 = 0
    U11.cb2 = 0
    for i in range(8):
        bit = 1 << i
        U10.pra = bit
        U11.pra = bit
    U10.pra = 0
    U11.pra = 0
    U10.ca2 = 1
    U11.ca2 = 1
    U10.ca2 = 0
    U11.ca2 = 0
    for i in range(8):
        bit = 1 << i
        U10.prb = bit
        U11.prb = bit
    U10.prb = 0
    U11.prb = 0
    U10.cb2 = 1
    U11.cb2 = 1
    U10.cb2 = 0
    U11.cb2 = 0


def register_report():
    print("U10:")
    U10.dump_regs()
    print("U11:")
    U11.dump_regs()


def run_output_test():
    # Set up both PIAs as all outputs
    U10.ddra = 0xFF
    U10.ddrb = 0xFF
    U11.ddra = 0xFF
    U11.ddrb = 0xFF
    U10.pra = 0x00
    U10.prb = 0x00
    U11.pra = 0x00
    U11.prb = 0x00
    U10.ca2 = 0
    U11.ca2 = 0
    U10.cb2 = 0
    U11.cb2 = 0
    register_report()

    started = ticks_ms()
    scan_once()
    ended = ticks_ms()
    duration = ticks_diff(ended, started)
    print(f"One scan takes {duration} ms (high period={duration / 8} ms")
    while True:
        scan_once()


def read_switches_once(sws):
    """Using U10 port A and CA2 as the strobes, read the switch states on port B."""
    for i in range(8):
        bit = 1 << i
        U10.pra = bit
        sleep_us(10)
        sws[i] = U10.prb
    U10.pra = 0
    U10.cb2 = 1
    sws[8] = U10.prb
    U10.cb2 = 0


def print_dip_switch_states(current_switches):
    for i in range(5, 9):
        sw = current_switches[i]
        print(f"{i}: {sw:08b}")


def read_switches():
    last_switches = bytearray(9)
    current_switches = bytearray(9)
    last_mv = memoryview(last_switches)
    U10.ddra = 0xFF  # PA0-PA7 all ouputs
    U10.ddrb = 0x00  # PB0-PB7 all inputs
    U10.pra = 0
    U10.ca2 = 0
    U10.cb2 = 0
    register_report()
    read_switches_once(current_switches)
    last_mv[:] = current_switches
    print_dip_switch_states(current_switches)
    while True:
        read_switches_once(current_switches)
        if current_switches != last_switches:
            diffs = "".join(
                map(
                    lambda x, y: f"{x ^ y:08b}",
                    current_switches,
                    last_switches,
                )
            )
            print(diffs)
            last_mv[:] = current_switches


eclock_set_mode(ECLOCK_EXTERNAL)
eclock_start()


try:
    print("Driving PRA/PRB/CA2/CB2 (Ctrl-C to break)")
    run_output_test()
except KeyboardInterrupt:
    pass

try:
    print("Reading switches (Ctrl-C to break)")
    read_switches()
except KeyboardInterrupt:
    pass
