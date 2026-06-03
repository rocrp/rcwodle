# Firmware analysis — recovered from `hcpu_app.bin`

Static reverse-engineering (no hardware). Method: `tools/fw_analyze.py` disassembles the
HCPU app (Thumb-2, base `0x12218000`), models r0–r3 immediates, finds the functions whose
call sites decode to valid `HAL_PIN_Set(pad, func, flags, hcpu)` tuples, and translates the
integers via the SDK enums (`sf32lb52x/bf0_pin_const.h`).

Confidence: **[C-RE]** decoded from the binary, high confidence for explicit peripheral
functions · **[?]** inferred / not yet pinned down.

> ⚑ **Superseded by the official schematic (2026-06-03).** The vendor dev package includes the SoC
> pin-assignment schematic → the **authoritative net→PAxx map now lives in
> [`../refs/schematic/README.md`](../refs/schematic/README.md)** (tag **[C-sch]**). The binary-recovered
> map below is **correct where it overlaps** (LCDC1, flash, UART1, I²C2, SPI1) and is kept for method +
> provenance, but the schematic **wins on conflicts**. Net effect of the cross-check:
> - **Confirmed:** EPD `CS=PA03 CLK=PA04 DIO0=PA05 DC=PA06`, flash `PA12–17` (straps PA13/PA17), console
>   `UART1 PA18/19`, sensor `I²C2 SCL=PA31 SDA=PA32`, **SPI1 = microSD** (PA24/25/28/29).
> - **Resolved unknowns:** EPD `RST=PA00`, `TE/BUSY≈PA02`; touch `I²C1 SCL=PA07 / SDA=PA08`, `INT=PA42`;
>   `TFDET=PA33`; `PWRKEY=PA34`; `KEY2=PA43 / KEY3=PA44`; **NFC on SPI2 (PA37–40)**; **4G modem on UART2
>   (PA26/27)**, enables `CAT1_PWR_EN=PA09 + CAT1EN=PA20`.
> - **Corrected** (two RE inferences that had borrowed the DevKit `board.conf`):
>   audio-PA enable is **PA11**, not PA10 (**PA10 = system `PWR_EN`**); charger INT is **PA41** (`PWR_INT`),
>   not PA44 (**PA44 = KEY3**).

## Recovered pin map (HCPU)

Merged from 2 pin-setter call targets (`0x1221e2c0`, `0x1244f788`). 974k instructions scanned.

| Pin | Function | Pull | Role | Tag |
|---|---|---|---|---|
| PA01 | GPIO / `PA01_TIM` | — | **Frontlight/backlight PWM** (matches `force PA01 low`) | C-RE |
| PA03 | `LCDC1_SPI_CS` | NOPULL | **E-paper** display chip-select | C-RE |
| PA04 | `LCDC1_SPI_CLK` | NOPULL | display clock | C-RE |
| PA05 | `LCDC1_SPI_DIO0` | NOPULL | display data 0 | C-RE |
| PA06 | `LCDC1_SPI_DIO1` | NOPULL | display data 1 (**dual-SPI**) | C-RE |
| PA07 | `I2C1_SCL` | PULLUP | **I²C bus 1** clock (SDA not captured, likely PA08) | C-RE |
| PA12 | `MPI2_CS` | NOPULL | **NOR flash** (16 MB) chip-select | C-RE |
| PA13 | `MPI2_DIO1` | PD | flash D1 | C-RE |
| PA14 | `MPI2_DIO2` | PU | flash D2 | C-RE |
| PA15 | `MPI2_DIO0` | PD | flash D0 | C-RE |
| PA16 | `MPI2_CLK` | NOPULL | flash clock | C-RE |
| PA17 | `MPI2_DIO3` | PU | flash D3 | C-RE |
| PA18 | `USART1_RXD` | PULLUP | **console / debug UART RX** | C-RE |
| PA19 | `USART1_TXD` | PULLUP | **console / debug UART TX** | C-RE |
| PA24 | `SPI1_DIO` | NOPULL | **SPI1** data IO (device TBD) | C-RE |
| PA25 | `SPI1_DI` | PD | SPI1 data in | C-RE |
| PA28 | `SPI1_CLK` | NOPULL | SPI1 clock | C-RE |
| PA29 | `SPI1_CS` | NOPULL | SPI1 chip-select | C-RE |
| PA31 | `I2C2_SCL` | PULLUP | **I²C bus 2** clock | C-RE |
| PA32 | `I2C2_SDA` | PULLUP | I²C bus 2 data | C-RE |
| PA34 | GPIO | PD | **PWRKEY / KEY1 / power button** (schematic `LONGPRESS_RST`; ref `board.conf` KEY1_PIN=34) | C-sch |
| PA10 | GPIO | — | **system `PWR_EN`** ~~AW8155 amp enable~~ — schematic: PA10=`PWR_EN`; the AW8155 enable is **PA11** (`PA_EN`) | C-sch |
| PA44 | GPIO | — | **KEY3 button** ~~AW32001 charger INT~~ — schematic: PA44=`KEY3`; charger INT is **PA41** (`PWR_INT`) | C-sch |
| PA00 | GPIO | — | **EPD reset** (`LCDC1_SPI_RSTB`) | C-sch |
| PA02 | GPIO | — | **EPD TE / BUSY** (`LCDC1_SPI_TE`) | C-sch |
| PA33 | GPIO | — | **microSD card-detect** (`TFDET`) | C-sch |
| PA37–40 | SPI2 | — | **NFC** (DIO/DI/CLK/CS) | C-sch |
| PA42 | GPIO | — | **touch INT** (`TP_INT`) | C-sch |
| PA43 | GPIO | — | **KEY2 button** | C-sch |
| PA21 | — | — | not routed on schematic sheet 1 — still unknown | ? |

