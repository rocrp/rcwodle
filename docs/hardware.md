# wodle / "AI Dou" — hardware notes

Reverse-engineering record for writing a custom framework. Source: stock firmware at
`/Users/rocry/Downloads/firmware/` (v1.4.0.0422). Started 2026-06-01.

Confidence tags: **[C]** confirmed by direct evidence · **[?]** unconfirmed / inferred · **[HW]** needs the physical device to settle.

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
| Display | **e-paper** (EPD busy-pin handshake, partial+full refresh, 4-level gray, ghost-clear); driver module named `st7789`; frontlight PWM on **PA01** | C |
| Display controller + resolution | unknown (`CO5300`/`TFT` are SDK-template leftovers) | ? / HW |
| Touch | **CST816** (I²C) | C |
| Charger | **AW32001** (I²C) | C |
| Fuel gauge | **BQ27220** (I²C) | C |
| Audio | amp **AW8155** + SoC internal codec/`audprc` + PDM mic; 3A AEC/AGC/ANS; Opus | C |
| Radio | **BLE + BT-classic only, NO WiFi**; internet via **BT-PAN tethering** | C |
| Buses live | uart1/2, spi1, i2c1/2/4, gpio, pwm2, gpadc, flash1/2 | C |
| I²C bus↔chip↔address mapping | unknown | ? |

## 4. Memory / flash layout

NOR @ `0x12000000` (16 MB → `0x13000000`). Addresses below from `update.json`, CRC32-verified.

| File | Flash addr | Region | Note |
|---|---|---|---|
| (ftab + bootloader + ?) | `0x12000000`–`0x12218000` | ~2.2 MB | contents **[?]** (likely ftab/bootloader/lcpu) |
| `hcpu_app.bin` | `0x12218000` | `0x240000` | HCPU app, XIP |
| `ezip_image.bin` | `0x12460000` | `0x680000` | eZip UI asset atlas |
| `font_data.bin` | `0x12AE0000` | `0x400000` | packed fonts |
| (FS / KVDB) | `0x12EE0000`–`0x13000000` | ~1.1 MB | **[?]** |

## 5. Flashing & low-level access

- **[C]** Stock update path = **microSD "tf_ota"**: FAT32 card with `update.json` + the 3 `.bin` files → device verifies version+CRC32, writes, reboots. Skips if version not newer. **This is the safe recovery path — keep a known-good card.**
- **[C]** Live **RT-Thread finsh shell** is compiled in. Commands present: `pin`, `regop`, `list_device`, `lcd_rreg`/`lcd_ctrl`, `epd_stat`/`epd_test`, `fal`, `nvds`, `kvdb_debug`.
- **[C]** Debug UART / SWD pair = **PA18 / PA19** (the pins `dbguart2jlink` re-muxes; it writes pinmux regs `0x5000307c` / `0x50003080`).
- **[?]** finsh password likely `rtthread`; console baud likely 1000000 (else 115200) — both unverified.
- **[C, from docs]** Custom builds flash with **`sftool -c SF32LB52`** over UART; enter ROM serial loader via the **Mode strap pin** (Mode=1). Exact Mode-pin location on this board **[HW]**.

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
- Datasheet **DS0056 SF32LB56x V1.8** (note: this is **56x**, not our 52x): https://downloads.sifli.com/silicon/DS0056-SF32LB56x-Datasheet%20V1p8.pdf **[C]** — *still need the SF32LB52x datasheet* **[?]**
- RE precedent (sister SF32LB551, methodology + eZip format + default creds): https://blog.byterialab.com/reversing-the-xiaomi-redmi-watch-through-crafted-watchfaces/ **[C]**

---

## 8. Comments & suggestions — upcoming moves

Only steps I'm confident about, in order. No architecture guesses yet.

1. **Read the chip marking** (open case) → settle 52x vs 56x. Everything downstream depends on it. **[HW]**
2. **Get the pin map.** Three confirmed-viable methods, cheapest first:
   - **finsh console** on PA18/PA19 → run `pin`, `list_device`, `regop`, `lcd_rreg` → reads live config, no disassembly. **[HW]**
   - **Disassemble** `hcpu_app.bin` at base `0x12218000` (M33 Thumb-2) → recover every `HAL_PIN_Set(pad,func,flags,hcpu)` call; decode `func` via `sf32lb52x/bf0_pin_const.c`. Needs no hardware — **I can run this now.**
   - **SWD dump** (J-Link/DAPLink on PA18/PA19) → full flash image + real partition table. **[HW]**
3. **Keep a recovery microSD** with the stock files before touching anything (the tf_ota path is the un-brick).
4. **Base the custom framework on the OpenSiFli SDK**, `sf32lb52-lcd_n16r8` board template; flash via `sftool -c SF32LB52`. Confirm the Mode strap pin location first.
5. **Defer**: display controller/resolution and per-chip I²C addresses — resolve via `lcd_rreg` + `list_device`/`i2c` scan on the live console (step 2a), not by guessing.

**Decisions still needed from you:** can you open the case + photograph the PCB; and do you have a USB-UART adapter and/or J-Link/DAPLink. The framework's shape is not decided yet — settle 1–2 first.
