from micropython import const
from bus_cycle_pio import bus_read_byte, bus_write_byte

DDR_BIT = const(0x04)


class PIA:
    def __init__(self, base_address):
        self._addr = base_address
        self._cra = 0
        self._ddra = 0
        self._pra = 0
        self._crb = 0
        self._ddrb = 0
        self._prb = 0

        self._pra_addr = self._addr
        self._ddra_addr = self._addr
        self._cra_addr = self._addr + 1
        self._prb_addr = self._addr + 2
        self._ddrb_addr = self._addr + 2
        self._crb_addr = self._addr + 3

    @property
    def cra(self):
        """Return the CRA contents."""
        self._cra = bus_read_byte(self._cra_addr)
        return self._cra

    @cra.setter
    def cra(self, value):
        """Set the CRA contents."""
        bus_write_byte(self._cra_addr, value)
        self._cra = value

    @property
    def crb(self):
        """Return the CRB contents."""
        self._crb = bus_read_byte(self._crb_addr)
        return self._crb

    @crb.setter
    def crb(self, value):
        """Set the CRB contents."""
        bus_write_byte(self._crb_addr, value)
        self._crb = value

    @property
    def pra(self):
        """Return the PRA contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRA
        self._cra = bus_read_byte(self._cra_addr)
        if not (self._cra & DDR_BIT):
            self.cra = self._cra | DDR_BIT
        self._pra = bus_read_byte(self._pra_addr)
        return self._pra

    @pra.setter
    def pra(self, value):
        """Set the PRA contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRA
        self._cra = bus_read_byte(self._cra_addr)
        if not (self._cra & DDR_BIT):
            self.cra = self._cra | DDR_BIT
        bus_write_byte(self._cra_addr, value)
        self._pra = value

    @property
    def prb(self):
        """Return the PRB contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRB
        self._crb = bus_read_byte(self._crb_addr)
        if not (self._crb & DDR_BIT):
            self.crb = self._crb | DDR_BIT
        self._prb = bus_read_byte(self._prb_addr)
        return self._prb

    @prb.setter
    def prb(self, value):
        """Set the PRB contents."""
        # Sync with hardware and ensure DDR_BIT is set to select PRB
        self._crb = bus_read_byte(self._crb_addr)
        if not (self._crb & DDR_BIT):
            self.crb = self._crb | DDR_BIT
        bus_write_byte(self._prb_addr, value)
        self._prb = value

    @property
    def ddra(self):
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
    def ddra(self, value):
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
    def ddrb(self):
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
    def ddrb(self, value):
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
