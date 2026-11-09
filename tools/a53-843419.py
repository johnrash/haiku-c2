#!/usr/bin/env python3
# Find, and optionally fix, Cortex-A53 erratum 843419 sequences in arm64 ELF
# files (the ODROID-C2's S905 has A53 r0p4 cores). An ADRP at page offset
# 0xff8 or 0xffc, a load or store (not a load pair), optionally one more
# non-branch instruction, then a load/store with an unsigned immediate
# offset based on the ADRP's register: the last access may use a wrong
# address. GNU ld avoids these with --fix-cortex-a53-843419 (our cross gcc
# passes it by default); prebuilt binaries linked without it need patching.
# The fix is the linker's "adr" variant: the ADRP becomes an ADR to the same
# page address when that is within +-1 MiB, which breaks the sequence.
# Usage: a53-843419.py [--fix] <elf>...   (exit status 1: unfixed sequences)
import struct
import sys


def segments(data):
    """Sections with code (SHF_EXECINSTR): (file offset, vaddr, size). Not
    whole executable segments: those hold read-only data too, which must
    neither be reported nor patched."""
    if data[:4] != b"\x7fELF" or data[4] != 2 or data[5] != 1:
        return []
    shoff, = struct.unpack_from("<Q", data, 0x28)
    shentsize, shnum = struct.unpack_from("<HH", data, 0x3a)
    result = []
    for i in range(shnum):
        _, shtype, flags, addr, offset, size = struct.unpack_from(
            "<IIQQQQ", data, shoff + i * shentsize)
        if shtype == 1 and flags & 4:
            result.append((offset, addr, size))
    return result


def is_adrp(insn):
    return insn & 0x9f000000 == 0x90000000


def mem_op(insn):
    """(is a load/store, is a pair, is a load), as binutils' aarch64_mem_op_p."""
    if insn & 0x0a000000 != 0x08000000:
        return False, False, False
    pair = insn & 0x3a000000 == 0x28000000
    if pair or insn & 0x3f000000 == 0x08000000 or insn & 0xbf000000 == 0x0c000000:
        load = bool(insn & (1 << 22))
    else:
        load = insn & 0x00c00000 != 0
    return True, pair, load


def ldst_uimm(insn):
    return insn & 0x3b000000 == 0x39000000


def is_branch(insn):
    return (insn & 0x7c000000 == 0x14000000      # b, bl
            or insn & 0xfe000000 == 0x54000000   # b.cond
            or insn & 0x7e000000 == 0x34000000   # cbz, cbnz
            or insn & 0x7e000000 == 0x36000000   # tbz, tbnz
            or insn & 0xfe000000 == 0xd6000000)  # br, blr, ret, eret


def sequence(insn1, insn2, insn3):
    memory, pair, load = mem_op(insn2)
    return (memory and (not pair or not load) and ldst_uimm(insn3)
            and (insn3 >> 5) & 31 == insn1 & 31)


def adr_for(insn, pc):
    """The ADR giving the ADRP's result at pc, or None when out of range."""
    imm = ((insn >> 29) & 3) | (((insn >> 5) & 0x7ffff) << 2)
    if imm & (1 << 20):
        imm -= 1 << 21
    target = (pc & ~0xfff) + (imm << 12)
    offset = target - pc
    if not -(1 << 20) <= offset < (1 << 20):
        return None
    offset &= (1 << 21) - 1
    return (0x10000000 | ((offset & 3) << 29) | ((offset >> 2) << 5)
            | (insn & 31))


def scan(path, fix):
    with open(path, "rb") as f:
        data = bytearray(f.read())
    found = fixed = 0
    for offset, vaddr, size in segments(data):
        start = (vaddr + 0xff8 - (vaddr & 0xfff)) if vaddr & 0xfff > 0xff8 \
            else vaddr - (vaddr & 0xfff) + 0xff8
        for page_end in range(start, vaddr + size - 8, 0x1000):
            for pc in (page_end, page_end + 4):
                at = offset + pc - vaddr
                if at < offset or at + 16 > offset + size:
                    continue
                insn1, insn2, insn3, insn4 = struct.unpack_from("<IIII", data, at)
                if not is_adrp(insn1):
                    continue
                if not (sequence(insn1, insn2, insn3)
                        or (not is_branch(insn3)
                            and sequence(insn1, insn2, insn4))):
                    continue
                found += 1
                adr = adr_for(insn1, pc)
                print("%s: 0x%x: erratum 843419 sequence%s" % (path, pc,
                    "" if adr is not None else " (ADR out of range)"))
                if fix and adr is not None:
                    struct.pack_into("<I", data, at, adr)
                    fixed += 1
    if fixed:
        with open(path, "r+b") as f:
            f.write(data)
    return found, fixed


def main():
    fix = "--fix" in sys.argv[1:]
    unfixed = 0
    for path in [a for a in sys.argv[1:] if a != "--fix"]:
        found, fixed = scan(path, fix)
        unfixed += found - fixed
        if fix and found:
            print("%s: %d fixed, %d left" % (path, fixed, found - fixed))
    sys.exit(1 if unfixed else 0)


if __name__ == "__main__":
    main()
