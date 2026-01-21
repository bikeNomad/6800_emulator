from micropython import const
from bus_cycle_pio import bus_read_byte, bus_write_byte

DDR_BIT = const(0x04)
CX2_CONTROL_BITS = const(0x38)  # bits 3-5 control Cx2
CX2_SR_BITS = const(0x30)  # allows CA2/CB2 output control by CRA bit 3


class PIA:
    def __init__(self, base_address):
        self._pra_addr = base_address
        self._ddra_addr = base_address
        self._cra_addr = base_address + 1
        self._prb_addr = base_address + 2
        self._ddrb_addr = base_address + 2
        self._crb_addr = base_address + 3

        self._cra = 0
        self._ddra = 0
        self._pra = 0
        self._crb = 0
        self._ddrb = 0
        self._prb = 0

    def reset(self):
        """Reset to all inputs"""
        self.cra = 0
        self.ddra = 0
        self.pra = 0
        self.crb = 0
        self.ddrb = 0
        self.prb = 0

    @property
    def cra(self) -> int:
        """Return the CRA contents."""
        self._cra = bus_read_byte(self._cra_addr)
        return self._cra

    @cra.setter
    def cra(self, value: int):
        """Set the CRA contents."""
        bus_write_byte(self._cra_addr, value)
        self._cra = value

    @property
    def crb(self) -> int:
        """Return the CRB contents."""
        self._crb = bus_read_byte(self._crb_addr)
        return self._crb

    @crb.setter
    def crb(self, value: int):
        """Set the CRB contents."""
        bus_write_byte(self._crb_addr, value)
        self._crb = value

    @property
    def pra(self) -> int:
        """Return the PRA contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRA
        self._cra = bus_read_byte(self._cra_addr)
        if not (self._cra & DDR_BIT):
            self.cra = self._cra | DDR_BIT
        self._pra = bus_read_byte(self._pra_addr)
        return self._pra

    @pra.setter
    def pra(self, value: int):
        """Set the PRA contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRA
        self._cra = bus_read_byte(self._cra_addr)
        if not (self._cra & DDR_BIT):
            self.cra = self._cra | DDR_BIT
        bus_write_byte(self._pra_addr, value)
        self._pra = value

    @property
    def prb(self) -> int:
        """Return the PRB contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRB
        self._crb = bus_read_byte(self._crb_addr)
        if not (self._crb & DDR_BIT):
            self.crb = self._crb | DDR_BIT
        self._prb = bus_read_byte(self._prb_addr)
        return self._prb

    @prb.setter
    def prb(self, value: int):
        """Set the PRB contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRB
        self._crb = bus_read_byte(self._crb_addr)
        if not (self._crb & DDR_BIT):
            self.crb = self._crb | DDR_BIT
        bus_write_byte(self._prb_addr, value)
        self._prb = value

    @property
    def ddra(self) -> int:
        """Return the DDRA contents."""
        # Sync with hardware
        self._cra = bus_read_byte(self._cra_addr)
        # Clear DDR_BIT to select DDRA
        if self._cra & DDR_BIT:
            self.cra = self._cra & ~DDR_BIT
        self._ddra = bus_read_byte(self._ddra_addr)
        # Set DDR_BIT back to normal operating state (PRA selected)
        self.cra = self._cra | DDR_BIT
        return self._ddra

    @ddra.setter
    def ddra(self, value: int):
        """Set the DDRA contents."""
        # Sync with hardware
        self._cra = bus_read_byte(self._cra_addr)
        # Clear DDR_BIT to select DDRA
        if self._cra & DDR_BIT:
            self.cra = self._cra & ~DDR_BIT
        bus_write_byte(self._ddra_addr, value)
        self._ddra = value
        # Set DDR_BIT back to normal operating state (PRA selected)
        self.cra = self._cra | DDR_BIT

    @property
    def ddrb(self) -> int:
        """Return the DDRB contents."""
        # Sync with hardware
        self._crb = bus_read_byte(self._crb_addr)
        # Clear DDR_BIT to select DDRB
        if self._crb & DDR_BIT:
            self.crb = self._crb & ~DDR_BIT
        self._ddrb = bus_read_byte(self._ddrb_addr)
        # Set DDR_BIT back to normal operating state (PRB selected)
        self.crb = self._crb | DDR_BIT
        return self._ddrb

    @ddrb.setter
    def ddrb(self, value: int):
        """Set the DDRB contents."""
        # Sync with hardware
        self._crb = bus_read_byte(self._crb_addr)
        # Clear DDR_BIT to select DDRB
        if self._crb & DDR_BIT:
            self.crb = self._crb & ~DDR_BIT
        bus_write_byte(self._ddrb_addr, value)
        self._ddrb = value
        # Set DDR_BIT back to normal operating state (PRB selected)
        self.crb = self._crb | DDR_BIT

    def set_ca2_as_output(self, value: bool = False):
        """Set CA2 as an output line."""
        self.cra = (self.cra & ~CX2_CONTROL_BITS) | (int(value) << 3) | CX2_SR_BITS

    def set_cb2_as_output(self, value: bool = False):
        """Set CB2 as an output line."""
        self.crb = (self.crb & ~CX2_CONTROL_BITS) | (int(value) << 3) | CX2_SR_BITS

    @property
    def ca2(self) -> bool:
        """Return the value of CA2 if it's an output, else False"""
        cra = self.cra & CX2_CONTROL_BITS
        if cra != 0:
            return bool(cra & 8)
        return False

    @ca2.setter
    def ca2(self, value: bool):
        self.set_ca2_as_output(value)

    @property
    def cb2(self) -> bool:
        """Return the value of CB2 if it's an output, else False"""
        crb = self.crb & CX2_CONTROL_BITS
        if crb != 0:
            return bool(crb & 8)
        return False

    @cb2.setter
    def cb2(self, value: bool):
        self.set_cb2_as_output(value)

    def dump_regs(self):
        print(f"DDRA={self.ddra:02X} DDRB={self.ddrb:02X}")
        print(f"CRA={self.cra:02X} CRB={self.crb:02X}")
        print(f"PRA={self.pra:02X} PRB={self.prb:02X}")
