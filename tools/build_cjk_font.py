#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["fonttools>=4.62.1", "freetype-py>=2.5.1"]
# ///
"""Build CJK .cpfont files (LXGW WenKai) for the crosspoint SD font system.

Drives the vendored fontconvert_sdcard.py (cpfont v4). Font sources are
fetched once via `gh release download` into tools/.font-cache/ (gitignored).

    uv run tools/build_cjk_font.py            # full set -> dist/sd-fonts/LXGWWenKai/
    uv run tools/build_cjk_font.py --fixture  # tiny fixture -> firmware/crosspoint/test/fixtures/

Deploy: copy dist/sd-fonts/LXGWWenKai/ onto the SD card as /fonts/LXGWWenKai/,
then pick it in Settings -> Font. LXGW WenKai is OFL-1.1 (https://github.com/lxgw/LxgwWenKai).
"""

import argparse
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
SCRIPTS = REPO / "firmware/crosspoint/vendor/lib/EpdFont/scripts"
CACHE = REPO / "tools/.font-cache"
DIST = REPO / "dist/sd-fonts/LXGWWenKai"
FIXTURE = REPO / "firmware/crosspoint/test/fixtures/TestCJK_14.cpfont"

RELEASE_TAG = "v1.522"
# Style id -> asset name. LXGW WenKai has no bold/italic; Medium stands in for
# bold, italic falls back to regular in firmware (SdCardFont style fallback).
STYLE_ASSETS = {0: "LXGWWenKai-Regular.ttf", 1: "LXGWWenKai-Medium.ttf"}

sys.path.insert(0, str(SCRIPTS))
import fontconvert_sdcard as fc  # noqa: E402

# Reader sizes map to the FONT_SIZE enum (SMALL..EXTRA_LARGE) by ordinal.
FULL_SIZES = [12, 14, 16, 18]
FULL_INTERVALS = "latin-ext,cjk"

# Fixture: ascii + the exact CJK codepoints asserted in
# firmware/crosspoint/test/sdcardfont/SdCardFontTest.cpp. Keep in sync.
FIXTURE_INTERVALS = (
    "ascii,(0x3001-0x3002),(0x4E16-0x4E16),(0x4E66-0x4E66),"
    "(0x4F60-0x4F60),(0x597D-0x597D),(0x754C-0x754C),(0xFF0C-0xFF0C)"
)


def fetch_fonts() -> dict[int, str]:
    missing = [a for a in STYLE_ASSETS.values() if not (CACHE / a).exists()]
    if missing:
        CACHE.mkdir(parents=True, exist_ok=True)
        cmd = ["gh", "release", "download", RELEASE_TAG, "-R", "lxgw/LxgwWenKai", "-D", str(CACHE), "--skip-existing"]
        for asset in missing:
            cmd += ["-p", asset]
        print(f"Fetching {', '.join(missing)} ...", file=sys.stderr)
        subprocess.run(cmd, check=True)
    return {sid: str(CACHE / asset) for sid, asset in STYLE_ASSETS.items()}


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--fixture", action="store_true", help="build the small host-test fixture instead of the full set"
    )
    args = parser.parse_args()

    style_fonts = fetch_fonts()

    if args.fixture:
        intervals = fc.resolve_intervals(FIXTURE_INTERVALS)
        FIXTURE.parent.mkdir(parents=True, exist_ok=True)
        fc.generate_cpfont_multistyle(style_fonts, 14, intervals, str(FIXTURE))
        return

    intervals = fc.resolve_intervals(FULL_INTERVALS)
    DIST.mkdir(parents=True, exist_ok=True)
    total = 0
    for size in FULL_SIZES:
        out = DIST / f"LXGWWenKai_{size}.cpfont"
        print(f"Generating {out.name} ...", file=sys.stderr)
        total += fc.generate_cpfont_multistyle(style_fonts, size, intervals, str(out))
    print(f"\nDone: {len(FULL_SIZES)} files, {total / 1024 / 1024:.2f} MB -> {DIST}", file=sys.stderr)
    print("Copy onto SD as /fonts/LXGWWenKai/ and select in Settings -> Font.", file=sys.stderr)


if __name__ == "__main__":
    main()
