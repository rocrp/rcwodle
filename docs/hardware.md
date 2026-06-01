# wodle / "AI Dou" — hardware notes

Reverse-engineering record for writing a custom framework. Source: stock firmware at
`/Users/rocry/Downloads/firmware/` (v1.4.0.0422). Started 2026-06-01.

Confidence tags: **[C]** confirmed by direct evidence · **[C-RE]** decoded from the binary · **[?]** unconfirmed / inferred · **[HW]** needs the physical device to settle.

> **Recovered pin map + 103-command finsh list → [`firmware-analysis.md`](firmware-analysis.md).** USB-connect test (2026-06-01): device does **not** enumerate — no serial, no disk volume. Charge-only port.

---

## 1. What it is

- **[C]** SiFli SoC, **not** an ESP32. Evidence: firmware strings `SiFli Corporation`, `chip_model_name: sf32lb563`, `bf0_hal_*`, HCPU/LCPU IPC.
- **[C]** Dual-core **Cortex-M33** (HCPU + LCPU).
- **[C]** Stack: **RT-Thread** + **LVGL v9** + cherryusb/MUSB + lwIP 2.1.2 + FlashDB. Built with GCC arm-none-eabi.
- **[C]** Silicon memory map is **SF32LB52x-style**: flash XIP base `0x12000000` = `QSPI2_MEM_BASE` (verified in SDK `drivers/cmsis/sf32lb52x/mem_map.h:102`). Also `dbguart2jlink` exists only `#if SF32LB52X`; `sftool` only supports `-c SF32LB52`; board macro `SF32LB52_LCD_N16R8_TFT_CO5300` embedded.
- **[?]** Exact part: **SF32LB52x, N16R8** (≈16 MB ext QSPI NOR + 8 MB PSRAM, from the `N16R8` board tag). Firmware self-labels `sf32lb563` / board `hdk563` — treat as vendor mislabel.
- **[C]** Product = "AI Dou" (`ai_dou`), vendor **hiveton** (`hiveton-dou-project`). A XiaoZhi-AI voice e-reader.
- **[HW]** **#1 open question: confirm silicon by reading the chip silkscreen.** Determines which CMSIS enum set (`sf32lb52x` vs `sf32lb56x`) to use for pin decoding.

## 2. Why there is no `/dev/cu.*`

- **[C]** SiFli has no auto-enumerating USB-Serial/JTAG (unlike ESP32-S3); no CP210x/CH340 bridge on board.
- **[C]** Device does **not** enumerate on USB at all — verified against the live `ioreg` USB tree. USB port is **charge / mass-storage only**.
- **Conclusion: a serial port will never appear. Do not look for USB-serial drivers.**

## 3. Components (mined from `hcpu_app.bin`)

| Subsystem | Part / detail | Tag |
|---|---|---|
| MCU | SF32LB52x N16R8, dual M33 | C / ? |
| Display | **e-paper** (4-level gray, partial+full refresh, busy-pin); **LCDC1 dual-SPI: CS=PA03 CLK=PA04 D0=PA05 D1=PA06**; frontlight PWM on **PA01**; driver module named `st7789` | C / C-RE |
| Display resolution | **528 × 792** (portrait), recovered from create-call | C-RE |
| Display controller | unknown (`CO5300`/`TFT` = SDK-template leftovers) | ? / HW |
| Touch | **CST816** (I²C) | C |
| Charger | **AW32001** (I²C) | C |
| Fuel gauge | **BQ27220** (I²C) | C |
| Audio | amp **AW8155** + SoC internal codec/`audprc` + PDM mic; 3A AEC/AGC/ANS; Opus | C |
| Radio | **BLE + BT-classic only, NO WiFi**; internet via **BT-PAN tethering** | C |
| Bus pins (recovered) | flash MPI2 `PA12/13/14/15/16/17`; console **UART1 RX=PA18 TX=PA19**; I²C1 `SCL=PA07`; I²C2 `SCL=PA31 SDA=PA32`; SPI1 `CLK=PA28 CS=PA29 DIO=PA24 DI=PA25` | C-RE |
| I²C devices | **I²C1:** CST816 touch @0x15. **I²C2** (PA31/32): AW32001 charger @0x49 + BQ27220 gauge @0x55. AW8155 amp = not I²C (GPIO mode pin) | C-RE |

## 4. Memory / flash layout

NOR @ `0x12000000` (16 MB → `0x13000000`). Addresses below from `update.json`, CRC32-verified.

| File | Flash addr | Region | Note |
|---|---|---|---|
| (ftab + bootloader + ?) | `0x12000000`–`0x12218000` | ~2.2 MB | contents **[?]** (likely ftab/bootloader/lcpu) |
| `hcpu_app.bin` | `0x12218000` | `0x240000` | HCPU app, XIP |
| dfu / ble NVDS (FAL) | `0x12458000` / `0x1245c000` | 16 K each | OTA flag + BLE bond store |
| `ezip_image.bin` | `0x12460000` | `0x680000` | eZip sprite atlas (≤528×256) |
| `font_data.bin` | `0x12AE0000` | `0x400000` | TTF (Ubuntu Mono Bold + CJK) |
| (FS / KVDB) | `0x12EE0000`–`0x13000000` | ~1.1 MB | **[?]** |

