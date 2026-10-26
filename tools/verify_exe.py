#!/usr/bin/env python3
"""Offline check that a LEGOHobbit.exe matches every assumption Auto Perfect Build
makes, without running the game. Read-only.

usage: python verify_exe.py "C:/.../LEGO - The Hobbit/LEGOHobbit.exe"

Exit code 0 = every patch site matches (the DLL will activate),
         1 = something differs (the DLL will stay inactive on this exe).
Requires: pip install pefile
"""
import hashlib
import struct
import sys

import pefile

EXPECT = dict(stamp=0x543C1BFA, image=0x01E72000, checksum=0x01CCE17D, machine=0x14C,
              sha256="072e25e2c0be8619d92acd4ae14319164d2ee1cdb1425a295ec2e0f6e2f5c998")

# (name, VA of the call/jmp, opcode, VA it must point to) - must match src/game.cpp
SITES = [
    ("IsSelectPressed#P1", 0x00AEB2C3, 0xE8, 0x00AEB1D0),
    ("IsSelectPressed#P2", 0x00AEB337, 0xE8, 0x00AEB1D0),
    ("OnSlotSelected", 0x00B8F49E, 0xE8, 0x00B65C10),
    ("PotLose#WrongPick", 0x00B65CEF, 0xE8, 0x00B4E910),
    ("PotLose#Timer", 0x00B66158, 0xE8, 0x00B4E910),
    ("Complete", 0x00B65D4A, 0xE9, 0x00B4D800),
    ("AnimPlayTo#Advance", 0x00B2F1E4, 0xE8, 0x00757B70),
    ("AnimPlayTo#Resume", 0x00B76561, 0xE8, 0x00757B70),
]
VTABLE_PLAYBUBBLE = 0x012F4EA4          # ??_7InstructionBuildPlayBubble@@6B@
VTABLE_EXPECT = {0: 0x00B651D0, 2: 0x00B8F360, 7: 0x00B88ED0}
RTTI_NAME = b".?AVInstructionBuildPlayBubble@@"


def main(path):
    data = open(path, "rb").read()
    pe = pefile.PE(data=data, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    ok = True

    def check(label, cond, detail=""):
        nonlocal ok
        ok &= bool(cond)
        print(f"[{'PASS' if cond else 'FAIL'}] {label} {detail}")

    sha = hashlib.sha256(data).hexdigest()
    print(f"file: {path}\nsize: {len(data)}  sha256: {sha}")
    check("SHA-256", sha == EXPECT["sha256"], "(informational: header checks below are what the DLL uses)")
    ok = True  # hash is informational only
    check("machine x86", pe.FILE_HEADER.Machine == EXPECT["machine"])
    check("TimeDateStamp", pe.FILE_HEADER.TimeDateStamp == EXPECT["stamp"], hex(pe.FILE_HEADER.TimeDateStamp))
    check("SizeOfImage", pe.OPTIONAL_HEADER.SizeOfImage == EXPECT["image"], hex(pe.OPTIONAL_HEADER.SizeOfImage))
    check("CheckSum", pe.OPTIONAL_HEADER.CheckSum == EXPECT["checksum"], hex(pe.OPTIONAL_HEADER.CheckSum))
    if not ok:
        print("RESULT: NOT supported - different executable (the DX11 exe is 64-bit and not supported)")
        return 1
    for name, va, op, tgt in SITES:
        b = img[va - base: va - base + 5]
        rel = struct.unpack_from("<i", b, 1)[0]
        check(f"site {name}", b[0] == op and va + 5 + rel == tgt, f"@{va:08X}: {b.hex()} -> {va + 5 + rel:08X}")
    for idx, fn in VTABLE_EXPECT.items():
        got = struct.unpack_from("<I", img, VTABLE_PLAYBUBBLE - base + 4 * idx)[0]
        check(f"PlayBubble vtable[{idx}]", got == fn, f"{got:08X}")
    col = struct.unpack_from("<I", img, VTABLE_PLAYBUBBLE - base - 4)[0]
    td = struct.unpack_from("<I", img, col - base + 12)[0]
    name = img[td - base + 8: td - base + 8 + len(RTTI_NAME)]
    check("vtable RTTI name", name == RTTI_NAME, name.decode("latin-1"))
    print("RESULT:", "supported - the mod will activate" if ok else "NOT supported - the mod will stay inactive")
    return 0 if ok else 1


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        sys.exit(2)
    sys.exit(main(sys.argv[1]))