**Buses summarised** (schematic-confirmed unless noted):
- **E-paper panel (UC8179C)** → LCDC1 SPI: `CS=PA03, CLK=PA04, DIO0=PA05, DC=PA06`, `RST=PA00`, frontlight `PWM=PA01`, `TE/BUSY≈PA02`.
- **NOR flash 16 MB** → MPI2/QSPI2 (`0x12000000`): `CS=PA12, CLK=PA16, D0=PA15, D1=PA13, D2=PA14, D3=PA17`.
- **Console/debug UART** → USART1: `RX=PA18, TX=PA19` (also the SWD pair per datasheet).
- **Touch I²C1** `SCL=PA07, SDA=PA08`, `INT=PA42` → CST836U @0x15 · **sensor I²C2** `SCL=PA31, SDA=PA32`, `INT=PA30` → AW32001 @0x49 (INT=PA41) + BQ27220 @0x55. AW8155 amp = GPIO enable `PA11`.
- **microSD (TF)** → SPI1 `CLK=PA28, CS=PA29, DIO=PA24, DI=PA25`, card-detect `TFDET=PA33`. **Confirmed** (was `[?]`).
- **4G modem (Quectel)** → USART2 `RX=PA26, TX=PA27`, enables `CAT1_PWR_EN=PA09 + CAT1EN=PA20`. Populated, unused by stock fw.
- **NFC** → SPI2 `DIO=PA37, DI=PA38, CLK=PA39, CS=PA40`. Populated, unused by stock fw; controller part TBD.

**Limits (honest):** static recovery only captures `HAL_PIN_Set` calls with constant args, so
this map is high-confidence but **partial**. Missing: I²C1 SDA, the per-GPIO purposes
(EPD/touch control lines), and any pin set via computed args. GPIO-role inference
(`tools/fw_gpio.py`) was **noise-dominated** (constants 0/1 swamp pin-arg detection; method
validated via button→PA34), yielding only weak, non-assertable candidates (touch↔PA42,
charger↔PA33). The live finsh `pin` / `list_device` dump (UART on PA18/PA19) is required to
complete and cross-check it.

## Reference-board cross-check (`sf32lb52-lcd_n16r8`)

