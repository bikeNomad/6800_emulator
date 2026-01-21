from time import ticks_ms, ticks_diff, ticks_us, sleep_us
from pia_tester import PIA
from pio_bus_access import eclock_start, eclock_stop, eclock_set_mode, ECLOCK_EXTERNAL
from array import array


PIA1 = PIA(0x88)  # U10
PIA2 = PIA(0x90)  # U11


def scan_once():
    for i in range(8):
        bit = 1 << i
        PIA1.pra = bit
        PIA1.prb = bit
        PIA2.pra = bit
        PIA2.prb = bit


def register_report():
    print(f"U10 DDRA={hex(PIA1.ddra)} DDRB={hex(PIA1.ddrb)}")
    print(f"U10 PRA={hex(PIA1.pra)} PRB={hex(PIA1.prb)}")
    print(f"U11 DDRA={hex(PIA2.ddra)} DDRB={hex(PIA2.ddrb)}")
    print(f"U11 PRA={hex(PIA2.pra)} PRB={hex(PIA2.prb)}")


def run_output_test():
    # Set up both PIAs as all outputs
    PIA1.ddra = 0xFF
    PIA1.ddrb = 0xFF
    PIA2.ddra = 0xFF
    PIA2.ddrb = 0xFF
    PIA1.pra = 0x00
    PIA1.prb = 0x00
    PIA2.pra = 0x00
    PIA2.prb = 0x00
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
        PIA1.pra = bit
        sleep_us(10)
        sws[i] = PIA1.prb


def print_dip_switch_states(current_switches):
    for i in range(5, 8):
        sw = current_switches[i]
        print(f"{i}: {sw:08b}")


def read_switches():
    last_switches = bytearray(8)
    current_switches = bytearray(8)
    PIA1.ddra = 0xFF  # PA0-PA7 all ouputs
    PIA1.ddrb = 0x00  # PB0-PB7 all inputs
    PIA1.pra = 0xFF
    sleep_us(10)
    register_report()
    read_switches_once(current_switches)
    last_switches = current_switches
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
            last_switches = current_switches


eclock_start()

try:
    print("Driving PRA/PRB...")
    run_output_test()
except KeyboardInterrupt:
    pass

try:
    print("Reading switches...")
    read_switches()
except KeyboardInterrupt:
    pass

eclock_stop()
