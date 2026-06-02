#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["capstone", "pyelftools"]
# ///
"""FLIRT-lite: locate functions from a symboled reference ELF inside the stripped
stock firmware by masked byte-signature matching.

For each FUNC symbol in the reference ELF we read its Thumb bytes, wildcard the
position-dependent instructions (branches + pc-relative loads), and search the
stock image for that pattern. A unique match => that symbol's address in the
stock firmware. Especially useful to pin HAL_PIN_Set and rt_pin_attach_irq.

Usage:
  uv run tools/fw_symmatch.py <ref.elf> [name1 name2 ...]
If no names given, matches a default watchlist + any FUNC symbol >= 24 bytes.
"""

import re
import sys

from capstone import CS_ARCH_ARM, CS_MODE_MCLASS, CS_MODE_THUMB, Cs
from elftools.elf.elffile import ELFFile

STOCK = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
BASE = 0x12218000
WATCH = [
    "HAL_PIN_Set",
    "HAL_PIN_SetMode",
    "rt_pin_attach_irq",
    "rt_pin_mode",
    "rt_pin_write",
    "rt_pin_read",
    "rt_pin_detach_irq",
    "rt_pin_irq_enable",
    "HAL_I2C_Master_Transmit",
    "rt_i2c_transfer",
    "BSP_PIN_Init",
]

if len(sys.argv) < 2:
    sys.exit("need ref ELF path")
elf_path = sys.argv[1]
names = sys.argv[2:]
stock = open(STOCK, "rb").read()
md = Cs(CS_ARCH_ARM, CS_MODE_THUMB | CS_MODE_MCLASS)
md.detail = False
BR = re.compile(r"^(b|bl|blx|bx|beq|bne|bcs|bhs|bcc|blo|bmi|bpl|bvs|bvc|bhi|bls|bge|blt|bgt|ble|bal|cbz|cbnz|adr)")


def mask_sig(code, addr):
    """Return (pattern_bytes, mask) wildcarding branch / pc-relative instructions."""
    pat = bytearray(code)
    msk = bytearray(b"\x01" * len(code))  # 1 = must match, 0 = wildcard
    off = 0
    for ins in md.disasm(code, addr):
        i = ins.address - addr
        wild = BR.match(ins.mnemonic) or "pc" in ins.op_str or ins.mnemonic.startswith("ldr") and "[pc" in ins.op_str
        if wild:
            for k in range(i, i + ins.size):
                msk[k] = 0
        off = i + ins.size
    return bytes(pat[:off]), bytes(msk[:off])


def to_regex(pat, msk):
    out = bytearray()
    for b, m in zip(pat, msk):
        if m:
            out += re.escape(bytes([b]))
        else:
            out += b"."
    return bytes(out)


with open(elf_path, "rb") as f:
    elf = ELFFile(f)
    symtab = elf.get_section_by_name(".symtab")
    funcs = {}
    for s in symtab.iter_symbols():
        if s["st_info"]["type"] == "STT_FUNC" and s["st_size"] >= 16:
            funcs[s.name] = (s["st_value"], s["st_size"])
    # read .text bytes by vaddr
    sects = [
        (sec["sh_addr"], sec["sh_addr"] + sec["sh_size"], sec.data())
        for sec in elf.iter_sections()
        if sec["sh_flags"] & 0x4 and sec.data()
    ]

    def read(va, n):
        for lo, hi, d in sects:
            if lo <= va and va + n <= hi:
                return d[va - lo : va - lo + n]
        return None


target = names or list(funcs)
print(f"ref ELF: {len(funcs)} FUNC symbols >=16B; matching {len(target)} names", file=sys.stderr)
for name in WATCH if not names else target:
    if name not in funcs:
        print(f"  {name:28s} : (not in reference build)")
        continue
    va, sz = funcs[name]
    code = read(va & ~1, sz)
    if not code:
        print(f"  {name:28s} : (no bytes)")
        continue
    pat, msk = mask_sig(code, va & ~1)
    rx = re.compile(to_regex(pat, msk), re.DOTALL)
    hits = [BASE + m.start() for m in rx.finditer(stock)]
    kept = sum(msk)
    tag = "UNIQUE" if len(hits) == 1 else ("multi" if hits else "none")
    locs = " ".join(f"0x{h:08x}" for h in hits[:6])
    print(f"  {name:28s} : {tag:6s} sz={sz:4d} kept={kept:3d}  {locs}")
