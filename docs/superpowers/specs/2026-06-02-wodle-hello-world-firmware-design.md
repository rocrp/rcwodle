# wodle "hello world" firmware — design spec

Date: 2026-06-02 · Board: `board/wodle/` (SF32LB52x N16R8, app links @ `0x12218000`) · SDK: SiFli-SDK v2.5.0 (`~/.sifli`)

## Goal

A custom RT-Thread firmware that boots on the wodle and **exercises most on-board components**, observable
**without soldering** (no UART, no working display in v1) and **safely** (cannot brick the recovery path).
Proves the toolchain → flash → boot → driver chain end-to-end, and becomes the base for the real framework.

## Scope

**In v1** (confirmed pins + SDK drivers exist): frontlight PWM, both I²C buses, fuel gauge (BQ27220) +
charger (AW32001), capacitive touch (CST816), microSD (SPI1), buttons (KEY1/PA34), RTC, persistent boot
counter (NOR/KVDB), optional audio beep (AW8155 + codec).

**Deferred to v2:** the e-ink display. Controller is unidentified (vendor module named "st7789" actually
drives an EPD over LCDC1 2-lane SPI; SDK only ships an 8-bit-parallel `epd_opm060da` driver — almost
certainly not a match). v2 = identify the controller live over SWD (`lcd_rreg`) and/or RE the init
sequence + waveform LUT + BUSY/DC/RST pins (among unknown PA21/33/38/42/43) from the stock app, then write
the driver. Needs the PA18/19 SWD tap.

**Explicitly out:** WiFi (device has none — BT only), XiaoZhi cloud, BT-PAN.

## Observability strategy (the core problem: no UART, no screen in v1)

Two independent channels, neither needing equipment:

1. **Frontlight heartbeat (PA01 PWM) — started FIRST in `main()`, before any other peripheral.**
   Slow breathe = "booted, main loop alive," visible to the naked eye. If a later probe hangs, you still
   see how far it got. Button press → bright flash (proves input is live). This is the immediate
   no-equipment proof-of-life. Worst case PA01 is driven as a GPIO toggle instead of PWM.

2. **microSD boot log `/wodle_hello.log` (SPI1) — the rich channel.** Each probe appends a line; pull the
   card, read on Mac. Same lines also go to `rt_kprintf`, so they appear on console for free the moment a
   UART/SWD tap exists. Log is append + flush-per-line (survives an ungraceful reset).

Detailed status lives in the log; the frontlight stays simple (alive-heartbeat + button feedback). No
elaborate blink codes (YAGNI).

## Architecture

`board/wodle/` (already build-verified; links @ `0x12218000`) + a small app. One job per file, < ~200 LOC:

| file | responsibility | key deps |
|---|---|---|
| `main.c` | orchestrate: start heartbeat → run probes → log results → interactive loop | rtthread |
| `heartbeat.c` | frontlight PWM breathe + status pulse + button-flash | rt pwm device / hwtimer |
| `probe.c` | I²C scan (both buses), battery V/SoC/current (BQ27220@0x55), charger (AW32001@0x49), touch ID (CST816@0x15), SD info, boot-count r/w | rt_i2c, mmcsd, fal/kvdb |
| `hello_log.c` | log sink: SD file (if mounted) + `rt_kprintf`; line-buffered | dfs, rt_kprintf |
| `input.c` | KEY1/PA34 + touch event handlers → log + heartbeat reaction | button middleware, CST816 |

Reuses SDK drivers throughout: PWM, `rt_i2c`, mmcsd-over-SPI1, `button` middleware
(`middleware/include/button.h`), CST816 (`customer/peripherals/cst816/`), charger/gauge. KEY2/PA11 and
LED1/PA26 are **unverified** → probed, never fatal (absence logged, not a failure).

## Component probe matrix

