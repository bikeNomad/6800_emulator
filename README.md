This project is a MC6808 emulator for the Raspberry Pi 2350. 
It is intended to replace the MC6808 processor (instruction compatible with the MC6800) as well as the EPROMs in early 1980s Williams and Bally pinball machines.
A portion of the on-chip flash is used for storage of the EPROM code, which is sent to the device via USB.
A portion of the on-chip RAM is used to emulate the internal and external RAM of the target system (typically less than 512 bytes)

The emulator must emulate all the documented MC6800 instructions, and will include a structure that holds at least:
  - the 16-bit PC register (program counter)
  - the 8-bit A and B accumulators
  - the 16-bit SP (stack pointer)
  - the 16-bit X (index) register
  - the 8-bit CCR (condition code) register, which includes these bits:
    - C (carry (from bit 7))
    - V (overflow)
    - Z (zero)
    - N (negative)
    - I (interrupt)
    - H (half-carry (from bit 3))
    - the two top bits are both 1

The code is in several sections:
  - A general-purpose MC6800 emulator, which interfaces through the rest of the system via a simple API that includes:
    - Read byte or bytes from a given memory address (used by the emulator for reading instructions or data)
    - Write byte or bytes to a given memory address (used by the emulator for writing data)
    - Execute next instruction (used by the system to run the program); returns current PC address

  - The code to connect the RP2350's GPIO and PIO peripherals to the target system. These connections include:
    - 8 data lines (bi-directional), plus a R/W output used to drive the level translators between the RP2350 pins and the TTL data bus
    - 16 address lines (outputs), plus a VMA (valid memory address) strobe that is used by peripherals and EPROM to latch addresses
    - An E clock, running at 3.579545/4 MHz, output to one pin and used to synchronize the processor execution.
    - /IRQ, /NMI, and /RESET inputs, which force the PC (Program Counter) to specific vectors

The emulator MUST execute each of the MC6808 instructions within 1.12µs (the period of the E clock).

The PIO and DMA peripherals MAY be used to speed bus operations, which include:
    - Generation of the E clock
    - Driving the address lines, VMA output, R/W output (high), and output to the data bus for writing bytes
    - Driving the address lines, VMA output, R/W output (low), and input from the data bus for reading bytes
    
The USB interface presents as a CDC device to the host PC for loading EPROM contents into RP2350 flash, as well as for interactive diagnostics.
These diagnostics include:
  - Read memory
  - Write memory 
  - Checksum memory range

For debugging, a clocked serial data stream is available via a hardware SPI port that outputs for every instruction:
    - current PC value (16 bits)
    - R/W status (1 bit)
    - data bus value (8 bits)

The code will be written to use the Raspberry Pi Pico SDK and TinyUSB for the USB support.
PICO_SDK_PATH is at /Users/ned/src/Micropython/micropython/lib/pico-sdk
TinyUSB is at /Users/ned/src/Micropython/micropython/lib/tinyusb/src/portable/raspberrypi/rp2040