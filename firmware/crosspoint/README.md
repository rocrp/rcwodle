# crosspoint-on-wodle

Port of [CrossPoint Reader](https://github.com/crosspoint-reader/crosspoint-reader)
(ESP32-C3 Xteink X4/X3 e-reader firmware) to the wodle (SF32LB525, RT-Thread,
UC8179C 528x792 EPD). Vendor snapshot + deltas: `vendor/VENDOR.md`. Plan:
`docs/superpowers/plans/2026-06-05-crosspoint-port-plan.md` (repo root docs/).

**Status: blind-port, compiles + links (3.2MB image), zero HIL.** Built entirely
while the device was away — expect bring-up iterations.

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
`/.crosspoint/`. EPUB pagination caches under `/.crosspoint/cache/`.

## Input mapping (port/hal/HalGPIO)

| physical | logical |
|---|---|
| KEY2 (PA43) | DOWN / page fwd |
| KEY3 (PA44) | UP / page back |
| KEY2+KEY3 chord | BACK |
| PWR short (<0.8s) | CONFIRM |
| PWR hold (>~2s) | POWER → sleep (placeholder spin, not real hibernate) |

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
  supply it later. ptab is now the V656 layout (board/wodle/ptab.yaml) —
  device must run the V656 bootloader/ftab (it does since 2026-06-05).
- **Sleep**: deep sleep = placeholder spin loop (HalGPIO::startDeepSleep).
  Real SF32 hibernate + PA34 wake = separate task.
- **Battery**: fixed 100% (BQ27220 on I2C2 @0x55 is the known upgrade).
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