> **Upstream note [2026-06-03].** The stock app forks **[`78/xiaozhi-sf32`](https://github.com/78/xiaozhi-sf32)**
> for its `sf32lb52-lcd_n16r8` board. That repo confirms the **flash map, app architecture, and the
> string origins** (`sf32lb563`@`xiaozhi_client_public.c:40`, solution `SF32LB52_LCD_N16R8_TFT_CO5300`,
> `app/peripherals/st7789/`). **But for PINS, use the SDK `sf32lb52-lcd_base` template below, not the
> upstream repo's bundled board** — the only fully-populated board in the upstream clone is `xty-ai`
> (audio PA-ctrl=PA26, backlight=PA42), which **differs from wodle** (audio=PA10, backlight=PA01). wodle
> matches the `lcd_n16r8`/`lcd_base` lineage, not `xty-ai`.

wodle derives from the SiFli DevKit board `sf32lb52-lcd_n16r8`. Its `hcpu/board.conf` names pins;
cross-referencing with wodle's *independently* recovered GPIO set corroborates:
- ~~PA10 = AW8155 amp enable~~ / ~~PA44 = charger INT~~ — these DevKit `board.conf` values **do NOT
  hold on wodle** (the schematic shows wodle rewired them): on wodle **PA10 = `PWR_EN`**, the AW8155
  enable is **PA11** (`PA_EN`); **PA44 = `KEY3`**, the charger INT is **PA41** (`PWR_INT`). A caution
  that DevKit cross-refs need schematic confirmation. **[C-sch]**
- **PA34 = KEY1 / power button** (`KEY1_PIN=34`; matches wodle schematic `PWRKEY` + datasheet) **[C-sch]**

Reference-only (NOT in wodle's recovered set → wodle differs): `KEY2=PA11`, `LED1=PA26`.
Base board panel = **CO5300 TFT/AMOLED** (`LCD_USING_TFT_CO5300`) — wodle swapped it for e-paper,
which is why `CO5300`/`TFT` strings linger. Buses enabled in ref: UART1, SPI1, I2C1/2/4,
PWMT1/PWMT2 (frontlight), ADC, RTC, EPIC.

### Labeled pins from `sf32lb52-lcd_base/bsp_pinmux.c` (DevKit) — resolves wodle unknowns

The shared base board's pinmux has **named** functions. Cross-referencing resolves/confirms:

| Pin | DevKit label | wodle status | Tag |
|---|---|---|---|
| **PA00** | `#LCD_RESETB` | wodle's unknown PA00 GPIO = **display RESET** | C-RE+ref |
| **PA01** | `GPTIM1_CH4` LCD backlight PWM | matches wodle frontlight | C-RE+ref |
| PA02 | `LCDC1_SPI_TE` (tearing) | wodle likely same | ref |
| PA03–06 | LCD CS/CLK/DIO0/DIO1 | matches wodle display | C-RE+ref |
| **PA07/PA08** | DevKit: LCD QSPI DIO2/DIO3 | **wodle uses 2-lane LCD → frees PA07/08 for I2C1**; so **I2C1 = SCL PA07 / SDA PA08** | C-RE (SDA inferred) |
| **PA10** | `AUDIO_PA_CTRL` | ⚠ wodle differs → PA10 = `PWR_EN`; AW8155 enable = **PA11** | C-sch |
| PA18/19 | UART1 debug | matches wodle console | C-RE+ref |
| **PA20/PA27** | `USART2` log UART | possible 2nd UART on wodle (unverified) | ref |
| **PA24/25/28/29** | `SPI1 (TF card)` | **identical to wodle SPI1 → SPI1 = microSD, CONFIRMED** | C-RE+ref |
| PA34 | Key1 power (kept PD for UART-download) | matches wodle | C-RE+ref |
| **PA35/PA36** | `USB_DP/USB_DM` (analog) | the USB port (charge/MSC) | ref |
| **PA44** | `VBUS_DET` | ⚠ wodle differs → PA44 = `KEY3`; charger INT = **PA41** (`PWR_INT`) | C-sch |
| touch | DevKit: RESET=PA09, INT=PA31, I2C1=PA30/33 | **wodle REWIRED** (PA31 = wodle I2C2_SCL) → wodle touch INT/RESET are elsewhere | ⚠ differs |

**Key takeaways:** (1) wodle is a DevKit derivative but the vendor **rewired I2C and touch control pins**
— so use wodle's *recovered* map, not the DevKit's, for those. (2) wodle runs the LCD in **2-data-lane**
QSPI (e-paper needs less bandwidth than the CO5300 AMOLED), which frees PA07/PA08 for I2C1. (3) SPI1 =
microSD is now confirmed. (4) PA00 = display reset.

## I²C device map (`tools/fw_xref.py`)

Each driver's init function references exactly one bus-name string → solid bus assignment.

| Bus | Pins | Device | Addr (7-bit) | Tag |
|---|---|---|---|---|
| I²C1 | SCL=PA07, SDA=PA08, INT=PA42 | **CST836U** touch (panel C2283A) | **0x15** (`0x2a` = 0x15<<1 seen) | C-sch |
| I²C2 | SCL=PA31, SDA=PA32, INT=PA30 | AW32001 charger (INT also PA41) | **0x49** (seen directly) | C-sch |
| I²C2 | (same bus) | BQ27220 fuel gauge | 0x55 (part default; not seen in scan) | C-RE bus / ? addr |
| — | GPIO `PA11` | AW8155 speaker amp | **not on I²C** (`PA_EN` enable pin) | C-sch |

> Firmware's driver is named `cst816`, but the physical part is **CST836U** (same Hynitron CST8xx
> family, register- and address-compatible) — confirmed by the vendor TP-test package
> (`refs/datasheets/CZ_C2283A_CST836U_TP_test.zip`: `chip_type=cst8xx`, `x_res=528 y_res=792`).

## Flash / partition layout

flash2 = MPI2/QSPI2, base `0x12000000`, 16 MB. The full table lives in the ftab (parsed in
[`firmware254.md`](firmware254.md) §1) — **confirmed byte-identical to upstream `78/xiaozhi-sf32`
`app/project/sf32lb52-lcd_n16r8_hcpu/ptab.json`**, so the boot-chain region between `0x12000000` and
`0x12218000` (formerly `[?]`) is now named: ftab 32 K · DFU_PAN_LOADER (`dfu_pan.bin`, LCPU+BT+recovery)
2 M · bootloader 64 K. FAL magic `0x45503130` appears only for the two KVDB regions:

| Region | Offset | Addr | Size |
|---|---|---|---|
| ftab | 0x000000 | 0x12000000 | 32 K |
| DFU_PAN_LOADER (`dfu_pan.bin`) | 0x008000 | 0x12008000 | 2 M |
| bootloader | 0x208000 | 0x12208000 | 64 K (runs SRAM 0x20020000) |
| HCPU app | 0x218000 | 0x12218000 | 0x240000 |
| KVDB_DFU (FAL) | 0x458000 | 0x12458000 | 16 K |
| KVDB_BLE (FAL) | 0x45c000 | 0x1245c000 | 16 K |
| EZIP assets | 0x460000 | 0x12460000 | 0x680000 |
| FONT_DATA | 0xAE0000 | 0x12AE0000 | 0x400000 |

## Audio

Opus codec; sample rates 16000 / 24000 / 48000 present; configurable `frame_ms`/`frame_dur`;
full **3A** front-end (AEC/AGC/ANS); PDM mic. Speaker via **AW8155** class-D amp (GPIO mode
control, not I²C). Architecture: `audio_server` + `audio_3a` (uplink/downlink/far-end) + HFP.

## Power (AW32001 charger + BQ27220 gauge, both I²C2)

- **AW32001 @0x49**: configured **IIN=550mA, ICHG=512mA, IPRE=31mA, VBAT=4.20V, VSYS=4.60V**;
  registers REG00..REG0C; shipping mode (EN_HIZ / FET_DIS / DIS_SHIPINT) for storage; the
  `shutdown` path requests shipping mode (fallback = SoC shutdown).
- **BQ27220**: reports percent, voltage(mV), current(mA), avg power(mW), batt_status, charging.

## Fonts & assets

- `font_data.bin` = standard **TTF/OTF** (sfnt `0x00010000`; GDEF/GPOS tables). Embedded face:
  **Ubuntu Mono Bold v0.80** (Dalton Maag) + a CJK face. Rendered via LVGL `tiny_ttf`
  (stb_truetype). → a custom framework can use stock TTFs.
- `ezip_image.bin` = sprite atlas: 22 sprites, 24-byte descriptors `{fmt, w, h, size, XIP-ptr}`,
  sizes 28×28 … **528×256**, XIP-mapped at `0x12460000`.

## Display resolution

**528 × 792** (portrait e-reader). Recovered from a one-shot create call `f(528, 792)`
@0x1221f5f8 (`tools/fw_res.py`) — the only clean large (W,H) pair in the image, and 528 matches
the widest UI asset. **[C-RE]** Now corroborated by the vendor filename `3.68_528X792`. The **EPD
controller is UltraChip UC8179C** (3.68″) — named by the vendor driver reference in
[`../refs/epd/`](../refs/epd/), which also gives the full init sequence + GC/DU/4-gray LUTs. **[C-sch]**

## finsh / MSH command inventory (103)

Stock firmware's control surface (run over the UART console). Bring-up-relevant ones in **bold**:

`abox_delay adc ae_log alarm_dump apa_set_mode aprc_debug assert audio_data bt_cm bt_rftest
btdm_dbg cat cd cm_cmd console cp` **`dbguart2jlink`** `df` **`drv_epic_cfg drv_lcd_test`**
**`epd_stat epd_test`** **`fal`** `fps2 free freq_scale gif_log help iperf` **`kvdb_debug
kvdb_assert`** **`lcd_ctrl lcd_rreg lcd_wreg`** **`lcpu`** `list_date` **`list_device`**
`list_event list_fd list_mailbox list_mem list_memheap list_mempool list_msgqueue list_mutex
list_sem list_thread list_timer ls mem_usage` **`mic_gain`** `mkdir mkfs mountfs mv`
**`net_bt_diag net_bt_open`** **`nvds`** **`pin`** `ping pm_dump pm_release pm_request pm_run
power ps pwd pwr_ctrl reboot` **`regop`** `reset rm rst set_3a_aec_en set_3a_agc_en show_date
sible sysinfo` **`tf_ota_check tf_ota_info tf_ota_install`** `time time_now time_sync`
**`tp_ctrl`** **`uart`** `ui_dbg_books ui_dbg_books_next ui_dbg_books_prev ui_dbg_font_select
ui_dbg_fonts ui_dbg_fw ui_dbg_reading ui_sw usb_disk_off usb_disk_on usb_disk_status version
wdt` **`xiaozhi2`**

## USB-connect result (2026-06-01)

Device plugged into the Mac → **nothing enumerated**: no serial port, no new disk volume,
no new entry in `ioreg`/`diskutil`. Confirms the USB port is **charge-only as wired** (or
USB mass-storage `usb_disk_on` needs an on-device toggle reachable only from the UI/console).
No data channel via USB.

## SD card contents (read 2026-06-01)

microSD (FAT32, 31 GB) pulled from the device. Top-level dirs: `books/ cache/ config/ firmware/
font/ games/ mp3/ pic/ record/`.

- **`config/device_config.cfg`** — plaintext `key=value` settings, **on the card → editable (no-wire
  config tap)**:
  `version=1; boot.network_mode=bt; boot.auto_connect=1; display.brightness=0;
  display.standby_timeout_sec=60; ui.language=zh-CN; reading.use_system_font=1; reading.font_size=28;
  reading.line_space=2; audio.music_volume=15; audio.ai_volume=8; wallpaper.path=/pic/1.jpg;
  ai.auto_resume=1; weather.auto_refresh=1`. (No debug/log key present; parser may/​may-not honor extras.)
- **`config/reading_state.cfg`** — `reading_state_v1 / sequence=0` (reader bookmark).
- **`firmware/`** — OTA payload, read from **`/firmware/update.json`** (confirms the flash folder).
  Card = **V1.4.0.0246** (2026-05-23); the Downloads copy is **newer V1.4.0.0422** (hcpu 2,340,748 vs
  2,142,852 B). To flash 0422, replace the 3 bins + `update.json` in this `/firmware/` folder
  (tf_ota only flashes a *newer* version).
- **`font/`** — `.hdfont` (SiFli HD-font) faces: Inter (Latin, 296 KB), LXGW WenKai GB (82 MB),
  MiSans Bold (95 MB), OPPO Sans 4.0 (84 MB) — CJK except Inter.
- **`record/`** — `rec_*.wav`: the AI assistant saves voice recordings here (private user audio; **not
  inspected**). RTC unset → default timestamp 2026-01-01.
- `books/` (txt/epub + COVERS), `pic/` (wallpapers incl. `/pic/1.jpg`), `mp3/`, `games/petgame/`,
  `cache/` (empty).

**Tap value:** editing `config/device_config.cfg` is a zero-wire way to change settings; `/firmware/`
is the confirmed OTA/flash path.

## Debug / tap surface — how to get into the OS

Investigated whether any no-wire debug entry exists. Findings:

- **Interactive shell = finsh/MSH, console device `uart1` ONLY** (PA18 RX / PA19 TX). 103 commands
  incl. `pin`, `regop`, `list_thread`, `list_device`, `usb_disk_on`, `tf_ota_*`. **No finsh over
  BT / USB-CDC / TCP / SPP** — searched and found none. → the 2-wire UART is the only real console. **[C-RE]**
- **USB**: only **MSC disk mode** (`usb_disk_on` → exposes the TF card as a drive), triggered by the
  finsh command, **not** automatically on plug (verified: nothing enumerated). USB-CDC (`eCDC`) is
  compiled but not used as a console. The SD card is removable anyway, so this is not a unique tap. **[C]**
- **BT SPP server** is present (`spp_*`) but is a generic data channel — **not bridged to the shell**. **[C]**
- **"Sifli BLE command"** = SiFli's proprietary BLE control/provisioning (companion app), not the shell. **[C]**
- **MCP server** `XiaoZhi-SF32` (protocol `2024-11-05`) exposes **`tools/list` + `tools/call`** — a real
  structured remote-control surface — but it rides the **cloud WebSocket** (`wss://api.tenclass.net/xiaozhi/v1/`,
  reached via BT-PAN tethering), not a local port. The WS URL is taken from the OTA response
  (`OTA websocket url present/empty…`), so it is **controllable**: stand up your own XiaoZhi server and
  redirect the device's OTA host (e.g. DNS on the tethering network) → then drive the OS via MCP tool calls.
  (Same self-hosting pattern as the picture-book / xiaozhi-esp32-server work.) **[C]**
- **Config files** = a key/value settings store; **no debug-enable flag found**. Correction to an
  earlier guess: they live on the **removable SD card** (`config/device_config.cfg` +
  `config/reading_state.cfg` — verified in the card snapshot, see below), **not** an internal FS, and
  there is **no** separate `network_mode.cfg` (the `boot.network_mode=bt` key is inside
  `device_config.cfg`). So they **are** editable zero-wire by pulling the card. **[C]**
- **Recovery-SD correction**: the OTA manifest is read from **`/firmware/update.json`** (a `firmware/`
  folder on the card), not the card root. **[C-RE]**

**Tap options, ranked:** (1) finsh over UART PA18/PA19 — full shell, needs 2 wires; (2) self-hosted
XiaoZhi WebSocket server + OTA-host redirect — full MCP/IoT remote control, no wires; (3) just pull the
SD card and read its FS directly — trivial, no tap needed.

## Reference build (toolchain proven)

OpenSiFli SDK v2.5.0 builds for board `sf32lb52-lcd_n16r8`:

- **Toolchain PROVEN** ✅ : `install.sh` (China mirror) → `arm-none-eabi-gcc 14.2.1` + `sftool 0.1.16`
  in `~/.sifli/`; `scons --board=sf32lb52-lcd_n16r8 -j8` → a symboled `main.elf`. Build fixes required:
  empty `UV_CONFIG_FILE` (override global uv `exclude-newer`); stub empty `middleware/bluetooth/Kconfig`
  + comment 2 BT lines in `board.conf`; `git submodule update --init tools/SiliconSchema` (ptab needs it).

> A cross-version FLIRT symbol-match against the stock bins was tried and abandoned (low yield — stock fw
> was built on an older SDK) and is moot now that upstream is identified. The `board.conf` cross-check it
> spun off has itself been **superseded by the schematic** (which corrected PA10/PA44 — see top banner).

## Reproduce

```
uv run tools/fw_analyze.py
```
