#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["fonttools>=4.62.1", "freetype-py>=2.5.1"]
# ///
"""Build the flash-resident font blob (``dist/fonts.bin``) for the wodle FONT area.

Companion to build_cjk_font.py (which targets the SD-card font system). This builds
a single XIP blob flashed into the device's unused NOR-flash partitions and rendered
straight from memory-mapped flash by the firmware's FlashFontSystem — the whole font
lives in flash, costing ~zero glyph-bitmap RAM (vs SD fonts, which copy into RAM).

Coverage is GB2312 (6763 hanzi + symbols + Latin/punctuation) rather than the SD
path's full CJK, so all four reader sizes fit the flash budget; rare/traditional
characters fall back to the built-in/SD font. Reuses build_cjk_font's font fetch
(LXGW WenKai via `gh`, cached in tools/.font-cache/) and the vendored fontconvert.

    uv run tools/build_flash_fonts.py            # -> dist/fonts.bin (sizes 12,14,16,18)
    uv run tools/build_flash_fonts.py --sizes 14,16

Flash it next to the firmware:
    uv run tools/wodle_flash.py write dist/fonts.bin --addr 0x12580000

fonts.bin layout (offsets are FONT-area-relative; mirrored by FlashFontSystem):
    Header (16 B):  magic "WFFD" | u32 version=1 | u32 count | u32 reserved=0
    Entry  (40 B) x count:  char name[24] | u8 size | u8 style | u8 pad[2]
                            u32 offset | u32 length | u32 reserved=0
    -> pad to 32 B, then each .cpfont blob (each 32 B aligned)
"""

from __future__ import annotations

import argparse
import struct
import sys
import tempfile
from pathlib import Path

import build_cjk_font as bcf  # reuses fetch_fonts() + the vendored fontconvert (bcf.fc)

fc = bcf.fc

# Flash-font area = the firmware-unused EZIP_REGION (6.5 MB @ 0x12580000) + FONT_REGION
# (4 MB @ 0x12C00000), claimed as one 10.5 MB XIP region. The stock bootloader/ftab own
# only the HCPU app; these data partitions are read by direct address, so merging the two
# unused partitions needs no partition-table change. Mirrored by FlashFontSystem::kBase/kSize.
FLASH_FONT_BASE = 0x12580000
FLASH_FONT_SIZE = 0x00A80000  # 10.5 MB
# The two underlying partitions the recovery bootloader reports (split deploy below).
EZIP_BASE = 0x12580000
EZIP_SIZE = 0x00680000  # 6.5 MB; EZIP_BASE + EZIP_SIZE == FONT_BASE (contiguous in XIP)
FONT_BASE = 0x12C00000
DIST = bcf.REPO / "dist/fonts.bin"

WFFD_MAGIC = b"WFFD"
WFFD_VERSION = 1
HEADER_SIZE = 16
ENTRY_SIZE = 40
BLOB_ALIGN = 32

DEFAULT_NAME = "霞鹜文楷"  # must match CROSSPOINT_DEFAULT_FLASH_FONT in CrossPointSettings.h


