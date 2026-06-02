# Firmware analysis — recovered from `hcpu_app.bin`

Static reverse-engineering (no hardware). Method: `tools/fw_analyze.py` disassembles the
HCPU app (Thumb-2, base `0x12218000`), models r0–r3 immediates, finds the functions whose
call sites decode to valid `HAL_PIN_Set(pad, func, flags, hcpu)` tuples, and translates the
integers via the SDK enums (`sf32lb52x/bf0_pin_const.h`).

Confidence: **[C-RE]** decoded from the binary, high confidence for explicit peripheral
functions · **[?]** inferred / not yet pinned down.

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
| PA34 | GPIO | PD | **KEY1 / power button** (ref `board.conf` KEY1_PIN=34 + datasheet) | C-RE+ref |
| PA10 | GPIO | — | **AW8155 speaker-amp enable** (ref `board.conf` AW8155_GPIO_PIN=10 ✕ wodle GPIO) | C-RE+ref |
| PA44 | GPIO | — | **AW32001 charger INT** (ref `board.conf` CHARGER_INT_PIN=44 ✕ wodle GPIO) | C-RE+ref |
| PA00, PA21, PA33, PA38, PA42, PA43 | GPIO | mixed | EPD BUSY/RST/DC + touch INT/RST (wodle-specific; not in ref board) | ? |

**Buses summarised:**
- **E-paper panel** → LCDC1 in 2-data-lane SPI: `CS=PA03, CLK=PA04, D0=PA05, D1=PA06` (+ RST/DC/BUSY among the GPIOs above).
- **NOR flash 16 MB** → MPI2/QSPI2 (`0x12000000`): `CS=PA12, CLK=PA16, D0=PA15, D1=PA13, D2=PA14, D3=PA17`.
- **Console/debug UART** → USART1: `RX=PA18, TX=PA19` (also the SWD pair per datasheet).
- **I²C1** `SCL=PA07` (SDA `[?]`) · **I²C2** `SCL=PA31, SDA=PA32`. Which of CST816 / AW32001 / BQ27220 / AW8155 sits on which bus = `[?]` (needs live `i2c` scan or deeper RE).
- **SPI1** `CLK=PA28, CS=PA29, DIO=PA24, DI=PA25` — device `[?]`; **likely the microSD/TF card in SPI mode** (firmware probes `sdcard` and no SDMMC pins were recovered) or a NOR MTD. Unconfirmed.

**Limits (honest):** static recovery only captures `HAL_PIN_Set` calls with constant args, so
this map is high-confidence but **partial**. Missing: I²C1 SDA, the per-GPIO purposes
(EPD/touch control lines), and any pin set via computed args. GPIO-role inference
(`tools/fw_gpio.py`) was **noise-dominated** (constants 0/1 swamp pin-arg detection; method
validated via button→PA34), yielding only weak, non-assertable candidates (touch↔PA42,
charger↔PA33). The live finsh `pin` / `list_device` dump (UART on PA18/PA19) is required to
complete and cross-check it.

## Reference-board cross-check (`sf32lb52-lcd_n16r8`)

wodle derives from the SiFli DevKit board `sf32lb52-lcd_n16r8`. Its `hcpu/board.conf` names pins;
cross-referencing with wodle's *independently* recovered GPIO set corroborates:
- **PA10 = AW8155 speaker-amp enable** (`AW8155_GPIO_PIN=10`; also GPIO in wodle) **[C-RE+ref]**
- **PA44 = AW32001 charger INT** (`CHARGER_INT_PIN=44`; also GPIO in wodle) **[C-RE+ref]**
- **PA34 = KEY1 / power button** (`KEY1_PIN=34`; matches wodle + datasheet) **[C-RE+ref]**

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
| **PA10** | `AUDIO_PA_CTRL` | = AW8155 enable (also board.conf) | C-RE+ref |
| PA18/19 | UART1 debug | matches wodle console | C-RE+ref |
| **PA20/PA27** | `USART2` log UART | possible 2nd UART on wodle (unverified) | ref |
| **PA24/25/28/29** | `SPI1 (TF card)` | **identical to wodle SPI1 → SPI1 = microSD, CONFIRMED** | C-RE+ref |
| PA34 | Key1 power (kept PD for UART-download) | matches wodle | C-RE+ref |
| **PA35/PA36** | `USB_DP/USB_DM` (analog) | the USB port (charge/MSC) | ref |
| **PA44** | `VBUS_DET` | = charger/USB detect (board.conf CHARGER_INT) | C-RE+ref |
| touch | DevKit: RESET=PA09, INT=PA31, I2C1=PA30/33 | **wodle REWIRED** (PA31 = wodle I2C2_SCL) → wodle touch INT/RESET are elsewhere | ⚠ differs |

