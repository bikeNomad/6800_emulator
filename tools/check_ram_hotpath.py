#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
#
# Copyright 2026 Ned Konz <ned@metamagix.tech>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
"""
Check that the emulator's hot path never touches XIP flash.

Starting from one or more root functions, walk the static call graph (direct
`bl`/`b` branches) and report:

  * any reachable function that lives in flash (0x10000000-0x1fffffff)
  * any literal-pool word in a reachable function that points into flash
    (e.g. a const table or string left in .rodata)

Indirect calls (function pointers, `blx rN`) cannot be followed statically and
are listed so they can be checked by hand.

Usage:
    tools/check_ram_hotpath.py build_ned_sys7/mc6800_emulator.elf [root ...]

Default root is `emu_core_run`. Exit status is 1 if anything flash-resident
is reachable.
"""

import re
import subprocess
import sys
from collections import defaultdict

OBJDUMP = "arm-none-eabi-objdump"
FLASH_LO, FLASH_HI = 0x10000000, 0x20000000

FUNC_RE = re.compile(r"^([0-9a-f]+) <([^>]+)>:$")
BRANCH_RE = re.compile(r"\s(?:bl|b|b\.w|b\.n|bl\.w)\s+([0-9a-f]+)\s+<([^>+]+)")
CONDBR_RE = re.compile(r"\sb[a-z]{2}(?:\.[nw])?\s+([0-9a-f]+)\s+<([^>+]+)")
WORD_RE = re.compile(r"\.word\s+0x([0-9a-f]+)")
INDIRECT_RE = re.compile(r"\s(blx|bx)\s+(r\d+|ip|lr)\b")


def flash_ranges(elf):
	"""Address ranges of allocated sections that live in flash."""
	out = subprocess.run([OBJDUMP, "-h", elf], check=True, capture_output=True,
			     text=True).stdout.splitlines()
	ranges = []
	for i, line in enumerate(out):
		parts = line.split()
		if len(parts) >= 6 and parts[0].isdigit():
			size, vma = int(parts[2], 16), int(parts[3], 16)
			flags = out[i + 1] if i + 1 < len(out) else ""
			if "ALLOC" in flags and FLASH_LO <= vma < FLASH_HI and size:
				ranges.append((vma, vma + size))
	return ranges


def disassemble(elf):
	out = subprocess.run([OBJDUMP, "-d", "-z", elf], check=True, capture_output=True,
			     text=True).stdout
	funcs = {}
	body = defaultdict(list)
	cur = None
	for line in out.splitlines():
		m = FUNC_RE.match(line)
		if m:
			cur = m.group(2)
			funcs[cur] = int(m.group(1), 16)
			continue
		if cur and line.strip():
			body[cur].append(line)
	return funcs, body


def symbol_for(addr, funcs_sorted):
	best = None
	for a, name in funcs_sorted:
		if a <= addr:
			best = name
		else:
			break
	return best


def main():
	if len(sys.argv) < 2:
		print(__doc__)
		return 2
	elf = sys.argv[1]
	roots = sys.argv[2:] or ["emu_core_run"]
	funcs, body = disassemble(elf)
	funcs_sorted = sorted((a, n) for n, a in funcs.items())

	missing = [r for r in roots if r not in funcs]
	if missing:
		print("Root function(s) not found:", ", ".join(missing))
		return 2

	in_flash = lambda name: FLASH_LO <= funcs.get(name, 0) < FLASH_HI
	franges = flash_ranges(elf)
	# Words that merely look like flash addresses (e.g. TBB/TBH jump tables
	# that objdump shows as .word) are ignored unless they fall inside an
	# allocated flash section.
	points_into_flash = lambda v: any(lo <= v < hi for lo, hi in franges)
	addr_to_func = {a & ~1: n for n, a in funcs.items()}
	seen, stack = set(), list(roots)
	flash_calls = defaultdict(set)
	flash_literals = defaultdict(set)
	indirect = defaultdict(int)
	flash_funcs = set()

	while stack:
		fn = stack.pop()
		if fn in seen:
			continue
		seen.add(fn)
		if in_flash(fn):
			flash_funcs.add(fn)
		for line in body[fn]:
			for rx in (BRANCH_RE, CONDBR_RE):
				m = rx.search(line)
				if m:
					target = m.group(2)
					if target == fn:
						continue
					if in_flash(target):
						flash_calls[fn].add(target)
					stack.append(target)
			m = WORD_RE.search(line)
			if m:
				v = int(m.group(1), 16)
				if points_into_flash(v):
					target = addr_to_func.get(v & ~1)
					if fn.endswith("_veneer") and target:
						# Long-branch veneer: treat as a call to its target
						flash_calls[fn].add(target)
						stack.append(target)
					else:
						flash_literals[fn].add((v, symbol_for(v & ~1, funcs_sorted)))
			if INDIRECT_RE.search(line) and "bx\tlr" not in line:
				indirect[fn] += 1

	ok = True
	print(f"Checked {len(seen)} functions reachable from: {', '.join(roots)}")
	if flash_funcs:
		ok = False
		print("\nREACHABLE FUNCTIONS IN FLASH:")
		for fn in sorted(flash_funcs):
			print(f"  {fn}")
	if flash_calls:
		ok = False
		print("\nFLASH-RESIDENT CALLEES:")
		for caller, callees in sorted(flash_calls.items()):
			print(f"  {caller} -> {', '.join(sorted(callees))}")
	if flash_literals:
		ok = False
		print("\nLITERALS POINTING INTO FLASH (const data, strings, or function addresses):")
		for fn, lits in sorted(flash_literals.items()):
			for v, sym in sorted(lits):
				print(f"  {fn}: 0x{v:08x} (near {sym})")
	if indirect:
		print("\nINDIRECT CALLS (check by hand):")
		for fn, n in sorted(indirect.items()):
			print(f"  {fn}: {n}")
	print("\nOK: hot path is RAM-only" if ok else "\nFAIL: hot path touches flash")
	return 0 if ok else 1


if __name__ == "__main__":
	sys.exit(main())
