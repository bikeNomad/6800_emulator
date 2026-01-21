from time import ticks_ms, ticks_diff, ticks_us, sleep_us
from pia_tester import PIA
from pio_bus_access import eclock_start, eclock_stop, eclock_set_mode, ECLOCK_EXTERNAL
from array import array


U10 = PIA(0x88)  # U10
U11 = PIA(0x90)  # U11


def scan_once():
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
    print(f"U10 DDRA={hex(U10.ddra)} DDRB={hex(U10.ddrb)}")
    print(f"U10 CRA={hex(U10.cra)} CRB={hex(U10.crb)}")
    print(f"U10 PRA={hex(U10.pra)} PRB={hex(U10.prb)}")
    print(f"U11 DDRA={hex(U11.ddra)} DDRB={hex(U11.ddrb)}")
    print(f"U11 CRA={hex(U11.cra)} CRB={hex(U11.crb)}")
    print(f"U11 PRA={hex(U11.pra)} PRB={hex(U11.prb)}")


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
