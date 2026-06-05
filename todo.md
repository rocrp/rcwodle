# wodle TODO

Status snapshot 2026-06-06. Two firmwares: `firmware/hello_wodle/` (validation
instrument, EPD console) + `firmware/crosspoint/` (the e-reader, blind-ported,
compiles + 134/134 host tests, ZERO HIL). Verify both: `firmware/crosspoint/run_checks.sh`.
Flash: 3,410,508 of 3,538,944 B — ~125KB headroom (I18n --strip-unused
reclaimed 82KB; UI-font compression tried + rejected, came out larger).
Next reclaim if needed: GBK table → SD, or drop 8pt/10pt-bold CJK subsets.

## HIL checklist — first session with the device (in order)

1. [ ] **hello_wodle VRES=600**: flash staged build → "2026" renders contiguous
       (mid-screen dead band gone). 1 flash, 30s.
2. [ ] **crosspoint first boot**: flash `firmware/crosspoint/.../main.bin` @0x12218000
       → ~0.3s frontlight pulse (battery must be connected — boost is VBAT-fed)
       → boot splash renders.
3. [ ] **SD mount**: card with `.txt`/`.epub` inserted → home/browser lists files.
       Fail mode: "SD card error" screen; check `msd_init`/`dfs_mount` on console.
4. [ ] **Keys**: PA43=DOWN PA44=UP chord=BACK PWR-short=CONFIRM PWR-hold=sleep.
       Polarity assumed active-low+pullup — if dead/inverted fix `HalGPIO.cpp readRaw`.
5. [ ] **Touch**: boot log `[WodleTouch] CST836U OK`; tap zones (L/R third = page,
       center = confirm, top strip = back). If zones land wrong → `TOUCH_SWAP_XY/
       MIRROR_*` flags in `WodleTouch.cpp`.
6. [ ] **Swipes**: L/R = page turns; U/D = frontlight ±20%, survives reboot
       (`/.crosspoint/frontlight`).
7. [ ] **TXT then EPUB**: open → paginate (first open slow, caches to SD) → page
       turns → progress resumes after reboot.
7b. [ ] **CJK SD font**: copy `dist/sd-fonts/LXGWWenKai/` → SD `/fonts/LXGWWenKai/`
        (regen: `uv run tools/build_cjk_font.py`) → Settings → Font → LXGWWenKai
        → open a Chinese book → renders incl. “”《》……—， punct; bold = Medium;
        watch prewarm latency on page turns (SDCF stats on console).
7c. [ ] **zh-CN UI**: Settings → Language → 简体中文 → menus/settings/status
        bar render in Chinese (UI fonts carry a CJK subset; host no-tofu test
        passed). Judge 10/12pt hanzi legibility at 1-bit — if strokes too thin,
        try --force-autohint on the CJK stack entry in tools/build_ui_cjk_fonts.py.
7d. [ ] **GBK txt**: open a GBK-encoded Chinese .txt → first open transcodes
        to cache (console: "Transcoding ... (GBK) to UTF-8 cache") → correct
        text, reopen skips transcode. Also try a UTF-16LE (Windows Notepad
        "Unicode") txt.
7e. [ ] **TXT chapters**: in a 第X章-style txt, press Confirm → chapter list
        (reader font, so CJK titles render) → select → jumps; reopen uses
        chapters.bin cache.
8. [ ] **DU fast refresh**: page turns use DU LUT (auto-GC every 10th) — judge
       ghosting/quality; tune `FAST_REFRESHES_PER_GC` in `HalDisplay.cpp`.
9. [ ] **Battery**: boot log `[WodleBattery] gauge OK (voltage=...)`; status bar %
       moves. Fail mode: fixed 100% (I2C2 PA31/32 mux or addr issue).
10. [ ] **Hibernate**: PWR-hold → sleep → PA34 press wakes (edge mode). If no wake:
        USB recovery still works; revisit `HalGPIO::startDeepSleep` wake polarity.
11. [ ] **Screenshot combo** PWR+KEY2 → BMP appears on SD.
12. [ ] (optional) UART console signal-integrity: solid short GND wire to WCH-Link,
        single reader (`pgrep minicom` first!), 1M baud should now read clean.

## Blind-able next (no device needed)