**Key takeaways:** (1) wodle is a DevKit derivative but the vendor **rewired I2C and touch control pins**
— so use wodle's *recovered* map, not the DevKit's, for those. (2) wodle runs the LCD in **2-data-lane**
QSPI (e-paper needs less bandwidth than the CO5300 AMOLED), which frees PA07/PA08 for I2C1. (3) SPI1 =
microSD is now confirmed. (4) PA00 = display reset.

## I²C device map (`tools/fw_xref.py`)

Each driver's init function references exactly one bus-name string → solid bus assignment.

| Bus | Pins | Device | Addr (7-bit) | Tag |
|---|---|---|---|---|
| I²C1 | SCL=PA07, SDA=? (likely PA08) | CST816 touch | **0x15** (`0x2a` = 0x15<<1 seen) | C-RE |
| I²C2 | SCL=PA31, SDA=PA32 | AW32001 charger | **0x49** (seen directly) | C-RE |
| I²C2 | (same bus) | BQ27220 fuel gauge | 0x55 (part default; not seen in scan) | C-RE bus / ? addr |
| — | GPIO / I²S | AW8155 speaker amp | **not on I²C** (mode-pin controlled) | C-RE |

## Flash / partition layout (refined)

flash2 = MPI2/QSPI2, base `0x12000000`, 16 MB. FAL magic `0x45503130` found only for dfu+ble
(the rest of the layout lives in the ftab, not FAL):

| Region | Offset | Addr | Size |
|---|---|---|---|
| ftab + bootloader (+?) | 0x000000 | 0x12000000 | → 0x218000 |
| HCPU app | 0x218000 | 0x12218000 | 0x240000 |
| dfu (FAL) | 0x458000 | 0x12458000 | 16 K |
| ble NVDS (FAL) | 0x45c000 | 0x1245c000 | 16 K |
| ezip assets | 0x460000 | 0x12460000 | 0x680000 |
| font_data | 0xAE0000 | 0x12AE0000 | 0x400000 |
| FS / KVDB | 0xEE0000 | 0x12EE0000 | ~1.1 M |

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
the widest UI asset. **[C-RE]** The **EPD controller part** is still unknown — `lcd_rreg` on
hardware, or decoding the SPI init byte sequence, would name it.

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
- **Config files** (`system.cfg`, `device_config.cfg`, `network_mode.cfg`, `reading_state.cfg`) = a
  key/value settings store; **no debug-enable flag found**, and they live on an internal FS (not the
  removable SD), so not editable without console/USB-disk. **[C-RE]**
- **Recovery-SD correction**: the OTA manifest is read from **`/firmware/update.json`** (a `firmware/`
  folder on the card), not the card root. **[C-RE]**

**Tap options, ranked:** (1) finsh over UART PA18/PA19 — full shell, needs 2 wires; (2) self-hosted
XiaoZhi WebSocket server + OTA-host redirect — full MCP/IoT remote control, no wires; (3) just pull the
SD card and read its FS directly — trivial, no tap needed.

## Reference build + symbol-match

Built OpenSiFli SDK v2.5.0 hello_world for board `sf32lb52-lcd_n16r8` to (a) prove the toolchain
and (b) FLIRT-match symbols into the stock binary.

- **Toolchain PROVEN** ✅ : `install.sh` (China mirror) → `arm-none-eabi-gcc 14.2.1` + `sftool 0.1.16`
  in `~/.sifli/`; `scons --board=sf32lb52-lcd_n16r8 -j8` → `main.elf` (2.84 MB, symboled). Build
  fixes required: empty `UV_CONFIG_FILE` (override global uv `exclude-newer`); stub empty
  `middleware/bluetooth/Kconfig` + comment 2 BT lines in `board.conf`; `git submodule update --init
  tools/SiliconSchema` (ptab needs it).
- **Symbol-match (`tools/fw_symmatch.py`) = limited yield.** Stock fw (v1.4.0.0422) was built on an
  **older SDK**, so larger functions' bytes diverge from the v2.5.0 build → cross-version FLIRT is
  unreliable. Only simple functions matched (e.g. `rt_i2c_transfer` UNIQUE @0x122f8228);
  `HAL_PIN_Set` (already known behaviorally @0x1221e2c0) and `rt_pin_*` did **not**. The build's real
  payoff was the `board.conf` cross-check (resolved PA10/PA44/PA34), not the byte-match.

## Reproduce

```
uv run tools/fw_analyze.py
```
