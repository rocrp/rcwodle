# Design: Flash-resident XIP CJK fonts + reader responsiveness

Date: 2026-06-15
Repo: `~/w/_hw/rcwodle` — SiFli SF32LB525 e-reader (crosspoint-reader port)
Status: approved, implementing Phase 1

## Motivation

A sibling firmware (`~/w/_tmp/fengda_wodle`, "crosslink") is a **binary-only** release of
the same crosspoint engine. Strings analysis shows it carries features our snapshot
(`crosspoint-reader@b12839d`) and even upstream (`~/w/_hw/crosspoint-reader`, tags ≤1.3.0)
lack — these are fengda's own port-level additions:

- **Flash Font Storage** (`[FFS]`/`[FM]`, `.epdfont`): user fonts installed into the 4MB
  `FONT_REGION` flash partition, rendered **XIP** (memory-mapped, ~zero RAM).
- **EPUB section prefetch thread + TXT worker thread + `[BMC]` PSRAM metadata cache.**
- Four gorgeous CJK typefaces (思源宋体 / 小米MiSans / 京华老宋体 / 落霞孤鹜文楷).

We cannot copy fengda's source. We **reimplement its best idea with our own tested format.**

## Core decision: capture the architecture, reuse our format

fengda's real win is *"font lives in the flash partition, rendered XIP, ~zero RAM, gorgeous
CJK"* — not its proprietary `.epdfont` glyph encoding. We capture that with crosspoint's own
`.cpfont` v4 (already rendered/kerned/AA'd correctly by our renderer) instead of feeding a
foreign glyph format into a renderer it wasn't built for. The CJK typefaces fengda ships are
all **free TTFs**, so we regenerate equivalent `.cpfont` from source — no reverse-engineering.

### Load-bearing facts (verified)

- `FONT_REGION` = `0x12C00000`, 4MB, NOR flash, **directly memory-mapped XIP-readable**
  (`*(const uint8_t*)0x12C00000`, no HAL call — `port/arduino/pgmspace.h`). Currently unused.
- The render seam is clean: `GfxRenderer::getGlyphBitmap()` returns
  `&fontData->bitmap[glyph->dataOffset]`; `EpdFontData` (`vendor/lib/EpdFont/EpdFontData.h`)
  is all `const*` pointers + `glyphMissHandler` hook. Built-in fonts are *already* XIP
  (const arrays in flash). A flash font = a built-in font stored in a partition.
- `.cpfont` v4 = header(32B) + per-style TOC(32B each) + per-style
  [intervals(12B), glyphs(16B), kern, ligatures, 2-bit bitmaps]. Uncompressed → no decode
  latency, ideal for XIP.
- `fontconvert_sdcard.py` (`crosspoint-reader/lib/EpdFont/scripts/`) generates `.cpfont` from
  TTF with interval presets (incl. `cjk`). Needs `freetype-py` + `fonttools`.
- RT-Thread RTOS; threads via `rt_thread_create` (existing example: `WodleUsbCdc` rx thread).
  PSRAM bump allocator `WodlePsram::alloc()` (8MB @ `0x60000000`). Cooperative main loop
  (`src/main.cpp`) + separate render task (`ActivityManager`, prio 1).
- Flash via `tools/wodle_flash.py write <bin> --addr <addr>` (HVR1 recovery) + `update.json`
  manifest. Build via SCons against `~/w/_hw/SiFli-SDK`.

## Phase 1 — Flash-resident XIP CJK font (flashable deliverable)

### Partition layout (FONT_REGION, 0x12C00000, 4MB)
A minimal FFS: tiny directory header + N concatenated `.cpfont` blobs.

```
offset 0:   FlashFontDir {
              magic   = "WFFD" (u32)
              version = 1      (u32)
              count   = N      (u32)
              reserved
              entries[N] { char name[24]; u8 pointSize; u8 style; u8 _pad[2];
                           u32 offset;  // from FONT_REGION base
                           u32 size; }
            }
align→      .cpfont blob 0
            .cpfont blob 1
            ...
```
Fit **one CJK family at 3 sizes** (S/M/L). Subset glyphs to **GB2312 + CJK punctuation +
Latin/ASCII** (~7k glyphs) so 3 sizes fit <4MB; full 20k CJK won't fit 3 sizes and >99.9% of
real Chinese is GB2312. Rare glyphs fall back to the existing built-in/SD path via the
renderer's replacement-glyph behavior.

### New code (firmware)
- `port/hal/WodleFlashFonts.{h,cpp}` (~150 LOC): `FlashFontStore`
  - `begin()`: validate `WFFD` magic at `0x12C00000`, read directory.
  - `count()`, `entry(i)` → {name, size, style}.
  - `loadFontData(i, EpdFontData& out)`: parse the blob's `.cpfont` header in flash, set
    `out.bitmap/glyph/intervals/kern*` = base+offset (zero-copy XIP), `out.groups=nullptr`,
    `out.glyphMissHandler=nullptr`. Mirrors `SdCardFont` minus all the SD-latency caching
    (prewarm / mini-cache / ring buffer) — flash reads are memory-mapped, so on-demand direct
    indexing is correct and *simpler*.
- Integration: register flash families in `SdCardFontRegistry`/`SdCardFontSystem` so they
  appear in the font-family setting; make the flash CJK family the **default reading font**.
  Reuse `EpdFont`/`EpdFontFamily` wrappers unchanged.

### New tooling (host)
- `tools/build_flash_fonts.py` (PEP-723 standalone, `uv run`): TTF → 3× `.cpfont` (calls
  `fontconvert_sdcard.py` with GB2312-subset intervals) → concatenate with `WFFD` directory →
  `dist/fonts.bin`. Reproducible; typeface swappable by argument.
- Default typeface: **落霞孤鹜文楷 (LXGW WenKai Screen)**, OFL, screen-tuned. Swappable.

### Flash
`wodle_flash.py write dist/fonts.bin --addr 0x12C00000` alongside
`main.bin` @ `0x12218000`. Add a combined `update.json` manifest for one-shot flashing.

### Tests
- New host unit test for `FlashFontStore` against a mocked in-RAM directory+blob buffer
  (parse directory, resolve a glyph offset, confirm zero-copy pointers). Lives in `test/`.
- Existing 210-test suite stays green.

## Phase 2 — Reader responsiveness (after Phase 1 HIL-verified)

EPUB **section-prefetch worker thread** (`rt_thread_create`, low prio; PSRAM scratch via
`WodlePsram::alloc`) that builds the *next* section's cache `.bin` while the user reads the
current page, then signals the UI thread (mutex + flag, integrated in `ActivityManager::loop`).
Scoped conservatively: the worker only **reads** the EPUB + **writes** a cache file (no shared
mutable renderer state), sidestepping the concurrency hazards of a single-threaded engine.
This is the more invasive half — lands second, on its own spec/plan once Phase 1 is proven.

## Verification

1. Host: `cd firmware/crosspoint && ./run_checks.sh` (210 tests + new FlashFontStore test) green.
2. Build: SCons against SiFli-SDK → `main.bin`.
3. Flash `main.bin` + `fonts.bin` to a device in recovery mode (`wodle_flash.py`).
4. HIL: `wodle` MSH cmd + `tools/wodle_console.py` (open a CJK EPUB → dump screen → PNG),
   confirm the flash CJK font renders and RAM headroom improved (no SD font load).

## Out of scope (YAGNI)

- Reverse-engineering `.epdfont`. - Runtime font install UI (fengda's full FFS). - Web flasher.
- Multiple simultaneous flash families. - Real bold (Medium) weight in flash (fake-bold for now).

## As-built (2026-06-15, Phase 1 shipped + HIL-verified)

- **Region**: measurement showed full-GB2312 costs ~1.0/1.4/1.8/2.3 MB at 12/14/16/18 pt — the
  4 MB `FONT_REGION` only fit 2 sizes. The neighbouring `EZIP_REGION` (6.5 MB @ `0x12580000`)
  is referenced *nowhere* in firmware (the `EZIP_*` config hits are the image codec, not the
  partition), so the flash-font area was widened to the contiguous **unused EZIP+FONT = 10.5 MB
  @ `0x12580000`**. No partition-table change: the stock bootloader/ftab own only the HCPU app;
  these data regions are pure XIP-by-address. `FlashFontSystem::kBase/kSize` + the build tool's
  `FLASH_FONT_BASE/SIZE` mirror this.
- **Font**: full GB2312 (7817 glyphs) × 4 sizes (12/14/16/18), `dist/fonts.bin` ≈ 6.6 MB,
  LXGW WenKai Regular (reused from `build_cjk_font.py`'s `gh`-fetched cache). Bold = fake-bold.
- **Deploy** (`build_flash_fonts.py` prints it): the recovery bootloader sees ezip (6.5 MB) +
  font (4 MB) as separate partitions, so the 6.6 MB blob is split at the partition boundary
  (= exactly the font partition start) into `fonts_ezip.bin` (→ `0x12580000`) + `fonts_font.bin`
  (→ `0x12C00000`), two partition-aligned writes that land contiguous in XIP. Flash alongside
  `main.bin` @ `0x12218000`.
- **Verified**: `FlashFontTest` (4) + full host suite 214/214; SCons firmware build OK; flashed
  to the device (every byte CRC-verified by the flasher); CJK renders throughout the reader
  (三国演义 chapter list + body pages) with the flash 霞鹜文楷 as the default reader font.
