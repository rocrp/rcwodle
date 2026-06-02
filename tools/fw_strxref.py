#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone"]
# ///
"""For each anchor substring, find the enclosing function(s) and dump every other
string literal that function references (via `ldr rX,[pc]` literal pools). Reveals
config-file paths, the keys parsed next to them, and nearby log messages.

Run: uv run tools/fw_strxref.py
"""

import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
BASE = 0x12218000
ANCHORS = [
    "system.cfg",
    "device_config.cfg",
    "network_mode.cfg",
    "reading_state.cfg",
    "enter USB disk mode",
    "[IoT] Received command",
    "eq_debug.bin",
]

data = open(FW, "rb").read()
strs = {BASE + m.start(): m.group().decode("latin1") for m in re.finditer(rb"[ -~]{4,}", data)}
str_vas = set(strs)

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
addrs, mnems, ops = [], [], []
litidx = {}  # insn index -> loaded word
LDRPC = re.compile(r"^\w+, \[pc, #(0x[0-9a-f]+|\d+)\]$")
pos, N = 0, len(data)
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
                fo = (((ins.address + 4) & ~3) + int(m.group(1), 0)) - BASE
                if 0 <= fo <= len(data) - 4:
                    litidx[i] = int.from_bytes(data[fo : fo + 4], "little")
        np = ins.address - BASE + ins.size
        if np <= pos:
            break
        pos = np
    if not adv:
        pos += 2
print(f"disasm {len(addrs)} insns", file=sys.stderr)


def func_range(i):
    s = i
    while s > 0 and not (mnems[s] == "push" and "lr" in ops[s]) and i - s < 1200:
        s -= 1
    e = i
    while (
        e < len(addrs) - 1
        and not ((mnems[e] == "pop" and "pc" in ops[e]) or (mnems[e] == "bx" and ops[e] == "lr"))
        and e - i < 1200
    ):
        e += 1
    return s, e


for anc in ANCHORS:
    avs = {va for va, s in strs.items() if anc in s}
    sites = [i for i, w in litidx.items() if w in avs]
    print(f"\n#### anchor {anc!r}: {len(sites)} xref site(s)")
    funcs = {}
    for i in sites:
        s, e = func_range(i)
        funcs.setdefault((addrs[s], addrs[e]), (s, e))
    for (fa, fe2), (s, e) in sorted(funcs.items()):
        refs = []
        for k in range(s, e + 1):
            if k in litidx and litidx[k] in str_vas:
                t = strs[litidx[k]]
                if 2 <= len(t) <= 80:
                    refs.append(t)
        seen, uniq = set(), []
        for r in refs:
            if r not in seen:
                seen.add(r)
                uniq.append(r)
        print(f"  func 0x{fa:08x}-0x{fe2:08x}  strings referenced:")
        for r in uniq[:40]:
            print(f"      {r!r}")