| component | bus/pin | probe | pass signal |
|---|---|---|---|
| frontlight | PA01 PWM | breathe | visible glow/pulse |
| KEY1 | PA34 (active-high) | button middleware press/long | log event + frontlight flash |
| touch CST816 | I²C1 PA07/08 @0x15 | read chip ID; report touch x/y | log coords on contact |
| fuel gauge BQ27220 | I²C2 PA31/32 @0x55 | read voltage/SoC/current | values in log |
| charger AW32001 | I²C2 @0x49 | read status reg; VBUS det PA44 | status in log |
| microSD | SPI1 PA24/25/28/29 | mount FAT, write log, stat free | log file exists |
| RTC | on-chip | read; set if unset (default 2026-01-01) | timestamp in log |
| boot counter | NOR via FAL/KVDB | read+increment+persist | count increments across reboots |
| audio (opt) | AW8155 EN PA10 + codec | short beep on boot/button | audible |
| I²C scan | both buses | enumerate ACKing addrs | address list in log |

## Flashing

Build artifact: `scons --board=wodle -j8` → one raw `.bin` linked @ `0x12218000`.

**HVR1 USB recovery can write the app region** — confirmed from `dfu_pan.bin` HELLO region table
(`0x12121fe0`): writable regions are `app` `0x12218000–0x12578000`, `ezip` `0x12580000–0x12c00000`,
`font` `0x12c00000–0x13000000`. Bootloader / `ftab` / `dfu_pan` are **outside** these (DENIED guard) →
this loop **cannot brick the recovery path**.

**Channel A — USB cable (daily driver).** Enter recovery: power off → hold recovery button (GPIO-43;
exact physical button TBD by experiment) → plug USB → enumerates `/dev/cu.usbmodem*`. Then:
```
python tools/wodle_flash.py hello                                # link + device info
python tools/wodle_flash.py write <app>.bin --addr 0x12218000    # BEGIN→ERASE→WRITE→VERIFY→COMMIT→REBOOT
```
Protocol + region access cracked; CLI frame-format unit-verified. End-to-end on hardware = the one
unproven step; the button experiment + `hello` validate it in <1 min.

**Channel B — SD `tf_ota` (proven fallback).** Copy `.bin` + version-bumped `update.json` (correct
crc32+size) into card `/firmware/`, insert, power on → bootloader verifies + flashes + reboots. New tool
**`tools/mk_update.py`** regenerates `update.json` from the built `.bin` (one command, no manual CRC).

## Bring-up sequence (also settles the secboot question empirically)

0. **Button experiment** (30 s, zero risk): power off, hold a button, plug USB, `ls /dev/cu.usbmodem*` →
   identifies the recovery trigger; confirms Channel A.
1. **Flash official 0422** via A (or B) → proves the flash loop + a fresh image boots.
2. **Flash heartbeat-only app** (frontlight breathe only) → if it breathes, **custom firmware boots past
   secboot** and SDK-v2.5.0/stock-bootloader are compatible. Make-or-break, 1-component.
3. **Add probes + SD log + input incrementally**, re-flashing over USB.

If step 2 doesn't breathe → secboot/boot-compat is the blocker; pivot to SWD or signing. We'll know fast.

## Risks

- **Secure boot** (SHA-256/ECDH present in bootloader; recovery→DFU jump only vector-checks, no app-sig
  check found) — may or may not gate the normal app jump. Resolved empirically by step 2.
- **SDK v2.5.0 vs stock v1.4 bootloader** clock/PSRAM init compat — de-risked by minimal step-2 app.
- **PA01 PWM** channel/polarity must match → fall back to GPIO toggle.
- **SD-over-SPI1** init must be configured right in board.conf/driver.
- **KEY2/PA11, LED1/PA26** unverified → optional, non-fatal.

## Build & test

- Build: `scons --board=wodle -j8`. Static check: LOAD segment @ `0x12218000`, entry in range.
- Test = the phased empirical bring-up above; each step independently observable (frontlight / SD log).
- No host unit tests for firmware logic in v1 (hardware-in-the-loop); `mk_update.py` and any pure helpers
  get a quick self-check.

## Deferred (v2, after SWD tap)

E-ink display driver: identify controller (`lcd_rreg` over finsh, or RE stock init+LUT), resolve
EPD BUSY/DC/RST pins, write driver, render "hello world" text/bitmap at 528×792.
