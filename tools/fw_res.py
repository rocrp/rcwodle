#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone"]
# ///
"""Find display resolution by reading the immediate args of create-style calls
(e.g. lv_display_create(hor,ver), LCDC config). Reports bl sites where r0 & r1
are both resolution-like integers, grouped by target.

Run: uv run tools/fw_res.py
"""

import collections
import re

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
BASE = 0x12218000
LO, HI = 80, 1100  # plausible pixel dimension range
data = open(FW, "rb").read()

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
REG = re.compile(r"^(r\d+|sb|sl|fp|ip|sp|lr|pc)$")
BR = frozenset(
    [
        "b",
        "bx",
        "beq",
        "bne",
        "bcs",
        "bhs",
        "bcc",
        "blo",
        "bmi",
        "bpl",
        "bvs",
        "bvc",
        "bhi",
        "bls",
        "bge",
        "blt",
        "bgt",
        "ble",
        "bal",
        "cbz",
        "cbnz",
        "cmp",
        "cmn",
        "tst",
        "teq",
    ]
)
calls = collections.defaultdict(list)
regs = {}
pos, N = 0, len(data)
while pos < N - 1:
    adv = False
    for ins in md.disasm(data[pos:], BASE + pos):
        adv = True
        m, o = ins.mnemonic, ins.op_str
        if m in ("bl", "blx"):
            if o.startswith("#"):
                try:
                    calls[int(o[1:], 0)].append((regs.get("r0"), regs.get("r1"), regs.get("r2"), ins.address))
                except ValueError:
                    pass
            for r in ("r0", "r1", "r2", "r3", "r12", "ip", "lr"):
                regs.pop(r, None)
        elif m.startswith("movt"):
            p = [x.strip() for x in o.split(",")]
            if len(p) == 2 and p[1].startswith("#") and p[0] in regs:
                try:
                    regs[p[0]] = (regs[p[0]] & 0xFFFF) | ((int(p[1][1:], 0) & 0xFFFF) << 16)
                except ValueError:
                    regs.pop(p[0], None)
        elif m.startswith("movw"):
            p = [x.strip() for x in o.split(",")]
            if len(p) == 2 and p[1].startswith("#"):
                try:
                    regs[p[0]] = int(p[1][1:], 0) & 0xFFFF
                except ValueError:
                    regs.pop(p[0], None)
        elif m.startswith("mov"):
            p = [x.strip() for x in o.split(",")]
            if len(p) == 2 and REG.match(p[0]):
                if p[1].startswith("#"):
                    try:
                        regs[p[0]] = int(p[1][1:], 0) & 0xFFFFFFFF
                    except ValueError:
                        regs.pop(p[0], None)
                elif REG.match(p[1]) and p[1] in regs:
                    regs[p[0]] = regs[p[1]]
                else:
                    regs.pop(p[0], None)
            elif p and REG.match(p[0]):
                regs.pop(p[0], None)
        elif m not in BR and not m.startswith("str") and not m.startswith("push") and not m.startswith("stm"):
            f = o.split(",", 1)[0].strip()
            if REG.match(f):
                regs.pop(f, None)
        np = ins.address - BASE + ins.size
        if np <= pos:
            break
        pos = np
    if not adv:
        pos += 2

# targets with resolution-like (r0,r1)
print("# bl targets whose call sites pass two resolution-like ints (r0,r1 in [%d,%d]):" % (LO, HI))
hits = []
for tgt, lst in calls.items():
    pairs = [(a, b, c, ad) for a, b, c, ad in lst if a and b and LO <= a <= HI and LO <= b <= HI]
    if pairs:
        hits.append((tgt, lst, pairs))
hits.sort(key=lambda x: len(x[2]), reverse=True)
for tgt, lst, pairs in hits[:15]:
    uniq = collections.Counter((a, b) for a, b, c, ad in pairs)
    print(
        f"  0x{tgt:08x}  total_calls={len(lst):4d}  reslike_sites={len(pairs):3d}  pairs(w,h)x cnt: {uniq.most_common(6)}"
    )
    for a, b, c, ad in pairs[:3]:
        print(f"        @0x{ad:08x}  r0={a} r1={b} r2={c}")
