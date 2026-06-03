# rcwodle — custom framework for the "wodle" / "AI Dou"

Reverse-engineering + custom-firmware workspace for the **wodle** (vendor product **"AI Dou"**,
`ai_dou`, by **hiveton**): a XiaoZhi-AI (小智) voice-assistant **e-reader**. Goal: own the device —
build our own firmware and deploy it with no wires.

Started 2026-06-01 by RE'ing the stock firmware. As of 2026-06-03 the stock app's upstream is
**identified** (see below), which turns this from black-box RE into "we have the source + know the
vendor's deltas."

## What it is, in one line

**SiFli SF32LB525 (525UC6), N16R8** (16 MB QSPI NOR XIP @ `0x12000000` + 8 MB PSRAM), dual Cortex-M33,
RT-Thread + LVGL v9. **528×792 e-paper**, CST816 touch, AW8155 amp + on-chip codec + PDM mic (Opus,
HW 3A). **BT-Classic + BLE only, no WiFi** — internet via **BT-PAN tether** to a phone. No USB-serial.

## Upstream lineage (the key insight)

The stock app is a **fork of [`github.com/78/xiaozhi-sf32`](https://github.com/78/xiaozhi-sf32)** (the
canonical XiaoZhi reference firmware; SDK = `OpenSiFli/SiFli-SDK`), built for its **`sf32lb52-lcd_n16r8`**
board/solution. This explains the long-standing oddities:

| Stock-firmware oddity | Explanation (from upstream source) |
|---|---|
| self-labels `chip_model_name: sf32lb563` | hardcoded literal in upstream `app/src/xiaozhi_client_public.c:40` — copied verbatim; **not** the real part. Real silicon = **SF32LB525** (board marking). |
| `CO5300` / `TFT` / `st7789` strings on an e-paper device | upstream solution name = `SF32LB52_LCD_N16R8_TFT_CO5300`, driver module `st7789` — hiveton swapped the panel to e-paper but kept the names. |
| flash map | **byte-for-byte = upstream `ptab.json`** (ftab/DFU_PAN/bootloader/app/KVDB/EZIP/FONT). |

**hiveton's deltas vs upstream:**
- Panel swapped **CO5300 AMOLED → e-paper** (528×792); reader/books UI added on top of the XiaoZhi app.
- **microSD `tf_ota`** updater (incl. whole-stack bootloader-transition) — *not* in upstream.
- **USB-CDC "HVR1" recovery** protocol — *not* in upstream (upstream only does DFU-over-PAN).
- PCB carries a **Quectel EG800Q (4G)** modem — **unused** by the firmware we have (no AT/SIM code).

Upstream ⇒ a custom framework can start from the real XiaoZhi app source, not a clean-room rewrite.

## Documentation

| Doc | Contents |
|---|---|
| [`docs/hardware.md`](docs/hardware.md) | What it is, silicon, component inventory, connectivity, references. **Start here.** |
| [`docs/firmware-analysis.md`](docs/firmware-analysis.md) | **Authoritative recovered pin map** + I²C device map + finsh command inventory + upstream cross-check. |
| [`docs/firmware254.md`](docs/firmware254.md) | Full-build (V1.4.0.0853) decode: **authoritative flash/boot-chain map**, the 3 update mechanisms, **HVR1 USB-CDC recovery protocol**. |
| [`docs/finsh_bringup.md`](docs/finsh_bringup.md) | UART-console bring-up cheat-sheet (wiring PA18/PA19, commands to run). |
| [`docs/bluetooth.md`](docs/bluetooth.md) | BLE/classic feasibility — **verdict: no local BLE control path**; use UART or USB-CDC. |
| [`docs/framework_plan.md`](docs/framework_plan.md) | Custom-firmware runbook, the no-wire SD dev loop, remaining blockers. |
| [`board/wodle/`](board/wodle/) | OpenSiFli-SDK custom board def (`ptab.yaml` links HCPU app @ `0x12218000`). |

## Tools (`tools/`)

- **`wodle_flash.py`** — HVR1 USB-CDC flasher (the Mac-accessible flash channel). `flash`/`write`/`hello`/`reboot`.
- `wodle_ble.py` — bleak BLE CLI (`scan`/`info`/`listen`/`send`); proven, but device not advertising while bonded.
- `wodle_spp.py` — classic-BT SPP probe (PyObjC, untested).
- `fw_analyze.py` (pin map) · `fw_xref.py` (I²C) · `fw_res.py` (resolution) · `fw_usbrec.py` (HVR1 decoder) ·
  `fw_strxref.py` / `fw_gpio.py` / `fw_gatt.py` / `fw_symmatch.py` — static analyzers over the stock bins.
- `usb_recovery_qt/` — vendor's Hiveton USB Recovery GUI (Qt6) = ground-truth HVR1 reference impl.

## Status & next step

Static RE is largely complete (pin map, flash map, recovery protocol, upstream identified). The
remaining gaps **need the physical device**:

1. **EPD controller part + the last GPIO roles** (EPD BUSY/DC, touch INT/RST) → attach USB-UART to
   **PA18(RX)/PA19(TX)**, run `lcd_rreg`/`pin`/`list_device` on the finsh console. See `finsh_bringup.md`.
2. **Is the EG800Q 4G modem populated** (+ nano-SIM holder)? → PCB photo.
3. De-risk the SD flash loop by flashing the official newer build first, then a minimal custom app.

> Stock firmware images and the SD card snapshot live outside git (`refs/`, `~/Downloads/firmware*`).
> Keep a known-good recovery card — the `tf_ota` path is the un-brick.
