# rcwodle — custom framework for the "wodle" / "AI Dou"

Reverse-engineering + custom-firmware workspace for the **wodle** (vendor product **"AI Dou"**,
`ai_dou`, by **hiveton**): a XiaoZhi-AI (小智) voice-assistant **e-reader**. Goal: own the device —
build our own firmware and deploy it with no wires.

Started 2026-06-01 by RE'ing the stock firmware. As of 2026-06-03 the stock app's upstream is
**identified** (see below) **and the vendor's own dev package is in hand** (`refs/schematic/`,
`refs/epd/`, `refs/datasheets/`) — so the pin map, EPD controller, touch part, and modem/NFC wiring
are now **ground truth from the schematic**, not inference.

## What it is, in one line

**SiFli SF32LB525 (525UC6), N16R8** (16 MB QSPI NOR XIP @ `0x12000000` + 8 MB PSRAM), dual Cortex-M33,
RT-Thread + LVGL v9. **528×792 e-paper (UltraChip UC8179C)**, **CST836U** touch (panel C2283A), AW8155
amp + on-chip codec + PDM mic (Opus, HW 3A). On-board **4G Cat-1 modem (Quectel, on UART2)** + **NFC
(SPI2)** — both wired but **unused by the stock firmware**. Radios: **BT-Classic + BLE, no WiFi** —
stock internet path is a **BT-PAN tether** to a phone. No USB-serial.

## Upstream lineage (the key insight)

The stock app is a **fork of [`github.com/78/xiaozhi-sf32`](https://github.com/78/xiaozhi-sf32)** (the
canonical XiaoZhi reference firmware; SDK = `OpenSiFli/SiFli-SDK`), built for its **`sf32lb52-lcd_n16r8`**
board/solution. The vendor's own `参考sdk.txt` points to
[`OpenSiFli/xiaozhi-sf32`](https://github.com/OpenSiFli/xiaozhi-sf32) — **SiFli's official fork of
`78/xiaozhi-sf32`**, carrying the same `sf32lb52-lcd_n16r8` board + `ptab.json`, so the cross-checks
below hold against either. This explains the long-standing oddities:

| Stock-firmware oddity | Explanation (from upstream source) |
|---|---|
| self-labels `chip_model_name: sf32lb563` | hardcoded literal in upstream `app/src/xiaozhi_client_public.c:40` — copied verbatim; **not** the real part. Real silicon = **SF32LB525** (board marking). |
| `CO5300` / `TFT` / `st7789` strings on an e-paper device | upstream solution name = `SF32LB52_LCD_N16R8_TFT_CO5300`, driver module `st7789` — hiveton swapped the panel to e-paper but kept the names. **Real controller = UltraChip UC8179C** (vendor `refs/epd/`). |
| flash map | **byte-for-byte = upstream `ptab.json`** (ftab/DFU_PAN/bootloader/app/KVDB/EZIP/FONT). |

**hiveton's deltas vs upstream:**
- Panel swapped **CO5300 AMOLED → e-paper** (528×792, **UC8179C**); reader/books UI added on top of the XiaoZhi app.
- **microSD `tf_ota`** updater (incl. whole-stack bootloader-transition) — *not* in upstream.
- **USB-CDC "HVR1" recovery** protocol — *not* in upstream (upstream only does DFU-over-PAN).
- PCB carries a **4G Cat-1 modem** (Quectel; user-reported EG800Q) **wired to UART2** (PA26/27, enables
  `CAT1_PWR_EN=PA9` + `CAT1EN=PA20`) and an **NFC** front-end (SPI2, PA37–40) — both **populated but
  unused** by the firmware we have (no AT / SIM / NFC code in the bins). The dev package ships the
  Quectel AT-command manual, so a custom build can drive **standalone 4G** (no phone tether).

Upstream ⇒ a custom framework can start from the real XiaoZhi app source, not a clean-room rewrite.

## Documentation

| Doc | Contents |
|---|---|
| [`docs/hardware.md`](docs/hardware.md) | What it is, silicon, component inventory, connectivity, references. **Start here.** |
| [`docs/firmware-analysis.md`](docs/firmware-analysis.md) | Recovered pin map (now **confirmed against the official schematic**) + I²C device map + finsh command inventory + upstream cross-check. |
| [`docs/firmware254.md`](docs/firmware254.md) | Full-build (V1.4.0.0853) decode: **authoritative flash/boot-chain map**, the 3 update mechanisms, **HVR1 USB-CDC recovery protocol**. |
| [`docs/finsh_bringup.md`](docs/finsh_bringup.md) | UART-console bring-up cheat-sheet (wiring PA18/PA19, commands to run). |
| [`docs/bluetooth.md`](docs/bluetooth.md) | BLE/classic feasibility — **verdict: no local BLE control path**; use UART or USB-CDC. |
| [`docs/framework_plan.md`](docs/framework_plan.md) | Custom-firmware runbook, the no-wire SD dev loop, remaining blockers. |
| [`refs/schematic/`](refs/schematic/) | **Official schematic pin map** (ground truth) — every net→PAxx. **The pin authority.** |
| [`refs/epd/`](refs/epd/) | EPD controller reference (**UC8179C** / UC8279 command set + LUTs + decoded notes). |
| [`board/wodle/`](board/wodle/) | OpenSiFli-SDK custom board def (`ptab.yaml` links HCPU app @ `0x12218000`). |
| [`firmware/hello_wodle/`](firmware/hello_wodle/) | **Our own validation firmware** — clean-room, simplest-first. S1 (proof-of-life) builds + links @ `0x12218000`; build/flash runbook inside. |

## Tools (`tools/`)

- **`wodle_flash.py`** — HVR1 USB-CDC flasher (the Mac-accessible flash channel). `flash`/`write`/`hello`/`reboot`.
- `wodle_ble.py` — bleak BLE CLI (`scan`/`info`/`listen`/`send`); proven, but device not advertising while bonded.
- `wodle_spp.py` — classic-BT SPP probe (PyObjC, untested).
- `fw_analyze.py` (pin map) · `fw_xref.py` (I²C) · `fw_res.py` (resolution) · `fw_usbrec.py` (HVR1 decoder) ·
  `fw_strxref.py` / `fw_gpio.py` / `fw_gatt.py` — static analyzers over the stock bins.
- `usb_recovery_qt/` — vendor's Hiveton USB Recovery GUI (Qt6) = ground-truth HVR1 reference impl.

## Status & next step

Static RE + the official dev package together close almost every open question: pin map (schematic),
flash map, recovery protocol, upstream identified, **EPD controller = UC8179C**, **touch = CST836U**,
and the **4G + NFC wiring**. What's left is **hardware validation**:

1. **Stand up the EPD driver** — UC8179C over LCDC1 SPI (`CS=PA3 CLK=PA4 SDA=PA5 DC=PA6 RST=PA0`,
   frontlight `PWM=PA1`). Command set + LUTs are in `refs/epd/`. One unknown to settle on hardware:
   the **BUSY** line (likely `PA2`/TE) and the exact TRES (`792×528`). Confirm via `lcd_rreg`/`epd_test`.
2. **De-risk the SD flash loop** by flashing the official newer build first, then a minimal custom app.
3. *(Optional)* Bring up the **4G modem** (Quectel AT over UART2, PA26/27) for standalone connectivity,
   and probe **NFC** (SPI2) — both unused by stock fw; the Quectel AT manual is in `refs/datasheets/`.

> Stock firmware images and the SD card snapshot live outside git (`refs/`, `~/Downloads/firmware*`).
> Keep a known-good recovery card — the `tf_ota` path is the un-brick.
