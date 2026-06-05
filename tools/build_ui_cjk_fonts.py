#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["freetype-py>=2.5.1", "fonttools>=4.62.1"]
# ///
"""Regenerate builtin UI fonts with a CJK subset for the zh-CN UI.

The UI fonts (ubuntu_10/12 regular+bold, notosans_8_regular) are compiled into
flash and are Latin/Hebrew-only upstream. This script re-runs the vendored
fontconvert.py with LXGW WenKai appended to the font stack and the exact set
of non-ASCII codepoints used by translations/chinese.yaml as extra intervals —
so the zh UI renders without shipping a full CJK font in flash.

    uv run tools/build_ui_cjk_fonts.py          # regenerate + report sizes

Run after editing chinese.yaml, then rebuild (`run_checks.sh`). Latin sources
come from the upstream checkout (~/w/_hw/crosspoint-reader); the CJK fallback
is fetched by tools/build_cjk_font.py into tools/.font-cache/.
"""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SCRIPTS = REPO / "firmware/crosspoint/vendor/lib/EpdFont/scripts"
OUT_DIR = REPO / "firmware/crosspoint/vendor/lib/EpdFont/builtinFonts"
TRANSLATIONS = REPO / "firmware/crosspoint/vendor/lib/I18n/translations"

UPSTREAM_SRC = Path.home() / "w/_hw/crosspoint-reader/lib/EpdFont/builtinFonts/source"
CACHE = REPO / "tools/.font-cache"
CJK = {"regular": CACHE / "LXGWWenKai-Regular.ttf", "bold": CACHE / "LXGWWenKai-Medium.ttf"}

HEBREW_INTERVAL = "0x05D0,0x05EA"  # matches upstream convert-builtin-fonts.sh

# (font_name, size, [latin stack...], cjk style)
UI_FONTS = [
    ("ubuntu_10_regular", 10, ["Ubuntu/Ubuntu-Regular.ttf", "NotoSansHebrew/NotoSansHebrew-Regular.ttf"], "regular"),
    ("ubuntu_10_bold", 10, ["Ubuntu/Ubuntu-Bold.ttf", "NotoSansHebrew/NotoSansHebrew-Bold.ttf"], "bold"),
    ("ubuntu_12_regular", 12, ["Ubuntu/Ubuntu-Regular.ttf", "NotoSansHebrew/NotoSansHebrew-Regular.ttf"], "regular"),
    ("ubuntu_12_bold", 12, ["Ubuntu/Ubuntu-Bold.ttf", "NotoSansHebrew/NotoSansHebrew-Bold.ttf"], "bold"),
    (
        "notosans_8_regular",
        8,
        ["NotoSans/NotoSans-Regular.ttf", "NotoSansHebrew/NotoSansHebrew-Regular.ttf"],
        "regular",
    ),
]


def translation_codepoints() -> list[int]:
    """Unique non-ASCII codepoints across ALL translation yamls (incl. _language_name).

    Union over every language, not just Chinese: it also catches chars outside
    the upstream hardcoded intervals (e.g. Hebrew gershayim U+05F4, used by
    hebrew.yaml but missing from the 0x05D0-0x05EA interval upstream = tofu).
    The I18nTest.UiFontsCoverAllLanguageCharsets host test enforces this.
    """
    cps: set[int] = set()
    for yaml in sorted(TRANSLATIONS.glob("*.yaml")):
        for line in yaml.read_text(encoding="utf-8").splitlines():
            line = line.strip()
            if not line or line.startswith("#") or ":" not in line:
                continue
            value = line.split(":", 1)[1].strip().strip('"')
            cps.update(ord(c) for c in value if ord(c) >= 0x80)
    if not cps:
        sys.exit(f"no non-ASCII codepoints found under {TRANSLATIONS}")
    return sorted(cps)


def merge_intervals(cps: list[int]) -> list[str]:
    out: list[tuple[int, int]] = []
    for cp in cps:
        if out and cp <= out[-1][1] + 1:
            out[-1] = (out[-1][0], cp)
        else:
            out.append((cp, cp))
    return [f"0x{a:04X},0x{b:04X}" for a, b in out]


def main() -> None:
    for style, path in CJK.items():
        if not path.exists():
            sys.exit(f"missing CJK fallback {path} — run `uv run tools/build_cjk_font.py --fixture` first")
    if not UPSTREAM_SRC.is_dir():
        sys.exit(f"missing upstream font sources at {UPSTREAM_SRC}")

    cps = translation_codepoints()
    intervals = merge_intervals(cps)
    print(f"{len(cps)} unique non-ASCII codepoints -> {len(intervals)} intervals", file=sys.stderr)

    for name, size, latin_stack, cjk_style in UI_FONTS:
        stack = [str(UPSTREAM_SRC / f) for f in latin_stack] + [str(CJK[cjk_style])]
        # 1-bit uncompressed like upstream: --compress requires --2bit, and
        # measured 2bit+DEFLATE came out ~7.5KB LARGER than 1-bit raw at these
        # small UI sizes (tiny glyphs don't amortize the group framing).
        cmd = [
            sys.executable,
            str(SCRIPTS / "fontconvert.py"),
            name,
            str(size),
            *stack,
            "--additional-intervals",
            HEBREW_INTERVAL,
        ]
        for iv in intervals:
            cmd += ["--additional-intervals", iv]
        out_path = OUT_DIR / f"{name}.h"
        result = subprocess.run(cmd, check=True, capture_output=True, text=True)
        out_path.write_text(result.stdout, encoding="utf-8")
        print(f"  {out_path.name}: {len(result.stdout)} bytes of source", file=sys.stderr)

    print("Done. Rebuild firmware to measure the flash delta.", file=sys.stderr)


if __name__ == "__main__":
    main()
