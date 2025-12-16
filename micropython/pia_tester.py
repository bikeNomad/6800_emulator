from bus_test import BusTester
from hexdump import hexdump, hexdump_from_file

t = BusTester()

class PIATester:
    DDR_BIT = 0x04

    def __init__(self, bus_tester, base_address):
        self._t = bus_tester
        self._addr = base_address
        self._cra = 0
        self._ddra = 0
        self._pra = 0
        self._crb = 0
        self._ddrb = 0
        self._prb = 0
    
    @property
    def cra(self):
        return (self._cra := self._t.read_byte(self._addr))

    @cra.setter
    def cra(self, value):
        self._t.write_byte(self._addr, value)
        self._cra = value

    @property
    def crb(self):
        return (self._crb := self._t.read_byte(self._addr + 3))

    @crb.setter
    def crb(self, value):
        self._t.write_byte(self._addr + 3, value)
        self._crb = value
    
    @property
    def pra(self):
        if self._cra & DDR_BIT:
            self._pra = self.t.read_byte(self._addr)
        else:
            # set DDR_BIT in CRA first:
            self.cra = self._cra | DDR_BIT
            self._pra = self.t.read_byte(self._addr)
            # restore CRA
            self.cra = self._cra & ~DDR_BIT
        return self._pra
    
    @pra.setter
    def pra(self, value):
        if self._cra & DDR_BIT:
            self._t.write_byte(self._addr, value)
        else:
            # set DDR_BIT in CRA first:
            self.cra = self._cra | DDR_BIT
            self._t.write_byte(self._addr, value)
            # restore CRA
            self.cra = self._cra & ~DDR_BIT
        self._pra = value

    @property
    def prb(self):
        if self._crb & DDR_BIT:
            self._prb = self.t.read_byte(self._addr + 2)
        else:
            # set DDR_BIT in CRA first:
            self.crb = self._crb | DDR_BIT
            self._prb = self.t.read_byte(self._addr + 2)
            # restore CRA
            self.crb = self._crb & ~DDR_BIT
        return self._prb
    
    @prb.setter
    def prb(self, value):
        if self._crb & DDR_BIT:
            self._t.write_byte(self._addr + 2, value)
        else:
            # set DDR_BIT in CRA first:
            self.crb = self._crb | DDR_BIT
            self._t.write_byte(self._addr + 2, value)
            # restore CRA
            self.crb = self._crb & ~DDR_BIT
        self._prb = value

    @property
    def ddra(self):
        if self._cra & DDR_BIT:
            self._ddra = self.t.read_byte(self._addr)
        else:
            # reset DDR_BIT in CRA first:
            self.cra = self._cra & ~DDR_BIT
            self._ddra = self.t.read_byte(self._addr)
            # restore CRA
            self.cra = self._cra | DDR_BIT
        return self._ddra
    
    @ddra.setter
    def ddra(self, value):
        if not (self._cra & DDR_BIT):
            self._t.write_byte(self._addr, value)
        else:
            # reset DDR_BIT in CRA first:
            self.cra = self._cra & ~DDR_BIT
            self._t.write_byte(self._addr, value)
            # restore CRA
            self.cra = self._cra | DDR_BIT
        self._ddra = value

    @property
    def ddrb(self):
        if not (self._crb & DDR_BIT):
            self._ddrb = self.t.read_byte(self._addr + 2)
        else:
            # reset DDR_BIT in CRA first:
            self.crb = self._crb & ~DDR_BIT
            self._ddrb = self.t.read_byte(self._addr + 2)
            # restore CRA
            self.crb = self._crb | DDR_BIT
        return self._ddrb
    
    @ddrb.setter
    def ddrb(self, value):
        if not (self._crb & DDR_BIT):
            self._t.write_byte(self._addr + 2, value)
        else:
            # reset DDR_BIT in CRA first:
            self.crb = self._crb & ~DDR_BIT
            self._t.write_byte(self._addr + 2, value)
            # restore CRA
            self.crb = self._crb | DDR_BIT
        self._ddrb = value
