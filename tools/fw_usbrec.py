#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone"]
# ///
"""Recover the USB-recovery frame magic/version from dfu_pan.bin.

The recovery service prints '[usb_recovery] FRAME reject ... magic=0x%08x version=%u'
when a frame fails validation. The validation compares the received magic against a
constant just before branching to that printf. We:

  1. linearly disassemble (Thumb-2 / M-class), tracking movw/movt/ldr-literal so we
     can materialize the absolute XIP constants the code builds;
  2. locate the START VA of each recovery format string (code loads the start, not a
     mid-string offset -- the previous version anchored mid-string and found nothing);
  3. for each load site, walk back to the function prologue and dump the disassembly,
     auto-flagging magic candidates (materialized 32-bit consts shown as ASCII + every
     cmp). The magic is the const compared right before the reject branch.

  uv run tools/fw_usbrec.py
"""

import struct

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware254/dfu_pan.bin"
BASE = 0x12008000
data = open(FW, "rb").read()
N = len(data)


def str_start_va(needle: bytes) -> int | None:
    i = data.find(needle)
    if i < 0:
        return None
    while i > 0 and data[i - 1] != 0:
        i -= 1
    return BASE + i


# format strings whose START the code loads for the log call
ANCHORS = {
    "FRAME_reject": str_start_va(b"FRAME reject"),
    "HELLO": str_start_va(b"HELLO seq=%u max_payload"),
    "banner": str_start_va(b"proto=1;max_payload"),
    "CMD_reject": str_start_va(b"CMD reject seq=%u cmd=%u"),
}
print("anchor string VAs:", {k: (hex(v) if v else None) for k, v in ANCHORS.items()})

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
addrs, mnem, ops = [], [], []
built = {}  # insn idx -> 32-bit constant materialized at that insn (in target reg)
reg = {}
pos = 0
while pos < N - 1:
    advanced = False
    for ins in md.disasm(data[pos:], BASE + pos):
        advanced = True
        idx = len(addrs)
        addrs.append(ins.address)
        mnem.append(ins.mnemonic)
        ops.append(ins.op_str)
        m, o = ins.mnemonic, ins.op_str
        parts = o.replace(",", " ").split()
        if m == "ldr" and "[pc" in o:
            # ldr rX, [pc, #imm]
            off = o.split("#")[-1].rstrip("]")
            try:
                fo = (((ins.address + 4) & ~3) + int(off, 0)) - BASE
            except ValueError:
                fo = -1
            if 0 <= fo <= N - 4:
                w = struct.unpack_from("<I", data, fo)[0]
                reg[parts[0]] = w
                built[idx] = w
        elif m.startswith("movw") and "#" in o:
            reg[parts[0]] = int(o.split("#")[-1], 0) & 0xFFFF
            built[idx] = reg[parts[0]]
        elif m.startswith("movt") and "#" in o:
            d = parts[0]
            if d in reg:
                reg[d] = (reg[d] & 0xFFFF) | ((int(o.split("#")[-1], 0) & 0xFFFF) << 16)
                built[idx] = reg[d]
        elif m == "mov" and "#" in o:
            reg[parts[0]] = int(o.split("#")[-1], 0) & 0xFFFFFFFF
            built[idx] = reg[parts[0]]
        pos2 = ins.address - BASE + ins.size
        if pos2 <= pos:
            break
        pos = pos2
    if not advanced:
        pos += 2
        reg.clear()
print(f"disassembled {len(addrs)} insns; {len(built)} materialized constants\n")

va2idx = {a: i for i, a in enumerate(addrs)}


def asc(v: int) -> str:
    return "".join(chr(x) if 32 <= x < 127 else "." for x in struct.pack("<I", v))


def magic_like(v: int) -> bool:
    b = struct.pack("<I", v)
    printable = sum(32 <= x < 127 for x in b)
    return printable >= 3 and v > 0xFFFF


def dump(label: str, va: int):
    sites = [i for i, w in built.items() if w == va]
    print(f"\n{'=' * 78}\n# {label}  string@0x{va:08x}  -> {len(sites)} load site(s)")
    for s in sites:
        # walk back to function prologue (push/push.w {... lr}) within 400 insns
        start = max(0, s - 44)
        for k in range(s, max(0, s - 400), -1):
            if mnem[k].startswith("push") and "lr" in ops[k]:
                start = k
                break
        print(f"\n  ---- load@0x{addrs[s]:08x}  fn-start~0x{addrs[start]:08x} ----")
        for k in range(max(start, s - 60), min(len(addrs), s + 8)):
            line = f"    0x{addrs[k]:08x}  {mnem[k]:8} {ops[k]}"
            note = ""
            if k in built:
                v = built[k]
                if magic_like(v):
                    note = f"   <== const 0x{v:08x} ascii={asc(v)!r}"
                elif v >= 0x10000:
                    note = f"   (0x{v:08x})"
            if mnem[k].startswith("cmp") or mnem[k] in ("cbz", "cbnz", "teq"):
                note += "   <<< COMPARE"
            print(line + note)


for label, va in ANCHORS.items():
    if va:
        dump(label, va)
