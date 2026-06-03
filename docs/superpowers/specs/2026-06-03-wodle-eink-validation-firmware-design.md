# wodle eink validation firmware — design spec

Date: 2026-06-03 · Board: `board/wodle/` (SF32LB525UC6, N16R8, app links @ `0x12218000`) · Platform: SiFli-SDK v2.5.0 RT-Thread BSP (`~/.sifli`)

## Intent

Prove our schematic-derived hardware map is correct by booting **our own minimal firmware** and lighting up
the hardware, **simplest-first**, iterating one validated capability at a time. The capstone: render
"hello world" on the **UC8179C** e-paper with **our own** driver — if it paints, the controller ID +
display pins + BUSY + LUTs are all confirmed at once.

## Approach (clean-room, references-only)

- **Our own code + drivers from scratch.** We do **not** build on or modify the xiaozhi-sf32 app or the
  vendor dev package — that avoids inheriting any legacy. Those are **read-only references**:
  - `refs/` (mirror of `~/Downloads/小豆子开发环境包20260530`) — `refs/epd/` (UC8179C cmd set + LUTs),
    `refs/schematic/` (pin map), datasheets.
  - `~/w/_tmp/xiaozhi-sf32` — the recommended app; consulted for *how-to* patterns (SPI-LCD init,
    board bring-up), never linked or copied wholesale.
- **Platform = SiFli-SDK RT-Thread BSP** (the only sane base for this silicon: clocks, PSRAM, flash XIP,
  and the stock-bootloader handoff at `0x12218000` are all handled there; `board/wodle/` already targets
  it and is build-verified). "Our stack" = our minimal app + our drivers on top. No xiaozhi app code, **no
  LVGL** (until/unless a later iteration needs it).
- **Safe:** only the `app`/`ezip`/`font` flash regions are ever written (HVR1 region guard + tf_ota) —
  the bootloader / ftab / dfu_pan are untouchable, so the recovery path can't be bricked.

## Stages — ship one, verify on hardware, then the next

Each stage is independently observable, so a failure **localizes** (boot? bus? driver?).

**S1 — Proof-of-life (the make-or-break).**
Smallest possible app. In `main()`, early: **assert `PWR_EN`=PA10** to hold the power latch *(verify: a
soft-power device may self-off after button release without this)*, then **frontlight PWM `BL_PWM`=PA1
breathe**. Breathes ⇒ a custom SDK-built app boots past the stock bootloader → answers the two open
questions: secure-boot does not gate the app jump, and SDK-v2.5.0/stock-bootloader clock+PSRAM init are
compatible. Also wire `rt_kprintf` → UART1 (PA18/19) for logs **if** a UART is tapped.

**S2 — Log + buses + sensors (validate the I²C/SPI map).**
SD log `/wodle_hello.log` on SPI1 (DIO=PA24 DI=PA25 CLK=PA28 CS=PA29) + `rt_kprintf`. **I²C scan both
buses** → confirm **CST836U @0x15** (I²C1 SCL=PA7/SDA=PA8), **AW32001 @0x49 + BQ27220 @0x55** (I²C2
SCL=PA31/SDA=PA32). Read battery V/SoC; persistent **boot counter** in NOR/KVDB; RTC. Input: **PWRKEY**
PA34 + **touch-INT** PA42 → log + heartbeat flash. Each probe is non-fatal (absence logged, not crash).

**S3 — eink capstone (validate UC8179C + display).**
**Our own UC8179C driver.** Render "hello world" + a couple of diagnostics (battery %, boot count) at
**528×792**, full-frame **GC** (B/W) refresh. Empirically confirms **BUSY (PA2/TE)** and **TRES
(792×528)**. This is the headline "everything we mapped is right" proof.

## Our UC8179C driver (minimal, ours)

- **Transport v1 = GPIO bit-bang SPI** — simplest, total control, mirrors the reference's structure:
  `CS=PA3, CLK=PA4, DIO0(SDA)=PA5, DC=PA6, RST=PA0, BUSY=PA2 (input), BL_PWM=PA1`. (Iterate to the SF32
  **LCDC1 hardware SPI** for speed once it paints — v2.)
