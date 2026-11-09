#!/usr/bin/env python3
# Find Cortex-A53 erratum 835769 candidates in arm64 ELF files, as GCC's
# -mfix-cortex-a53-835769 sees them: a load/store directly followed by a
# 64-bit multiply-accumulate (madd/msub/smaddl/smsubl/umaddl/umsubl,
# Ra != xzr) that does not use the load's destination register. Code built
# with the flag has none; hand-written assembly and prebuilt binaries can.
# Unlike 843419 there is no in-place fix: the sequence needs a NOP between.
# Usage: a53-835769.py <elf>...   (prints "<count> <file>"; exit status 1 if
# any file has sequences)
import importlib.util
import os
import struct
import sys

spec = importlib.util.spec_from_file_location(
    "e", os.path.join(os.path.dirname(os.path.abspath(__file__)), "a53-843419.py"))
e = importlib.util.module_from_spec(spec)
spec.loader.exec_module(e)

found = False
for path in sys.argv[1:]:
    with open(path, "rb") as f:
        data = f.read()
    n = 0
    for off, vaddr, size in e.segments(data):
        words = struct.unpack_from("<%dI" % (size // 4), data, off)
        for a, b in zip(words, words[1:]):
            memory, pair, load = e.mem_op(a)
            if not memory or b & 0xff000000 != 0x9b000000:
                continue
            if (b >> 21) & 7 not in (0, 1, 5) or (b >> 10) & 31 == 31:
                continue
            sources = {(b >> 5) & 31, (b >> 16) & 31, (b >> 10) & 31}
            vector = bool(a & (1 << 26))
            if load and not vector:
                dests = {a & 31} | ({(a >> 10) & 31} if pair else set())
                if dests & sources:
                    continue
            n += 1
    if n:
        print(n, path)
        found = True
sys.exit(1 if found else 0)
