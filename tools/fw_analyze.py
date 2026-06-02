#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone"]
# ///
"""Static pin-map recovery for SiFli SF32LB52x HCPU firmware.

Disassembles hcpu_app.bin (Thumb-2, Cortex-M33, XIP base 0x12218000), models r0-r3
immediates per call site, and finds the function whose calls best decode to valid
(pad, func, hcpu) tuples (= HAL_PIN_Set). Decodes each call using the SDK enums.

Fast path: detail=False, parse op_str text. Run:
  uv run tools/fw_analyze.py
"""

import collections
import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
HDR = "/Users/rocry/w/_tmp/SiFli-SDK/drivers/cmsis/sf32lb52x/bf0_pin_const.h"
BASE = 0x12218000


def parse_enums(path):
    txt = open(path).read()
    enums = {}
    for body, name in re.findall(r"typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;", txt, re.S):
        vals, cur = {}, -1
        for line in body.splitlines():
            line = re.sub(r"/\*.*?\*/", "", line).split("//")[0].strip().rstrip(",")
            m = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*([0-9xXa-fA-F]+))?$", line) if line else None
            if not m:
                continue
            cur = int(m.group(2), 0) if m.group(2) else cur + 1
            vals[cur] = m.group(1)
        enums[name] = vals
    return enums


enums = parse_enums(HDR)
PAD = enums["pin_pad"]
FUNC = enums["pin_function"]
PULL = {0: "NOPULL", 16: "PULLDOWN", 48: "PULLUP"}  # PE=0x10, PS=0x20
print(f"enums: pin_pad={len(PAD)} pin_function={len(FUNC)}", file=sys.stderr)

REG = re.compile(r"^(r\d+|sb|sl|fp|ip|sp|lr|pc)$")
BRANCH = frozenset(
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

code = open(FW, "rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False

calls = collections.defaultdict(list)
regs = {}
pos, N, cnt = 0, len(code), 0
while pos < N - 1:
    advanced = False
    for ins in md.disasm(code[pos:], BASE + pos):
        advanced = True
        cnt += 1
        if cnt % 400000 == 0:
            print(f"  ..{cnt} insns @0x{ins.address:08x}", file=sys.stderr)
        m, o = ins.mnemonic, ins.op_str
        if m in ("bl", "blx"):
            if o.startswith("#"):
                try:
                    tgt = int(o[1:], 0)
                    calls[tgt].append((ins.address, regs.get("r0"), regs.get("r1"), regs.get("r2"), regs.get("r3")))
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
            elif p:
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
        elif m not in BRANCH and not m.startswith("str") and not m.startswith("push") and not m.startswith("stm"):
            first = o.split(",", 1)[0].strip()
            if REG.match(first):
                regs.pop(first, None)
        np = ins.address - BASE + ins.size
        if np <= pos:
            break
        pos = np
    if not advanced:
        pos += 2
        regs.clear()

print(f"  total {cnt} insns, {len(calls)} call targets", file=sys.stderr)


def valid(lst):  # how many calls decode to a real (pad PA/PB, func, hcpu in 0/1)
    n = 0
    for _, r0, r1, r2, r3 in lst:
        if r0 in PAD and r1 in FUNC and r3 in (0, 1) and PAD[r0].startswith(("PAD_PA", "PAD_PB")):
            n += 1
    return n


ranked = sorted(calls.items(), key=lambda kv: valid(kv[1]), reverse=True)
print("\n# top targets by valid (pad,func,hcpu) decode rate:")
for tgt, lst in ranked[:8]:
    print(f"  0x{tgt:08x}  calls={len(lst):4d}  valid_pin_decodes={valid(lst):4d}")

# merge all high-confidence pin-setter targets (HAL_PIN_Set + veneers/wrappers)
PINSET = [(t, l) for t, l in calls.items() if valid(l) >= 5 and valid(l) >= 0.4 * len(l)]
print("\n# merging pin-setter targets: " + ", ".join(f"0x{t:08x}({valid(l)}/{len(l)})" for t, l in PINSET))

pins = {}  # pad -> set of (func, pull, cpu)
for t, l in PINSET:
    for addr, r0, r1, r2, r3 in l:
        if r0 in PAD and r1 in FUNC and r3 in (0, 1) and PAD[r0].startswith(("PAD_PA", "PAD_PB")):
            pins.setdefault(PAD[r0][4:], set()).add((FUNC[r1], PULL.get(r2, f"f{r2}"), "HCPU" if r3 else "LCPU"))


def padkey(p):
    return (p[1], int(p[2:]))


print(f"\n# recovered pin map ({len(pins)} pads):")
for pad in sorted(pins, key=padkey):
    for fn, pull, cpu in sorted(pins[pad]):
        print(f"  {pad:5s} {fn:18s} {pull:9s} {cpu}")
