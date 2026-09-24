#!/usr/bin/env python3
"""Turn the dis51 listing (alesis_mmt8_v111.asm) into assemblable as31 source.

The listing carries an address and hex column on every line; this strips
them and rewrites the few Intel-syntax forms as31 does not take:
  CSEG AT 0000h -> .org 0x0000     DB/DW -> .db/.dw     END -> .end
  0B5h-style hex literals -> 0x.. (as31 would read "0B" as a binary prefix)

Usage: listing2asm.py alesis_mmt8_v111.asm > firmware/mmt8.asm
"""
import re
import sys

for line in open(sys.argv[1]):
    line = line.rstrip("\n")
    m = re.match(r"^\s*[0-9A-F]{4} [0-9A-F ]{6}\s+(.*)$", line)
    src = m.group(1) if m else line
    src = re.sub(r"^\s*CSEG AT ([0-9A-Fa-f]+)h\s*$", r".org 0x\1", src)
    src = re.sub(r"^(\s*)DB\b", r"\1.db", src)
    src = re.sub(r"^(\s*)DW\b", r"\1.dw", src)
    if src.strip() == "END":
        src = ".end"
    src = re.sub(r"\b0?([0-9A-Fa-f]+)[hH]\b", lambda mm: "0x" + mm.group(1), src)
    print(src)
