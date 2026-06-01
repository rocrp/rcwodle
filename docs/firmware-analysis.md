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
| PA34 | GPIO | PD | **Power / reset button** (datasheet: PA34 long-press reset) | C-RE |
| PA00, PA10, PA21, PA33, PA38, PA42, PA43, PA44 | GPIO | mixed | control / IRQ lines — EPD BUSY/RST/DC, touch INT/RST, power-EN, LED, etc. | ? |

**Buses summarised:**
- **E-paper panel** → LCDC1 in 2-data-lane SPI: `CS=PA03, CLK=PA04, D0=PA05, D1=PA06` (+ RST/DC/BUSY among the GPIOs above).
- **NOR flash 16 MB** → MPI2/QSPI2 (`0x12000000`): `CS=PA12, CLK=PA16, D0=PA15, D1=PA13, D2=PA14, D3=PA17`.
- **Console/debug UART** → USART1: `RX=PA18, TX=PA19` (also the SWD pair per datasheet).
- **I²C1** `SCL=PA07` (SDA `[?]`) · **I²C2** `SCL=PA31, SDA=PA32`. Which of CST816 / AW32001 / BQ27220 / AW8155 sits on which bus = `[?]` (needs live `i2c` scan or deeper RE).
- **SPI1** `CLK=PA28, CS=PA29, DIO=PA24, DI=PA25` — peripheral `[?]`.

**Limits (honest):** static recovery only captures `HAL_PIN_Set` calls with constant args, so
this map is high-confidence but **partial**. Missing: I²C1 SDA, the per-GPIO purposes
(EPD/touch control lines), and any pin set via computed args. The live finsh `pin` /
`list_device` dump (UART on PA18/PA19) would complete and cross-check it.

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

## Reproduce

```
uv run --with capstone python tools/fw_analyze.py
```
