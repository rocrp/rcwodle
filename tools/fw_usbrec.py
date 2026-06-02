#!/usr/bin/env python3
"""Recover the USB-recovery frame format (magic + header) from dfu_pan.bin.

dfu_pan.bin is XIP at 0x12008000. We track movw/movt/ldr-literal so we can resolve
the high XIP string pointers, find the function referencing the "FRAME reject ...
magic=... version=..." string, and read the 32-bit magic constant it compares against.

  uv run --with capstone python tools/fw_usbrec.py
"""

import re
import struct

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware254/dfu_pan.bin"
BASE = 0x12008000
data = open(FW, "rb").read()

anchors = {}
for label, needle in (("frame", b"FRAME reject status=BAD_FRAME"), ("hello", b"HELLO seq=%u max_payload")):
    i = data.find(needle)
    if i >= 0:
        anchors[label] = BASE + i
print("anchors:", {k: hex(v) for k, v in anchors.items()})
anchor_vas = set(anchors.values())

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
addrs, mnem, ops = [], [], []
built = {}  # insn idx -> 32-bit constant materialized there
LDRPC = re.compile(r"^(\w+), \[pc, #(0x[0-9a-f]+|\d+)\]$")
IMM = re.compile(r"^(\w+), #(0x[0-9a-fA-F]+|\d+)$")
reg = {}
pos, N = 0, len(data)
while pos < N - 1:
    adv = False
    for ins in md.disasm(data[pos:], BASE + pos):
        adv = True
        idx = len(addrs)
        addrs.append(ins.address)
        mnem.append(ins.mnemonic)
        ops.append(ins.op_str)
        m, o = ins.mnemonic, ins.op_str
        if m == "ldr":
            mm = LDRPC.match(o)
            if mm:
                fo = (((ins.address + 4) & ~3) + int(mm.group(2), 0)) - BASE
                if 0 <= fo <= len(data) - 4:
                    w = struct.unpack_from("<I", data, fo)[0]
                    reg[mm.group(1)] = w
                    built[idx] = w
        elif m.startswith("movw"):
            mm = IMM.match(o)
            if mm:
                reg[mm.group(1)] = int(mm.group(2), 0) & 0xFFFF
                built[idx] = reg[mm.group(1)]
        elif m.startswith("movt"):
            mm = IMM.match(o)
            if mm and mm.group(1) in reg:
                reg[mm.group(1)] = (reg[mm.group(1)] & 0xFFFF) | ((int(mm.group(2), 0) & 0xFFFF) << 16)
                built[idx] = reg[mm.group(1)]
        elif m.startswith("mov"):
            mm = IMM.match(o)
            if mm:
                reg[mm.group(1)] = int(mm.group(2), 0) & 0xFFFFFFFF
                built[idx] = reg[mm.group(1)]
        pos2 = ins.address - BASE + ins.size
        if pos2 <= pos:
            break
        pos = pos2
    if not adv:
        pos += 2
        reg.clear()
print(f"disassembled {len(addrs)} insns; {len(built)} materialized constants")


def asc(v):
    return "".join(chr(x) if 32 <= x < 127 else "." for x in struct.pack("<I", v))


# find sites that materialize an anchor string VA, dump nearby 32-bit constants (magic candidates)
for label, va in anchors.items():
    sites = [i for i, w in built.items() if w == va]
    print(f"\n# {label} @0x{va:08x}: {len(sites)} reference site(s)")
    for s in sites[:3]:
        lo, hi = max(0, s - 100), min(len(addrs), s + 20)
        print(f"  -- around 0x{addrs[s]:08x} --")
        for k in range(lo, hi):
            if k in built and built[k] >= 0x100:
                v = built[k]
                tag = ""
                if 0x20 <= (v & 0xFF) < 0x7F and 0x20 <= ((v >> 24) & 0xFF) < 0x7F:
                    tag = f"  ascii={asc(v)!r}"
                print(f"    0x{addrs[k]:08x} {mnem[k]:7} {ops[k]:24} = 0x{v:08x}{tag}")
            elif mnem[k].startswith("cmp"):
                print(f"    0x{addrs[k]:08x} {mnem[k]:7} {ops[k]}")
