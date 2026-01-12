# Limitations and Bugs

## No CMOS RAM persistence

Originally I wanted to persist the CMOS RAM contents in the board’s flash. However, this version doesn’t do this. CMOS RAM is treated just like any other RAM: it’s copied from the bus to the RAM shadow at startup time and read from/written to the shadow during operation. This ensures that the target system CMOS remains unchanged while emulating. It also means that any changes you make during an emulation session will be lost at the next power cycle.

## Early Bally/Stern may run slowly

I don’t have one of these games available to test with. But because all the RAM and both PIAs live in the region from $00-$FF, the emulator treats this entire range as “unmapped”, reading from and writing to the bus directly. This is a consequence of handling the memory map in 256-byte pages; if I went to 128-byte pages I could shadow the RAM region.

## `map clear, map program `currently untested

## Documentation out of date and incorrect

Much of the documentation was written by an LLM early in the development cycle. I haven’t gotten back to update it since. Between not updating the docs and the docs having been written by an LLM, there are likely going to be a number of inconsistencies or outright errors. Beware.