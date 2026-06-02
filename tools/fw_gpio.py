#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone"]
# ///
"""Infer GPIO control-line roles: for each driver (by anchor string), find its
function and report immediates that equal an unassigned GPIO pad index. For
HCPU, RT-Thread pin number == PA index, so immediate N => PA<N>.

Validation anchors: EPD backlight should show 1 (PA01), button should show 34 (PA34).

Run: uv run tools/fw_gpio.py
"""

import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
BASE = 0x12218000
# pads that came out as plain GPIO (unassigned) + known ones for validation
GPIO_SET = {0, 1, 10, 21, 33, 34, 38, 42, 43, 44}
ANCHORS = {
    "EPD backlight": "EPD backlight",
    "EPD busy/init": "EPD busy",
    "EPD partial": "EPD partial",
    "touch cst816": "cst816",
    "charger aw32001": "AW32001",
    "button/home": "home talk",
}
data = open(FW, "rb").read()
strs = {BASE + m.start(): m.group().decode("latin1") for m in re.finditer(rb"[ -~]{4,}", data)}
str_vas = set(strs)

md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
addrs, mnems, ops = [], [], []
litidx = {}
imms = {}  # idx -> int immediate (mov/movs/movw)
LDRPC = re.compile(r"^\w+, \[pc, #(0x[0-9a-f]+|\d+)\]$")
IMM = re.compile(r"#(0x[0-9a-fA-F]+|\d+)$")
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
        elif ins.mnemonic.startswith(("movs", "mov", "movw")) and not ins.mnemonic.startswith("movt"):
            mm = IMM.search(ins.op_str)
            if mm and "," in ins.op_str:
                imms[i] = int(mm.group(1), 0)
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


for label, anc in ANCHORS.items():
    avs = {va for va, s in strs.items() if anc in s}
    sites = [i for i, w in litidx.items() if w in avs]
    franges = {}
    for i in sites:
        s, e = func_range(i)
        franges[(s, e)] = (addrs[s], addrs[e])
    found = {}
    for (s, e), (fa, fb) in franges.items():
        for k in range(s, e + 1):
            if k in imms and imms[k] in GPIO_SET:
                found.setdefault(imms[k], 0)
                found[imms[k]] += 1
    pins = sorted(found)
    print(
        f"\n{label:18s} ({len(sites)} sites) -> PA pins referenced: "
        + ", ".join(f"PA{p:02d}(x{found[p]})" for p in pins)
        if pins
        else f"\n{label:18s} ({len(sites)} sites) -> none in set"
    )
