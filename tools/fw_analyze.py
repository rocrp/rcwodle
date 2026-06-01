#!/usr/bin/env python3
"""Static pin-map recovery for SiFli SF32LB52x HCPU firmware.

Disassembles hcpu_app.bin (Thumb-2, Cortex-M33, XIP base 0x12218000), finds the
most-called function whose call sites set r0+r1 to immediates (= HAL_PIN_Set
candidate), and decodes each call's (pad, func, flags, hcpu) into signal names
using the SDK's pin_pad / pin_function enums.

Run: uv run --with capstone python tools/fw_analyze.py
"""

import collections
import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from capstone.arm import ARM_INS_BL, ARM_INS_BLX, ARM_INS_MOV, ARM_INS_MOVT, ARM_INS_MOVW, ARM_OP_IMM, ARM_OP_REG

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
HDR = "/Users/rocry/w/_tmp/SiFli-SDK/drivers/cmsis/sf32lb52x/bf0_pin_const.h"
BASE = 0x12218000


# ---- parse the two enums from the header -------------------------------------
def parse_enums(path):
    txt = open(path).read()
    enums = {}
    for body, name in re.findall(r"typedef\s+enum\s*\{(.*?)\}\s*(\w+)\s*;", txt, re.S):
        vals, cur = {}, -1
        for line in body.splitlines():
            line = re.sub(r"/\*.*?\*/", "", line).split("//")[0].strip().rstrip(",")
            if not line:
                continue
            m = re.match(r"([A-Za-z_]\w*)\s*(?:=\s*([0-9xXa-fA-F]+))?$", line)
            if not m:
                continue
            cur = int(m.group(2), 0) if m.group(2) else cur + 1
            vals[cur] = m.group(1)
        enums[name] = vals
    return enums


enums = parse_enums(HDR)
PAD = enums["pin_pad"]  # value -> PAD_xxx
FUNC = enums["pin_function"]  # value -> signal name
PULL = {0: "NOPULL", 1: "PULLUP", 2: "PULLDOWN"}  # PIN_NOPULL/UP/DOWN (verify)
print(f"enums: pin_pad={len(PAD)} pin_function={len(FUNC)}", file=sys.stderr)

# ---- disassemble -------------------------------------------------------------
code = open(FW, "rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = True

calls = collections.defaultdict(list)  # target -> [(addr, r0,r1,r2,r3)]
regs = {}  # reg name -> int|None
pos = 0
N = len(code)
while pos < N - 1:
    advanced = False
    for ins in md.disasm(code[pos:], BASE + pos):
        advanced = True
        ops = ins.operands
        try:
            _, written = ins.regs_access()
        except Exception:
            written = []
        wset = {ins.reg_name(r) for r in written}
        d = None
        if ins.id == ARM_INS_MOV and len(ops) == 2 and ops[0].type == ARM_OP_REG:
            d = ins.reg_name(ops[0].reg)
            if ops[1].type == ARM_OP_IMM:
                regs[d] = ops[1].imm & 0xFFFFFFFF
            elif ops[1].type == ARM_OP_REG:
                regs[d] = regs.get(ins.reg_name(ops[1].reg))
            wset.discard(d)
        elif ins.id == ARM_INS_MOVW and len(ops) == 2 and ops[1].type == ARM_OP_IMM:
            d = ins.reg_name(ops[0].reg)
            regs[d] = ops[1].imm & 0xFFFF
            wset.discard(d)
        elif ins.id == ARM_INS_MOVT and len(ops) == 2 and ops[1].type == ARM_OP_IMM:
            d = ins.reg_name(ops[0].reg)
            regs[d] = None if regs.get(d) is None else (regs[d] & 0xFFFF) | ((ops[1].imm & 0xFFFF) << 16)
            wset.discard(d)
        elif ins.id in (ARM_INS_BL, ARM_INS_BLX) and ops and ops[0].type == ARM_OP_IMM:
            tgt = ops[0].imm
            calls[tgt].append((ins.address, regs.get("r0"), regs.get("r1"), regs.get("r2"), regs.get("r3")))
            for r in ("r0", "r1", "r2", "r3", "r12", "lr"):
                regs[r] = None
            continue
        for r in wset:
            regs[r] = None
        np = ins.address - BASE + ins.size
        if np <= pos:
            break
        pos = np
    if not advanced:
        pos += 2
        regs.clear()


# ---- rank candidates ---------------------------------------------------------
def score(lst):
    return sum(1 for _, a, b, *_ in lst if a is not None and b is not None)


ranked = sorted(calls.items(), key=lambda kv: score(kv[1]), reverse=True)
print("\n# top call targets by (#sites with r0&r1 immediate):")
for tgt, lst in ranked[:12]:
    print(f"  0x{tgt:08x}  total_calls={len(lst):4d}  with_imm_r0r1={score(lst):4d}")

# ---- dump decoded table for the top candidate --------------------------------
if ranked:
    tgt, lst = ranked[0]
    print(f"\n# decoded call sites of top candidate 0x{tgt:08x} (assumed HAL_PIN_Set: pad,func,flags,hcpu)")
    rows = []
    for addr, r0, r1, r2, r3 in lst:
        if r0 is None or r1 is None:
            continue
        pad = PAD.get(r0, f"?{r0}")
        fn = FUNC.get(r1, f"?{r1}")
        pull = PULL.get(r2, f"flags={r2}") if r2 is not None else "?"
        cpu = {1: "HCPU", 0: "LCPU"}.get(r3, f"?{r3}")
        rows.append((pad, fn, pull, cpu, addr))
    # de-dup identical (pad,func) keeping first
    seen = set()
    for pad, fn, pull, cpu, addr in rows:
        key = (pad, fn, cpu)
        if key in seen:
            continue
        seen.add(key)
        print(f"  {pad:10s} {fn:18s} {pull:9s} {cpu:5s}  @0x{addr:08x}")
    print(f"\n# {len(seen)} unique (pad,func,cpu) tuples from {len(lst)} calls")
