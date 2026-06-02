#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone"]
# ///
"""Cross-reference chip drivers to their I2C bus + slave address in hcpu_app.bin.

The image is XIP-mapped 1:1 at BASE, so VA = BASE + file_offset. We resolve
`ldr rX, [pc, #imm]` literals to find which functions reference each chip-name
string and each bus-name string ("i2c1"/"i2c2"/...), then report co-located
buses and candidate 7-bit I2C addresses (immediates 0x08-0x77).

Run: uv run tools/fw_xref.py
"""

import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
BASE = 0x12218000
data = open(FW, "rb").read()
END = BASE + len(data)

# string table: start-VA -> string
strs = {BASE + m.start(): m.group().decode("latin1") for m in re.finditer(rb"[ -~]{3,}", data)}
bus_va = {va: s for va, s in strs.items() if s in ("i2c1", "i2c2", "i2c3", "i2c4", "spi1", "spi2", "spi3")}
CHIPS = ("cst816", "aw32001", "bq27220", "aw8155")

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
addrs, mnems, ops = [], [], []
litword = {}  # idx -> 32-bit word loaded by ldr [pc]
pos, N = 0, len(data)
LDRPC = re.compile(r"^\w+, \[pc, #(0x[0-9a-f]+|\d+)\]$")
while pos < N - 1:
    adv = False
    for ins in md.disasm(data[pos:], BASE + pos):
        adv = True
        i = len(addrs)
        addrs.append(ins.address)
        mnems.append(ins.mnemonic)
        ops.append(ins.op_str)
        if ins.mnemonic == "ldr":
            m = LDRPC.match(ins.op_str)
            if m:
                lit = ((ins.address + 4) & ~3) + int(m.group(1), 0)
                fo = lit - BASE
                if 0 <= fo <= len(data) - 4:
                    litword[i] = int.from_bytes(data[fo : fo + 4], "little")
        np = ins.address - BASE + ins.size
        if np <= pos:
            break
        pos = np
    if not adv:
        pos += 2
print(f"disassembled {len(addrs)} insns, {len(litword)} pc-literals", file=sys.stderr)

idx_by_addr = {a: i for i, a in enumerate(addrs)}


def func_range(i):
    s = i
    while s > 0 and not (mnems[s] == "push" and "lr" in ops[s]):
        s -= 1
        if i - s > 1500:
            break
    e = i
    while e < len(addrs) - 1 and not ((mnems[e] == "pop" and "pc" in ops[e]) or (mnems[e] == "bx" and ops[e] == "lr")):
        e += 1
        if e - i > 1500:
            break
    return s, e


def imm_of(o):
    m = re.search(r"#(0x[0-9a-fA-F]+|\d+)$", o)
    return int(m.group(1), 0) if m else None


for chip in CHIPS:
    anchor_vas = {va for va, s in strs.items() if chip in s.lower()}
    sites = [i for i, w in litword.items() if w in anchor_vas]
    print(f"\n=== {chip}: {len(sites)} string-xref site(s) ===")
    buses, addrcands, funcs = set(), set(), set()
    for i in sites:
        s, e = func_range(i)
        funcs.add((addrs[s], addrs[e]))
        for k in range(s, e + 1):
            if k in litword and litword[k] in bus_va:
                buses.add(bus_va[litword[k]])
            if mnems[k] in ("mov", "movs", "movw"):
                v = imm_of(ops[k])
                if v is not None and 0x08 <= v <= 0x77:
                    addrcands.add(hex(v))
    for fs, fe in sorted(funcs):
        print(f"  func 0x{fs:08x}-0x{fe:08x}")
    print(f"  buses referenced: {sorted(buses) or '(none found in func scope)'}")
    print(f"  candidate 7-bit I2C addrs in scope: {sorted(addrcands)}")
