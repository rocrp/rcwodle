# CrossPoint Reader → wodle migration plan

**Goal**: port `~/w/_hw/crosspoint-reader` (ESP32-C3 Arduino e-reader fw) to wodle
(SF32LB525, RT-Thread, UC8179C 528x792 EPD). Blind development phase — compile-green
every slice; HIL checkpoints deferred until device returns.

## Why this is tractable

- **Panel twin**: CrossPoint's X3 target = 792x528, 99 B/row, UC8179-family command set
  (0x00/0x01/0x06/0x30/0x61/0xE1, dual-plane 0x10/0x13, LUT banks 0x20-0x24, refresh 0x12)
  — same geometry + controller family as wodle. X3 display path ports almost 1:1,
  including FAST-refresh LUTs + ghosting management. Framebuffer layout identical
  (row-major 99 B x 528 rows; phyY=gate row, phyX=source bit) = our proven epd.c mapping.
- **Clean HAL seam**: 7 singletons (HalDisplay/HalGPIO/HalStorage/HalClock/HalPowerManager/
  HalTiltSensor/HalSystem). App + libs talk to these, not to Arduino directly.
- **lib/ portability** (per survey): PORTABLE x9 (expat uzlib Utf8 JsonParser I18n Memory
  InflateReader XmlParserUtils FsHelpers*), LIGHT x10 (Epub ZipFile Txt Xtc EpdFont Logging
  MiniBidi OpdsParser PngToBmp Serialization*), BOUND x4 (GfxRenderer→HAL only,
  JpegToBmp→JPEGDEC+HAL, KOReaderSync→WiFi(drop), Serialization→esp_mac/mbedtls(trim)).
- **RAM**: code engineered for 380KB; wodle has ~500KB SRAM + 8MB PSRAM. Strict streaming
  design = headroom, not risk.
- `RT_USING_CPLUSPLUS` already on; SCons drives CXX; gnu++2a needed (gcc 14.2 OK).

## Scope

**v1 reading core** (this phase): boot → mount SD → file browser → open EPUB/TXT →
paginate (disk cache) → render pages (builtin fonts, GC refresh first; DU fast-refresh
once X3 LUT path ported) → page turn via KEY2/KEY3 → progress save/resume.

**Deferred**: images in EPUB (JPEGDEC vendor), XTC, SD-card .cpfont fonts, grayscale AA,
themes beyond default, tilt (no IMU... CST836U touch instead, later), i18n packs,
screenshots, dictionary.

**Out (no WiFi on wodle)**: webserver/WebDAV/OPDS/OTA/KOReaderSync/WifiSelection/
FontDownload activities. BT-based transfer = future separate design; book loading via
SD card (user copies files) or HVR1-adjacent tool later.

## Layout (in rcwodle)

```
firmware/crosspoint/
  project/           SCons project (board=wodle, C++20)
  vendor/            snapshot of crosspoint-reader lib/ + src/ subset
                     (origin commit recorded in vendor/VENDOR.md; prune network)
  port/
    arduino/         Arduino.h Print.h Stream.h WString.h HardwareSerial.h shims
    freertos/        FreeRTOS.h semphr.h task.h → rt-thread mapping
    hal/             HalDisplay/HalGPIO/HalStorage/... wodle implementations
  docs in this file + HIL checklist at bottom
```

hello_wodle stays untouched = HIL instrument (EPD console) + known-good reference.

## Slices (compile-green each)

1. **Skeleton**: firmware/crosspoint/project builds empty C++ main under scons
   (CXXFLAGS gnu++2a; verify C++ runtime init/static ctors under RT-Thread).
2. **Shims**: arduino/ + freertos/ headers; Logging→rt_kprintf.
3. **Vendor PORTABLE libs**: uzlib expat Utf8 JsonParser I18n Memory InflateReader
   XmlParserUtils FsHelpers → compile.
4. **HalStorage-wodle**: POSIX/DFS implementation (HalFile over open/read/...).
   SD bring-up: RT-Thread spi(msd)+elm-FAT on SPI1 (PA24/25/28/29, TFDET PA33).
   Fallback bench path: tiny ROMFS w/ test book baked into image.
5. **Vendor LIGHT libs**: ZipFile Epub Txt EpdFont MiniBidi Serialization(trimmed) → compile.
6. **HalDisplay-wodle + GfxRenderer**: wrap hello_wodle epd driver (GC refresh) behind
   HalDisplay API; port X3 fast/partial LUT path as phase 2; framebuffer = malloc'd 52272B.
7. **HalGPIO-wodle**: KEY2=PA43 KEY3=PA44 PWR=PA34 (+pinmux!), debounce, map to
   CrossPoint buttons (UP/DOWN/SELECT/BACK/POWER... per MappedInputManager).
8. **Activity core + main**: ActivityManager (freertos shim), HomeActivity/FileBrowser/
   EpubReaderActivity/TxtReaderActivity + settings/state stores; prune network activities;
   setup()/loop() → RT-Thread main thread.
9. **Stubs**: HalClock (RTC later), HalPowerManager (battery via BQ27220 I2C2 later),
   HalTiltSensor (absent → no-op), HalSystem (panic→rt assert hook).
10. **HIL checklist doc** + flash package.

## Risks / notes

- Vendor bootloader region: app must stay @0x12218000; size budget 3.5MB (V656 ptab) —
  fonts/assets OK in flash via static const (XIP).
- SD over RT-Thread SPI is the biggest blind-bring-up risk (timing/CS). Mitigation:
  ROMFS fallback + heavy logging to EPD console path retained.
- FreeRTOS render-task semantics → rt_thread + rt_mutex + rt_sem; watch priority
  inversion (RT-Thread mutexes have priority inheritance — fine).
- Arduino String: vendor a minimal String impl in shim (only what FsHelpers/light libs use).
- C++ exceptions disabled upstream (-fno-exceptions) — keep; new(std::nothrow) honored.

## HIL checklist (when device returns)

1. hello_wodle VRES=600 build already staged → verify '2026' renders contiguous.
2. crosspoint fw: boot logo/home renders.
3. SD mount + file browser lists card contents.
4. Open .txt → pages render, KEY2/KEY3 turn pages.
5. Open .epub → spine parses, pagination caches to SD, progress resume.
6. Fast refresh (X3 LUT port) quality vs GC.
7. Frontlight (carried-over open issue: GPT PWM verified-OK but no light; circuit-side?).
