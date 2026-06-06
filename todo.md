# wodle TODO

Status snapshot 2026-06-06. Two firmwares: `firmware/hello_wodle/` (validation
instrument, EPD console) + `firmware/crosspoint/` (the e-reader, blind-ported,
compiles + 156/156 host tests, ZERO HIL). Verify both: `firmware/crosspoint/run_checks.sh`.
Flash: 3,418,636 of 3,538,944 B — ~117KB headroom (I18n --strip-unused
reclaimed 82KB; UI-font compression tried + rejected, came out larger).
Next reclaim if needed: GBK table → SD, or drop 8pt/10pt-bold CJK subsets.

## HIL checklist — first session with the device (in order)

> Bring-up aid: **Settings → System → Diagnostics** shows live battery/USB/
> AHT20/touch/SD/heap/wake-reason + last input, auto-refreshing every 2s —
> covers items 4, 5, 9, 9b, 9c without a working UART console.

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
       MIRROR_*` flags in `WodleTouch.cpp`. **Long-press** (stationary >400ms):
       hold center in reader = bookmark, hold top strip ≥1s = go home, hold
       L/R zone = chapter skip (needs Settings → long-press behavior enabled).
6. [ ] **Swipes**: L/R = page turns; U/D = frontlight ±20%, survives reboot
       (`/.crosspoint/frontlight`). Also Settings → Display → Frontlight/阅读灯
       (0-100% in 20% steps, same persisted level).
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
9b. [ ] **AHT20 temp/humidity**: boot log `[WodleAht20] OK (status=0x..)`; reader
        status bar shows e.g. `23°C 45%` left of the progress text (default on).
        Settings → Status Bar → Temperature (Hide/°C/°F) + Humidity rows appear
        only when the sensor responds. Fail mode: `not responding` → addr 0x38
        on I2C2; sanity-check values against a room thermometer.
9c. [ ] **USB charging bolt**: boot log `[WodleBattery] charger OK (id=0x49 ...)`;
        plug USB → lightning bolt in the battery icon within ~1s (unplug →
        gone). Verify a plug/unplug while reading repaints without a page turn.
10. [ ] **Hibernate**: PWR-hold → sleep → PA34 press wakes (edge mode). If no wake:
        USB recovery still works; revisit `HalGPIO::startDeepSleep` wake polarity.
        Also: short TAP wake should drop back to hibernate (anti-pocket-wake,
        unless Settings short-press=sleep); HOLD should boot fully.
11. [ ] **Screenshot combo** PWR+KEY2 → BMP appears on SD.
11b. [ ] **4-gray AA**: Settings → Text Anti-Aliasing ON → page turn runs the
         gray pass (console "ERS Page render ... gray_*") → judge AA text edge
         quality + that untouched pixels don't shift; ghosting after the pass
         (next refresh is forced GC). Knob: no-op LUT banks in HalDisplay.cpp.
12. [ ] (optional) UART console signal-integrity: solid short GND wire to WCH-Link,
        single reader (`pgrep minicom` first!), 1M baud should now read clean.
13. [ ] **USB file transfer (MSC)**: plug USB → Home → File Transfer → device
        restarts into "USB File Transfer" screen → Mac mounts "wodle SD Card"
        (VID 0x38F4 PID 0x1002) → copy a book → eject → unplug (or PWR press)
        → device restarts to home → new book appears in browser. Fail modes:
        no enumeration (MUSB glue/IRQ — console shows [WodleUsbMsc] lines);
        "SD card not ready" (spi_msd reprobe). NOTE: HVR1 recovery is
        unaffected (different boot path, PID 0x1001).

## Blind-able next (no device needed)

- [x] **USB MSC file transfer** — DONE 2026-06-06 (HIL = 13): cherryusb device
      stack (PKG_CHERRYUSB_DEVICE_MUSB_SIFLI + MSC, ~15KB flash) straight from
      SDK example msc/sdcard_disk (SPI_MSD backend, sd0). Mount exclusivity by
      construction: home menu File Transfer (was "requires WiFi" stub) →
      wodleEnterUsbTransfer() silent-restart target → boot path runs
      runUsbTransferMode() BEFORE any SD mount — host owns the FAT alone.
      Exit = unplug (AW32001 PG_STAT, gated on chargerAvailable) or PWR press
      → restart to home. usb_config.h in src/ (CPPPATH-exposed for the pkg
      build); MSC sector IO on cherryusb's own thread (CONFIG_USBDEV_MSC_THREAD).
- [x] **Hardware diagnostics screen** — DONE 2026-06-06: Settings → System →
      Diagnostics (WODLE-PORT DiagnosticsActivity): live gauge/charger/AHT20/
      frontlight/touch/SD/heap/wake-reason/last-input, 2s fast-refresh cadence,
      Confirm = full refresh. Service screen, body intentionally EN-only.
- [x] **AHT20 temp/humidity → status bar** — DONE 2026-06-06: WodleAht20
      driver (I2C2 @0x38, non-blocking trigger/collect, 30s cache) + reader
      status bar readout left of the clock slot (Hide/°C/°F + humidity toggle,
      gated on sensor presence, persisted via SettingsList) + zh/en i18n +
      UI fonts regenerated (温/湿). Host suite test/aht20/ (9 tests: published
      CRC vector, decode math, busy/CRC rejection). HIL = checklist 9b.
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
- [x] PSRAM beachhead — DONE 2026-06-06: WodlePsram (boot probe w/ 64KB-stride
      write/readback + permanent carve-out bump allocator over the dead 8MB
      @0x60000000; SDK inits the controller every boot via BSP_USING_PSRAM).
      First tenant: 4-gray AA planes (2×52KB) now PSRAM-backed w/ SRAM-heap
      fallback; PSRAM line on the Diagnostics screen. Full heap integration
      (rt_memheap) stays deferred ON PURPOSE — 52x kernel is partly
      ROM-linked, config skew risk; revisit only if HIL shows SRAM pressure.
- [x] BT/BLE assessment — ASSESSED 2026-06-06, verdict: **skip BLE transfer for
      v1; assess USB-MSC post-HIL instead.**
      - Feasible in principle: 52x BLE host stack (sibles) + LCPU boot are SDK
        components; radio HW proven by stock fw (A2DP/PAN). Custom GATT file
        service + bleak Mac script (tools/wodle_ble.py exists) would work.
      - Costs: BLE stack ~100-200KB flash (headroom 125KB → needs reclaim
        first) + LCPU RAM carve-out + pairing UX + power while advertising;
        throughput ~5-20KB/s → 1MB book = minutes.
      - SD-card swap already covers transfer with zero effort. The superior
        upgrade is **USB MSC** — since IMPLEMENTED blind 2026-06-06, see the
        USB MSC entry above (HIL = 13).
      - docs/bluetooth.md (stock-era "no BLE control path") is about the stock
        fw; doesn't constrain custom fw.

## Post-HIL backlog

- [x] CJK filenames/titles tofu in browser/recents — DONE 2026-06-06 via
      GfxRenderer::uiFontFor (per-string SD-reading-font fallback when the UI
      font lacks glyphs) applied at BaseTheme/Lyra/Lyra3Covers/RoundedRaff
      list+title sites + browser path bar; verified VISUALLY via the new host
      render harness (test/render/ dumps screens to /tmp/cp_render/*.pgm).
      Caveat for HIL: needs an SD font selected; SD-font rows may look large
      next to UI rows (reader size drives glyph size).

- [ ] X4-style partial window refresh (`displayWindow`) for status-bar updates
      — NOTE: no consumer in vendor snapshot b12839d1 (GfxRenderer::displayWindow
      is commented out upstream); needs feature design + HIL timing data first,
      not just the UC8179 0x90/0x91/0x92 primitive.
- [x] 4-gray grayscale — IMPLEMENTED BLIND 2026-06-06 (HIL judges waveform):
      X3-style differential overlay in HalDisplay (MSB flags→0x10, LSB→0x13,
      overlay grey LUT built from vendor banks: WW←drive-to-dark-grey, WB=
      light-grey, BB/BW=timing-aligned GND no-ops); reader's full-buffer AA
      path (Settings → Text Anti-Aliasing) is now live; gray pass forces next
      refresh to GC (fallback path never calls cleanup). Host: plane invariant
      + 4-level preview test. HIL knobs: no-op banks → all-zero variant if
      untouched pixels shift; AA quality vs 2×52KB heap cost.
- [ ] LCDC / hardware-SPI EPD data path (bit-bang is the remaining refresh cost)
- [x] 18pt/XL font tier via SD `.cpfont` — COVERED 2026-06-06: LXGWWenKai SD
      set ships 12/14/16/18 (XL works once an SD font is selected); Latin
      sets buildable with upstream `build-sd-fonts.py` (16 families in
      sd-fonts.yaml). Builtin-only XL still falls back to 16pt by design.
- [x] USB detect → charging UI — DONE 2026-06-06 (HIL = 9c): AW32001 PG_STAT
      (reg 0x08 bit1, chip-ID-verified probe) behind HalGPIO::isUsbConnected/
      wasUsbStateChanged (1s-cached poll + edge detect in WodleBattery);
      upstream battery-bolt UI + plug/unplug repaint now live. The **wake
      reason** half stays open ON PURPOSE: post-flash boots also have VBUS →
      AfterUSBPower would insta-sleep after every sftool flash; needs a
      HIL-verified reset-cause signature first (see HalGPIO.h comment).
- [ ] SF32 on-chip RTC → HalClock (status-bar clock; 32.768kHz crystal on PA22
      per schematic). Gated on TWO HIL facts: does the RTC domain survive our
      hibernate (PMU LDOs off)? + needs a manual time-set UI (upstream only
      has UTC-offset + NTP — no editor). Don't build the UI before the RTC
      retention answer.
- [x] Real wakeup-reason — IMPLEMENTED BLIND 2026-06-06: getWakeupReason reads
      PMU WSR (PIN0 = PA34 hibernate wake → PowerButton; latched once + WCR
      cleared); verifyPowerButtonWakeup ports the upstream anti-pocket-wake
      hold check (released early / too short → straight back to hibernate).
      HIL: wake with a short tap (default settings) should re-sleep; hold
      should boot. AfterFlash/AfterUSBPower still need USB detect.
- [x] esp_mac shim → real SF32 chip UID — WON'T DO: the bootloader-byte-derived
      key is equally stable per device; swapping to a UID would invalidate
      existing obfuscated settings once for zero functional gain.
- [x] Frontlight settings UI entry — DONE 2026-06-06 (Settings → Display,
      ENUM 0-100% via WodleFrontlight::setPersisted; STR_FRONTLIGHT added to
      en/zh yamls — gen_i18n parser rejects yaml comments, keep them out)
- [x] hello_wodle parity — WON'T DO: it's a validation probe, not a product;
      crosspoint's driver is the maintained one.
- [x] Frontlight circuit doc — CLOSED 2026-06-06: sheet 2 (power tree) was
      never in the vendor package; "VBAT-fed boost, battery required" (proven
      empirically, noted in README/HalDisplay) is all that's documentable.

## Open issues / notes

- rt_pwm framework `set()` fails through ROM-linked RT layer (direct HAL works;
  cause never found — moot but curious)
- ~~`ZipFile` dangling `const std::string&` path~~ — fixed 2026-06-06 (by-value
  + WODLE-PORT marker; test constructs from a temporary); worth upstreaming
- Upstream sync: vendor snapshot = b12839d1 (2026-06-01) + cherry-picked
  bd101b2 (EPUB %-encoded asset paths) + 19d51ec (de STR_INVERTED) on
  2026-06-06; remaining upstream delta (fd5b807 t5s3 fork chore) is N/A.
  `// WODLE-PORT:` marks every local edit; I18n regenerated via gen_i18n.py
  (upstream committed files stale)
- Repo unpushed (no remote) — decide hosting if/when open-sourcing
