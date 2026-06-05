# crosspoint-on-wodle

Port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
(ESP32-C3 Xteink X4/X3 e-reader firmware) to the wodle (SF32LB525, RT-Thread,
UC8179C 528x792 EPD). Vendor snapshot + deltas: `vendor/VENDOR.md`. Plan:
`docs/superpowers/plans/2026-06-05-crosspoint-port-plan.md` (repo root docs/).

**Status: blind-port, compiles + links (3.2MB image), zero HIL.** Built entirely
while the device was away — expect bring-up iterations.

## Verify (blind-development loop)

```sh
firmware/crosspoint/run_checks.sh        # target build + 102 host tests
```

Host tests (gtest, `test/`): port shims with known-answer vectors (MD5/base64/
String/TapClassifier) + upstream JSON parser suites + ZipFile/inflate over a
real EPUB fixture + SdCardFont over a real `.cpfont` CJK fixture, all via
POSIX host shims.

## Build + flash

```sh
source ~/w/_hw/SiFli-SDK/export.sh
cd firmware/crosspoint/project
scons --board=wodle -j8
# device → recovery mode (stock Settings → USB recovery, or the user's path)
uv run tools/wodle_flash.py write firmware/crosspoint/project/build_wodle_hcpu/output/main.bin \
    --addr 0x12218000 --port /dev/cu.usbmodemHVR_RECOVERY1 -y
```

Restore stock: flash `~/Downloads/firmware_656/hcpu_app.bin` the same way.

## SD card prep

FAT32 card. Books anywhere (e.g. `/books/*.epub`, `.txt`). App state lands in
`/.crosspoint/`. EPUB pagination caches under `/.crosspoint/cache/`. Extra
reading fonts go in `/fonts/<Family>/<Family>_<size>.cpfont` (see below).

## CJK reading fonts (LXGW WenKai)

Builtin fonts are Latin-only — Chinese books need an SD font:

```sh
uv run tools/build_cjk_font.py     # → dist/sd-fonts/LXGWWenKai/ (4 sizes, ~40MB)
# copy dist/sd-fonts/LXGWWenKai/ to the card as /fonts/LXGWWenKai/
# on device: Settings → Font → LXGWWenKai
```

Regular + Medium-as-bold (LXGW WenKai has no italic; firmware style fallback
covers it), `latin-ext,cjk` intervals = 22.7k glyphs incl. fullwidth punct,
CJK quotes/brackets, ellipsis, em-dash. Font sources cached in
`tools/.font-cache/` via `gh release download`. The `sdcardfont` host suite
proves converter↔loader format compat over `test/fixtures/TestCJK_14.cpfont`
(regen: `uv run tools/build_cjk_font.py --fixture`); validate any generated
file with the firmware loader via
`CPFONT_EXTRA=/path/to/X.cpfont /tmp/cp_test/sdcardfont/SdCardFontTest`.

## Input mapping (port/hal/HalGPIO)

| physical | logical |
|---|---|
| KEY2 (PA43) | DOWN / page fwd |
| KEY3 (PA44) | UP / page back |
| KEY2+KEY3 chord | BACK |
| PWR short (<0.8s) | CONFIRM |
| PWR hold (>~2s) | POWER → hibernate |
| tap left/right third | page back / page forward |
| tap center | CONFIRM |
| tap top strip | BACK |
| swipe left / right | page forward / back |
| swipe up / down | frontlight brighter / dimmer (20% steps, persisted) |

Polarity assumed active-low w/ pullups — **HIL checkpoint #1 if input is dead/inverted.**

## HIL checklist (in order)

1. **Boot**: ~0.3s frontlight pulse almost immediately (proof-of-life;
   battery must be connected — boost is VBAT-fed), then splash (BootActivity).
   - If no pulse and no splash: UART boot fingerprint; check SD inserted (SD
     fail → "SD card error" screen should still render).
2. **SD mount**: home shows file browser/recents. `msd_init`/`dfs_mount` logs
   on console. SPI1 @ default speed; if mount flaky, drop spi_msd max_hz.
3. **Keys**: navigation per the table above.
4. **TXT open** → pages render → page turns.
5. **EPUB open** → first-open pagination (slow, GC-refresh-only progress
   popups) → reading → progress saved (`/.crosspoint/`) → resume after reboot.
6. **Screenshot combo** (PWR+KEY2) writes BMP to SD.

## Known gaps / stubs (by design, blind phase)

- **Refresh**: DU fast-refresh (vendor LUT) for FAST_REFRESH with auto-GC
  every 10th update; GC for FULL/HALF. Data write = direct-register bit-bang
  (DOSR/DOCR). HIL checkpoint: DU quality/ghosting on this panel; LCDC/hw-SPI
  data path is the remaining speed upgrade.
- **Fonts**: 12/14/16 NotoSerif+NotoSans (all styles) + UI fonts in flash;
  the 18pt (XL) tier alone is ~550KB and overflows the 3.5MB region — omitted
  (WODLE_OMIT_18PT in src/main.cpp). XL size falls back; SD `.cpfont` can
  supply it later (LXGWWenKai set ships all 4 sizes incl. 18). ptab is now the
  V656 layout (board/wodle/ptab.yaml) — device must run the V656
  bootloader/ftab (it does since 2026-06-05). UI strings render with builtin
  (Latin-only) fonts — zh-CN *UI* would need a CJK subset font in flash +
  a chinese.yaml (upstream has neither); book text is covered via SD fonts.
- **Sleep**: real SF32 hibernate (PMU, per SDK example/pm/classical 52x
  recipe) with PA34 edge wake (polarity-robust). Wake = chip reset → normal
  boot (quick-resume restores the frame from SD). HIL: verify it actually
  wakes; if not, USB recovery still works.
- **Battery**: live BQ27220 SOC over I2C2 (PA31/32); falls back to 100% if
  the gauge doesn't respond.
- **Clock**: none (status bar clock hidden).
- **USB detect / tilt / grayscale / images-in-epub dithering**: stubbed or
  best-effort; JPEGDEC/PNGdec are linked but image rendering is untested.
- **No WiFi features**: transfer/OPDS/KOReader-sync/OTA menus pruned or show
  "requires WiFi" message.
- **esp_mac shim**: settings obfuscation key derived from bootloader bytes,
  not a true chip UID.

## Layout

- `port/arduino/` — Arduino core shims (String, Print/Stream, Serial→rt_kputs,
  millis/delay, base64/MD5/esp_* shims)
- `port/freertos/` — FreeRTOS façade → rt_thread/rt_mutex/rt_sem
- `port/hal/` — wodle Hal{Display,GPIO,Storage,Clock,PowerManager,TiltSensor,System}
- `vendor/` — upstream snapshot (commit in VENDOR.md), `// WODLE-PORT:` marks edits
- `src/main.cpp` — upstream main, WiFi/OTA pruned, RT-Thread entry
