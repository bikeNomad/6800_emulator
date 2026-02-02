<!--
SPDX-License-Identifier: MIT

Copyright 2026 Ned Konz <ned@metamagix.tech>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
-->

# Limitations and Bugs

## No CMOS RAM persistence

Originally I wanted to persist the CMOS RAM contents in the board’s flash. However, this version doesn’t do this. CMOS RAM is treated just like any other RAM: it’s copied from the bus to the RAM shadow at startup time and read from/written to the shadow during operation. This ensures that the target system CMOS remains unchanged while emulating. It also means that any changes you make during an emulation session will be lost at the next power cycle.

## Early Bally/Stern may run slowly

I don’t have one of these games available to test with. But because all the RAM and both PIAs live in the region from $00-$FF, the emulator treats this entire range as “unmapped”, reading from and writing to the bus directly. This is a consequence of handling the memory map in 256-byte pages; if I went to 128-byte pages I could shadow the RAM region.

## `map clear, map program `currently untested

## Documentation out of date and incorrect

Much of the documentation was written by an LLM early in the development cycle. I haven’t gotten back to update it since. Between not updating the docs and the docs having been written by an LLM, there are likely going to be a number of inconsistencies or outright errors. Beware.