- [x] **CJK reading fonts** — DONE 2026-06-06: `tools/build_cjk_font.py` builds
      LXGWWenKai_{12,14,16,18}.cpfont (latin-ext+cjk, regular+Medium-as-bold)
      → `dist/sd-fonts/`; `test/sdcardfont/` host suite (12 tests) proves
      converter↔loader compat over committed CJK fixture; all 4 production
      files validated through the firmware loader. HIL = checklist 7b.
- [x] **zh-CN UI** — DONE 2026-06-06: chinese.yaml (374/374 keys) + I18n regen
      (25 langs, ZH=24) + UI fonts regenerated with CJK subset via
      `tools/build_ui_cjk_fonts.py` (union of non-ASCII chars across ALL
      translations → no-tofu host test for every language; also fixed
      upstream's Hebrew U+05F4 tofu). Flash 3,436,812B of 3,538,944B
      (~100KB headroom). Default stays EN; switch in Settings. HIL = 7c.
- [x] Host test: Epub container/opf parsers — DONE 2026-06-06 (`test/epub/`,
      9 tests: full Epub::load over fixture — opf metadata/spine/toc, item
      streaming, CSS discovery, metadata-cache reload; converter stubs keep
      JPEGDEC/PNGdec out — "deps balloon" concern was obsolete)
- [ ] PSRAM (8MB) heap region for big-EPUB headroom (only if HIL shows pressure)
- [x] BT/BLE assessment — ASSESSED 2026-06-06, verdict: **skip BLE transfer for
      v1; assess USB-MSC post-HIL instead.**
      - Feasible in principle: 52x BLE host stack (sibles) + LCPU boot are SDK
        components; radio HW proven by stock fw (A2DP/PAN). Custom GATT file
        service + bleak Mac script (tools/wodle_ble.py exists) would work.
      - Costs: BLE stack ~100-200KB flash (headroom 125KB → needs reclaim
        first) + LCPU RAM carve-out + pairing UX + power while advertising;
        throughput ~5-20KB/s → 1MB book = minutes.
      - SD-card swap already covers transfer with zero effort. The superior
        upgrade is **USB MSC** (expose SD over the existing USB-C: instant,
        fast, no pairing) — check SDK usbd MSC + sdmmc backing post-HIL.
      - docs/bluetooth.md (stock-era "no BLE control path") is about the stock
        fw; doesn't constrain custom fw.

## Post-HIL backlog

- [ ] CJK filenames/titles in file browser + home recents render with UI fonts
      → tofu for chars outside the translation subset. Chapter list solved
      this by drawing items with the reader font (SD CJK font); apply the
      same to browser/recents lists (needs prewarm batching like
      TxtReaderChapterSelectionActivity::render).

- [ ] X4-style partial window refresh (`displayWindow`) for status-bar updates
- [ ] 4-gray grayscale (refs/epd UC8279_4gray_reference.c) for images/AA text
- [ ] LCDC / hardware-SPI EPD data path (bit-bang is the remaining refresh cost)
- [ ] 18pt/XL font tier via SD `.cpfont` (cut from flash: 550KB over budget)
- [ ] USB detect (VBUS via PMIC/PA41 PWR_INT?) → charging UI + wake reason
- [ ] SF32 on-chip RTC → HalClock (status-bar clock, no NTP without network)
- [ ] Real wakeup-reason (PMU boot cause register) → PowerButton/AfterFlash routing
      + `verifyPowerButtonWakeup` semantics
- [ ] esp_mac shim → real SF32 chip UID read (settings obfuscation key)
- [ ] Frontlight settings UI entry (beyond swipe gestures)
- [ ] hello_wodle parity: DU LUT + direct-register IO (low value — it's a probe fw)
- [ ] Frontlight circuit doc: confirm boost part on schematic sheet 2 (VBAT-fed,
      proven empirically)

## Open issues / notes

- rt_pwm framework `set()` fails through ROM-linked RT layer (direct HAL works;
  cause never found — moot but curious)
- ~~`ZipFile` dangling `const std::string&` path~~ — fixed 2026-06-06 (by-value
  + WODLE-PORT marker; test constructs from a temporary); worth upstreaming
- Upstream sync: vendor snapshot = b12839d1 (2026-06-01); `// WODLE-PORT:` marks
  every local edit; I18n regenerated via gen_i18n.py (upstream committed files stale)
- Repo unpushed (no remote) — decide hosting if/when open-sourcing