- **Command set + init + LUTs** transcribed from `refs/epd/UC8179C_3.68in_528x792_reference.c`: PSR `0x00`,
  power `0x01`, booster `0x06`, VCOM `0x82`, frame-rate `0x30`, **TRES `0x61` programmed 792×528**, RAM
  `0x13` (B/W; add `0x10` for 4-gray later), LUT `0x20`–`0x24` (GC), refresh `0x12`, **BUSY active-low**,
  deep-sleep `0x07`/`0xA5`. Reset active-low ≥10 ms.
- **Our own 1-bpp framebuffer** (528×792/8 ≈ 52 KB) + a tiny text/bitmap blitter (embedded bitmap font or
  a pre-rendered "hello" bitmap). No LVGL, no SDK `LCD_DrvOpsDef` registration in v1.
- **4-level gray deferred** to a later iteration (dual-plane `0x10`+`0x13` + `LUT_Grey`, ref
  `refs/epd/UC8279_4gray_reference.c`).

## `board/wodle/hcpu/board.conf` — correct to schematic truth

Stale DevKit values currently contradict the schematic; fix as part of this work:
`KEY2_PIN 11→43` (+ add `KEY3=44`) · `AW8155_GPIO_PIN 10→11` · `CHARGER_INT_PIN 44→41` · drop `LED1`
(no such net) · drop `LCD_USING_TFT_CO5300` (we drive UC8179C ourselves, not an SDK panel driver) ·
keep `KEY1`/PWRKEY = 34.

## Observability (no soldering required)

Two equipment-free channels: **frontlight heartbeat** (naked-eye proof-of-life) + **SD log** (pull card,
read on Mac). Optional **UART1 PA18/19** tap for live `rt_kprintf`. After S3, the eink itself is the display.

## Flashing (both region-safe)

- **SD `tf_ota`** (proven, primary). New `tools/mk_update.py` regenerates `update.json` (crc32 + size)
  from the built `.bin`; copy `.bin` + `update.json` into card `/firmware/`, power on → self-flash.
- **HVR1 USB recovery** (`tools/wodle_flash.py`) — faster iteration once proven E2E on hardware.

## Architecture (our files, one job each, < ~200 LOC)

| file | responsibility |
|---|---|
| `main.c` | early power-latch + heartbeat, then run the stage's probes/draw, then idle/MSH loop |
| `heartbeat.c` | `BL_PWM` PA1 breathe + button-press flash |
| `selftest.c` | I²C scan, battery/charger, SD info, boot counter, RTC |
| `hello_log.c` | log sink: SD file (if mounted) + `rt_kprintf`, line-buffered/flushed |
| `input.c` | PWRKEY PA34 + touch-INT PA42 handlers |
| `epd_uc8179c.{c,h}` | our UC8179C driver (bit-bang SPI, init, GC LUT, BUSY wait, full refresh) |
| `hello_draw.c` | 1-bpp framebuffer + minimal text/bitmap blit |

## Risks → how settled

- **Secure-boot / SDK-vs-bootloader compat** → S1 (smallest app); breathe-or-not is the verdict.
- **BUSY = PA2/TE is inferred** → S3 confirms; fallback = fixed worst-case delay + UART trace.
- **`PWR_EN` latch behavior** unverified → asserted early in S1; if device still self-offs, revisit.
- **bit-bang refresh slow** (full frame, seconds) → acceptable for hello-world; LCDC-SPI is the v2 speedup.
- **PWM channel/polarity**, **SD-over-SPI1 init** → fallbacks: GPIO toggle; board.conf SPI/mmcsd config.

## Out of scope (v1)

xiaozhi app, LVGL, audio, BT/BT-PAN, cloud, 4G modem, NFC, 4-level gray, hardware-LCDC SPI. Each is a
later, separately-validated iteration.

## Build & test

- Build: `scons --board=wodle -j8` → raw `.bin` linked @ `0x12218000`. Static gate: LOAD segment address
  + entry in range (device-independent, lands now).
- HIL = the staged bring-up above; each stage observable via heartbeat / SD log / eink.
- Pure host helpers (`mk_update.py`) get a quick self-check; no host unit tests for firmware logic in v1.
