# wodle / "AI Dou" — hardware notes

Reverse-engineering record for writing a custom framework. Source: stock firmware at
`/Users/rocry/Downloads/firmware/` (v1.4.0.0422). Started 2026-06-01.

Confidence tags: **[C]** confirmed by direct evidence · **[C-RE]** decoded from the binary · **[?]** unconfirmed / inferred · **[HW]** needs the physical device to settle.

> **Recovered pin map + 103-command finsh list → [`firmware-analysis.md`](firmware-analysis.md).** USB-connect test (2026-06-01): device does **not** enumerate — no serial, no disk volume. Charge-only port.

> **Upstream lineage [C, 2026-06-03].** The stock app is a fork of **[`github.com/78/xiaozhi-sf32`](https://github.com/78/xiaozhi-sf32)** (canonical XiaoZhi reference fw; SDK = `OpenSiFli/SiFli-SDK`), built for its **`sf32lb52-lcd_n16r8`** board/solution. Confirmed by: flash map byte-identical to upstream `ptab.json`; the `sf32lb563`/`CO5300`/`st7789` strings all trace to upstream source lines (below). hiveton's deltas = e-paper panel (vs upstream CO5300 AMOLED) + reader UI + microSD `tf_ota` + USB-CDC HVR1 recovery (none upstream). See [`README.md`](../README.md) for the full delta table.

---

## 1. What it is

- **[C]** SiFli SoC, **not** an ESP32. Evidence: firmware strings `SiFli Corporation`, `chip_model_name: sf32lb563`, `bf0_hal_*`, HCPU/LCPU IPC. (The `sf32lb563` string is a **hardcoded literal in upstream** `app/src/xiaozhi_client_public.c:40` — copied verbatim, not a real part ID; see §1 part-ID note.)
- **[C]** Dual-core **Cortex-M33** (HCPU + LCPU).
- **[C]** Stack: **RT-Thread** + **LVGL v9** + cherryusb/MUSB + lwIP 2.1.2 + FlashDB. Built with GCC arm-none-eabi.
- **[C]** Silicon memory map is **SF32LB52x-style**: flash XIP base `0x12000000` = `QSPI2_MEM_BASE` (verified in SDK `drivers/cmsis/sf32lb52x/mem_map.h:102`). Also `dbguart2jlink` exists only `#if SF32LB52X`; `sftool` only supports `-c SF32LB52`; board macro `SF32LB52_LCD_N16R8_TFT_CO5300` embedded.
- **[C]** Exact part: **SF32LB525 (525UC6)**, N16R8 (16 MB ext QSPI NOR + 8 MB PSRAM). Confirmed by **physical board marking "思澈/SiFli 525"** (user, 2026-06-02) — matches the part already chosen in `board/wodle/ptab.yaml` (`SF32LB525UC6`). Firmware self-labels `sf32lb563`/`hdk563` — **confirmed mislabel** (build is `sf32lb52-lcd_n16r8`, `sftool -c SF32LB52`, XIP base `0x12000000`=QSPI2, `dbguart2jlink` `#if SF32LB52X`). SiFli's own 525 EVB = SDK board `eh-lb525`.
- **[C]** Product = "AI Dou" (`ai_dou`), vendor **hiveton** (`hiveton-dou-project`). A XiaoZhi-AI voice e-reader.
- **[C]** ~~#1 open question: 52x vs 56x~~ **RESOLVED → SF32LB52x (525).** Use the `sf32lb52x` CMSIS enum set for pin decoding. Remaining HW open items: EPD controller/resolution detail + GPIO roles (§8), and whether the cellular modem (below) is populated.

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
| Display controller | unknown. `CO5300`/`TFT`/`st7789` strings = **upstream leftovers** — upstream solution = `SF32LB52_LCD_N16R8_TFT_CO5300`, driver `app/peripherals/st7789/`; hiveton swapped the panel to e-paper but kept the names. Real EPD controller still needs `lcd_rreg` on hardware. | ? / HW |
| Touch | **CST816** (I²C) | C |
| Charger | **AW32001** (I²C) | C |
| Fuel gauge | **BQ27220** (I²C) | C |
| Audio | amp **AW8155** + SoC internal codec/`audprc` + PDM mic; 3A AEC/AGC/ANS; Opus | C |
| Radio (on-chip) | **BLE + BT-classic only, NO WiFi** (SiFli internal); internet via **BT-PAN tether** to a phone (lwIP-over-BNEP). Both fw v0422 & v0853: `net: BT/PAN only manager initialized` | C |
| Cellular (board) | **Quectel EG800Q** — LTE **Cat 1 bis (4G)** modem (LGA; ~10/5 Mbps; variants EG800Q-NA / EG800K-EU 2G+4G). Reported on PCB (user, 2026-06-02). **NOT used by either firmware** — exhaustive bin scan found zero AT / SIM / Quectel / USB-modem-CDC code. So on the builds we have it is **unpopulated or fw-unused**; a 4G data path needs different firmware. **Confirm by PCB inspection** (LGA module + nano-SIM holder) | ?-HW |
| Bus pins (recovered) | flash MPI2 `PA12/13/14/15/16/17`; console **UART1 RX=PA18 TX=PA19**; I²C1 `SCL=PA07`; I²C2 `SCL=PA31 SDA=PA32`; SPI1 `CLK=PA28 CS=PA29 DIO=PA24 DI=PA25` | C-RE |
| I²C devices | **I²C1:** CST816 touch @0x15. **I²C2** (PA31/32): AW32001 charger @0x49 + BQ27220 gauge @0x55. AW8155 amp = not I²C (GPIO mode pin) | C-RE |

> **Connectivity caveat (2026-06-02).** A `boot.network_mode=bt` setting exists on the SD card (the `config/device_config.cfg` key — there is **no** separate `network_mode.cfg`; earlier notes that named one were wrong) — the key *name* implies the product family is designed for **selectable** network modes, consistent with a 4G SKU. But **only `bt` is implemented** in fw v0422/v0853 (no `4g`/`cell` mode token found). If the EG800Q is populated, a custom framework could add **standalone 4G** (no phone tether): the modem attaches over UART (AT) or USB — find which SiFli UART is wired to it (only UART1=PA18/19 console is mapped so far; UART2=PA20/27 is a candidate).

## 4. Memory / flash layout

NOR @ `0x12000000` (16 MB → `0x13000000`). **Authoritative map → [`firmware254.md`](firmware254.md) §1**
(parsed from `ftab.bin`, magic `FCES`) — now **confirmed byte-identical to upstream
`78/xiaozhi-sf32` `app/project/sf32lb52-lcd_n16r8_hcpu/ptab.json`**, so the formerly-`[?]` regions are
named. Summary:

| Region | Flash addr | Size | Note |
|---|---|---|---|
| ftab | `0x12000000` | 32 K | flash table |
| DFU_PAN_LOADER (`dfu_pan.bin`) | `0x12008000` | 2 M | LCPU + BT core + DFU/recovery |
| bootloader | `0x12208000` | **64 K** | runs in SRAM `0x20020000` |
| `hcpu_app.bin` (HCPU app) | `0x12218000` | 0x240000 | XIP |
| KVDB_DFU / KVDB_BLE (FAL) | `0x12458000` / `0x1245C000` | 16 K each | OTA flag + BLE bond store |
| `ezip_image.bin` (EZIP_IMAGE) | `0x12460000` | 0x680000 | eZip sprite atlas (≤528×256) |
| `font_data.bin` (FONT_DATA) | `0x12AE0000` | 0x400000 | TTF (Ubuntu Mono Bold + CJK) |

PSRAM1 @ `0x60000000` (8 MB): PSRAM_CODE 2 M + PSRAM_DATA 6 M.

## 5. Flashing & low-level access

- **[C]** Stock update path = **microSD "tf_ota"**: FAT32 card with `update.json` + the 3 `.bin` files → device verifies version+CRC32, writes, reboots. Skips if version not newer. **This is the safe recovery path — keep a known-good card.**
- **[C]** Live **RT-Thread finsh shell** is compiled in. Commands present: `pin`, `regop`, `list_device`, `lcd_rreg`/`lcd_ctrl`, `epd_stat`/`epd_test`, `fal`, `nvds`, `kvdb_debug`.
- **[C-RE]** Console/debug UART = **USART1: PA18=RX, PA19=TX** (recovered from firmware; also the SWD pair per datasheet — `dbguart2jlink` re-muxes them). **Attach a 3.3 V USB-UART here for the finsh shell.**
- **[?]** finsh password likely `rtthread`; console baud likely 1000000 (else 115200) — both unverified.
- **[C]** Boot straps (datasheet): `Bootstrap[1]=PA13`, `Bootstrap[0]=PA17` — sampled at reset (these pins double as MPI2 flash D1/D3 at runtime); LL=SPI-NOR boot.
- **[C, from docs]** Flash custom builds with **`sftool -c SF32LB52 -p <port> -b 1000000 write_flash <file>@<addr>`** over UART (PA18/PA19); `--before default_reset` auto-enters the loader. sftool v0.2.3 ships macOS prebuilt binaries.

## 6. Cloud / protocol (for reference, not needed for bring-up)

- **[C]** XiaoZhi: `wss://api.tenclass.net/xiaozhi/v1/` (host+path from upstream `xiaozhi_mqtt.h:8`), OTA `ota.sifli.com` (`…/v2/xiaozhi/<solution>/<board>?chip_id=…&version=latest`), portal `xiaozhi.me`, weather = Seniverse. In-firmware **MCP** server (device-side: volume/light/motor/GPIO; cloud-side extends the LLM). Audio = **Opus**, 60 ms frames (mic 16 kHz, speaker 24 kHz). State machine Idle ↔ Listening ↔ Speaking. Keyword wake "小智小智" (upstream; wodle's enablement unconfirmed). Full app source = upstream `78/xiaozhi-sf32` `app/src/`.

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

1. ~~Read the chip marking → settle 52x vs 56x.~~ **DONE — it's SF32LB525.** New PCB question instead: **is the Quectel EG800Q (4G) populated, and is there a nano-SIM holder?** If yes, trace which SiFli UART/USB feeds it → unlocks standalone-4G in a custom build. **[HW]**
2. **Pin map — DONE (partial)**, see [`firmware-analysis.md`](firmware-analysis.md). To COMPLETE it (I²C1 SDA, the EPD/touch GPIO roles, the SPI1 device): attach USB-UART to **PA18(RX)/PA19(TX)** and run `pin` + `list_device` on the live finsh console. **[HW]**
3. **Keep a recovery microSD** with the stock files before touching anything (the tf_ota path is the un-brick).
4. **Base the custom framework on the OpenSiFli SDK**, `sf32lb52-lcd_n16r8` board template; flash via `sftool -c SF32LB52`. Confirm the Mode strap pin location first.
5. **Defer**: display controller/resolution and per-chip I²C addresses — resolve via `lcd_rreg` + `list_device`/`i2c` scan on the live console (step 2a), not by guessing.

**Decisions still needed from you:** can you open the case + photograph the PCB; and do you have a USB-UART adapter and/or J-Link/DAPLink. The framework's shape is not decided yet — settle 1–2 first.