def gb2312_codepoints() -> set[int]:
    """Unicode codepoints reachable through GB2312 (6763 hanzi + 682 symbols)."""
    cps: set[int] = set()
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                cps.add(ord(bytes((lead, trail)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return cps


# Ranges every reader needs regardless of the hanzi subset.
EXTRA_RANGES: list[tuple[int, int]] = [
    (0x0020, 0x007E),  # ASCII
    (0x00A0, 0x00FF),  # Latin-1 supplement (©, °, é …)
    (0x2010, 0x2027),  # general punctuation: dashes, quotes, ellipsis
    (0x2030, 0x205E),  # ‰, ′, ※, dagger …
    (0x3000, 0x303F),  # CJK symbols & punctuation (。、《》「」…)
    (0xFF00, 0xFFEF),  # fullwidth forms
]


def coalesce(cps: set[int]) -> list[tuple[int, int]]:
    """Merge a codepoint set into sorted (first,last) intervals for fontconvert."""
    s = sorted(cps)
    out: list[tuple[int, int]] = []
    start = prev = s[0]
    for c in s[1:]:
        if c == prev + 1:
            prev = c
        else:
            out.append((start, prev))
            start = prev = c
    out.append((start, prev))
    return out


def align_up(n: int, a: int) -> int:
    return (n + a - 1) & ~(a - 1)


def build_entry(name: str, size: int, style: int, offset: int, length: int) -> bytes:
    name_b = name.encode("utf-8")
    if len(name_b) > 24:
        raise SystemExit(f"name {name!r} is {len(name_b)} UTF-8 bytes (max 24)")
    return struct.pack("<24sBBxxIII", name_b.ljust(24, b"\0"), size, style, offset, length, 0)


def main() -> None:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--name", default=DEFAULT_NAME, help="Family name in the font menu (<=24 UTF-8 bytes).")
    ap.add_argument("--sizes", default="12,14,16,18", help="Comma-separated point sizes (150 DPI).")
    ap.add_argument(
        "--budget",
        type=lambda s: int(s, 0),
        default=FLASH_FONT_SIZE - 64 * 1024,
        help="Max fonts.bin size (default: region - 64KB slack).",
    )
    ap.add_argument("-o", "--output", default=str(DIST), help="Output path.")
    args = ap.parse_args()

    sizes = [int(s) for s in args.sizes.split(",")]
    regular_ttf = bcf.fetch_fonts()[0]  # regular weight only — flash bold uses fake-bold
    intervals = coalesce(set().union(*(set(range(a, b + 1)) for a, b in EXTRA_RANGES)) | gb2312_codepoints())
    total_cps = sum(b - a + 1 for a, b in intervals)
    print(f"GB2312 coverage: {total_cps} codepoints in {len(intervals)} intervals", file=sys.stderr)

    tmpdir = Path(tempfile.mkdtemp(prefix="wodleflash_"))
    blobs: list[tuple[int, bytes]] = []
    for size in sizes:
        out = tmpdir / f"font_{size}.cpfont"
        print(f"\n=== rasterising {args.name} {size}pt ===", file=sys.stderr)
        fc.generate_cpfont_multistyle({0: regular_ttf}, size, intervals, str(out))
        blobs.append((size, out.read_bytes()))

    def total_for(selected: list[tuple[int, bytes]]) -> int:
        off = align_up(HEADER_SIZE + ENTRY_SIZE * len(selected), BLOB_ALIGN)
        for _, b in selected:
            off = align_up(off + len(b), BLOB_ALIGN)
        return off

    selected = list(blobs)
    while selected and total_for(selected) > args.budget:
        dropped = selected.pop()  # largest (last) size first
        print(f"!! {args.name} {dropped[0]}pt dropped to fit budget", file=sys.stderr)
    if not selected:
        raise SystemExit("no font size fits the budget")

    # Lay out directory + 32 B-aligned blobs.
    count = len(selected)
    body_base = align_up(HEADER_SIZE + ENTRY_SIZE * count, BLOB_ALIGN)
    entries = bytearray()
    body = bytearray()
    offset = body_base
    for size, blob in selected:
        entries += build_entry(args.name, size, 0, offset, len(blob))
        body += b"\0" * (offset - body_base - len(body))  # pad up to this blob's offset
        body += blob
        offset = align_up(offset + len(blob), BLOB_ALIGN)
    body += b"\0" * (offset - body_base - len(body))

    header = struct.pack("<4sIII", WFFD_MAGIC, WFFD_VERSION, count, 0)
    out_bytes = header + entries + b"\0" * (body_base - HEADER_SIZE - len(entries)) + body
    assert len(out_bytes) == offset, (len(out_bytes), offset)

    outp = Path(args.output)
    outp.parent.mkdir(parents=True, exist_ok=True)
    outp.write_bytes(out_bytes)

    print(f"\n=== {outp} ===", file=sys.stderr)
    print(f"  family '{args.name}', sizes {[s for s, _ in selected]} (regular)", file=sys.stderr)
    print(
        f"  {len(out_bytes)} bytes ({len(out_bytes) / 1024 / 1024:.2f} MB) of "
        f"{FLASH_FONT_SIZE / 1024 / 1024:.1f} MB region",
        file=sys.stderr,
    )

    # The recovery bootloader knows ezip (0x12580000, 6.5 MB) and font (0x12c00000,
    # 4 MB) as separate partitions; a single cross-boundary write is unverified. When
    # the blob exceeds the ezip partition, emit two partition-aligned pieces (the split
    # point is exactly the font partition start, so they land contiguous in XIP) and
    # print the safe two-write deploy.
    if len(out_bytes) > EZIP_SIZE:
        p1, p2 = out_bytes[:EZIP_SIZE], out_bytes[EZIP_SIZE:]
        f1, f2 = outp.with_name("fonts_ezip.bin"), outp.with_name("fonts_font.bin")
        f1.write_bytes(p1)
        f2.write_bytes(p2)
        print(f"  spans ezip+font; split -> {f1.name} ({len(p1)} B) + {f2.name} ({len(p2)} B)", file=sys.stderr)
        print("  flash (after main.bin):", file=sys.stderr)
        print(f"    uv run tools/wodle_flash.py write {f1} --addr 0x{EZIP_BASE:08X} --no-reboot -y", file=sys.stderr)
        print(f"    uv run tools/wodle_flash.py write {f2} --addr 0x{FONT_BASE:08X} -y", file=sys.stderr)
    else:
        print(f"  flash: uv run tools/wodle_flash.py write {outp} --addr 0x{FLASH_FONT_BASE:08X}", file=sys.stderr)


if __name__ == "__main__":
    main()