## 5. Flashing & low-level access

- **[C]** Stock update path = **microSD "tf_ota"**: FAT32 card with `update.json` + the 3 `.bin` files → device verifies version+CRC32, writes, reboots. Skips if version not newer. **This is the safe recovery path — keep a known-good card.**
- **[C]** Live **RT-Thread finsh shell** is compiled in. Commands present: `pin`, `regop`, `list_device`, `lcd_rreg`/`lcd_ctrl`, `epd_stat`/`epd_test`, `fal`, `nvds`, `kvdb_debug`.
- **[C-RE]** Console/debug UART = **USART1: PA18=RX, PA19=TX** (recovered from firmware; also the SWD pair per datasheet — `dbguart2jlink` re-muxes them). **Attach a 3.3 V USB-UART here for the finsh shell.**
- **[?]** finsh password likely `rtthread`; console baud likely 1000000 (else 115200) — both unverified.
- **[C]** Boot straps (datasheet): `Bootstrap[1]=PA13`, `Bootstrap[0]=PA17` — sampled at reset (these pins double as MPI2 flash D1/D3 at runtime); LL=SPI-NOR boot.
- **[C, from docs]** Flash custom builds with **`sftool -c SF32LB52 -p <port> -b 1000000 write_flash <file>@<addr>`** over UART (PA18/PA19); `--before default_reset` auto-enters the loader. sftool v0.2.3 ships macOS prebuilt binaries.

## 6. Cloud / protocol (for reference, not needed for bring-up)

- **[C]** XiaoZhi: `wss://api.tenclass.net/xiaozhi/v1/`, OTA `ota.sifli.com`, weather = Seniverse. In-firmware **MCP** server. Audio = Opus.

---

## 7. References

**Local**
- Firmware: `/Users/rocry/Downloads/firmware/{hcpu_app.bin, ezip_image.bin, font_data.bin, update.json}` **[C]**
- SDK clone: `~/w/_tmp/SiFli-SDK` **[C, verified]**
  - Mem map / pin enums (our part): `drivers/cmsis/sf32lb52x/{mem_map.h, bf0_pin_const.c, bf0_pin_const.h}` **[C]**
  - `HAL_PIN_Set` impl: `drivers/hal/bf0_hal_pinmux.c` **[C]**
  - Closest board base: `customer/boards/sf32lb52-lcd_n16r8/` (and `…_jdi/`) **[C]**
  - Pinmux *format* example (different family — format only): `customer/boards/ec-lb563xxx/{bsp_pinmux.c, Kconfig, pinmux_563.xlsx}` **[C]**

**External**
- OpenSiFli SDK: https://github.com/OpenSiFli/SiFli-SDK **[C]**
- OpenSiFli flash tool `sftool`: https://github.com/OpenSiFli/sftool **[? referenced by SDK docs]**
- SiFli docs/wiki/downloads: https://wiki.sifli.com · https://docs.sifli.com · https://downloads.sifli.com **[C, reachable]**
- Datasheet **DS0052 SF32LB52x** (EN V2.3 / 中文 V2.4). Local: `refs/datasheets/DS0052-SF32LB52x-Datasheet-V2p3.pdf` (+ zh). URL: https://downloads.sifli.com/silicon/DS0052-SF32LB52x-Datasheet%20V2p3.pdf **[C]**. SKUs: SF32LB520U36/523UB6/525UC6/527UD6, all QFN68L, **45 GPIO (PA00–PA44)**.
- RE precedent (sister SF32LB551, methodology + eZip format + default creds): https://blog.byterialab.com/reversing-the-xiaomi-redmi-watch-through-crafted-watchfaces/ **[C]**

---

## 8. Comments & suggestions — upcoming moves

Only steps I'm confident about, in order. No architecture guesses yet.

1. **Read the chip marking** (open case) → settle 52x vs 56x. Everything downstream depends on it. **[HW]**
2. **Pin map — DONE (partial)**, see [`firmware-analysis.md`](firmware-analysis.md). To COMPLETE it (I²C1 SDA, the EPD/touch GPIO roles, the SPI1 device): attach USB-UART to **PA18(RX)/PA19(TX)** and run `pin` + `list_device` on the live finsh console. **[HW]**
3. **Keep a recovery microSD** with the stock files before touching anything (the tf_ota path is the un-brick).
4. **Base the custom framework on the OpenSiFli SDK**, `sf32lb52-lcd_n16r8` board template; flash via `sftool -c SF32LB52`. Confirm the Mode strap pin location first.
5. **Defer**: display controller/resolution and per-chip I²C addresses — resolve via `lcd_rreg` + `list_device`/`i2c` scan on the live console (step 2a), not by guessing.

**Decisions still needed from you:** can you open the case + photograph the PCB; and do you have a USB-UART adapter and/or J-Link/DAPLink. The framework's shape is not decided yet — settle 1–2 first.
