# EPD controller reference — UC8179C (+ UC8279 4-gray)

Vendor-supplied panel-driver reference code from the official dev package
`小豆子开发环境包20260530` (2026-05-30). This **names the e-paper controller** — formerly the single
biggest hardware unknown (the firmware's `st7789`/`CO5300` strings are upstream leftovers, not the
real part).

| File | Panel / controller | Modes |
|---|---|---|
| `UC8179C_3.68in_528x792_reference.c` | **3.68″ 528×792, UltraChip UC8179C** | B/W with `GC` (full/quality) + `DU` (fast/partial) LUTs |
| `UC8279_4gray_reference.c` | UltraChip **UC8279** (same UC82xx family) | **4-level grayscale** (2 bpp) |

> The two are command-compatible UC82xx parts. wodle's panel = **UC8179C, 528×792**; the firmware
> drives **4-level gray + partial/full refresh + BUSY** (matches these references). The UC8279 file is
> the 4-gray waveform/encoder reference for that mode.

⚠ The reference is **STM32 eval code** (bit-banged GPIO on `GPIOD`/`GPIOE`) — ignore its pin numbers.
On wodle the same controller is driven by the **SF32 LCDC1 in SPI mode**; for the real wiring see
[`../schematic/README.md`](../schematic/README.md): `CS=PA3 CLK=PA4 SDA(DIO0)=PA5 DC=PA6 RST=PA0`,
frontlight `PWM=PA1`, and BUSY most likely on `PA2` (TE). Take only the **command set + LUTs** from here.

## Decoded command set (UC8179C)

| Cmd | Name | Reference payload | Note |
|---|---|---|---|
| `0x00` | Panel Setting (PSR) | `0x3F 0x4A` | LUT-from-register, B/W |
| `0x01` | Power Setting | `0x03 0x00 0x78 0x78 0x17` | VGH/VGL/VDH/VDL |
| `0x03` | — | `0x10` | |
| `0x06` | Booster Soft-Start | `0x25 0x25 0x3C` | |
| `0x82` | VCOM DC | `0x24` | read range 0x1D–0x21 |
| `0x30` | PLL / frame rate | `0x0F` | 0x0F=80 Hz, 0x10=85 Hz |
| `0x61` | Resolution (TRES) | `0x03 0x18  0x02 0x58` | HRES=`0x0318`=**792**; VRES — see note ↓ |
| `0x65` | Scan start address | `0x00×4` | |
| `0xE1` | Gate scan mode | `0x02` | |
| `0x50` | VCOM / data interval | `0x97` (GC) / `0xD7` (DU) | per-LUT |
| `0x10` | RAM (old / B-plane) | image | 4-gray uses **both** 0x10 + 0x13 |
| `0x13` | RAM (new / W-plane) | image | B/W uses only 0x13 |
| `0x20`–`0x24` | LUT VCOM / WW / BW / WB / BB | 49 bytes each | from `LUT_GC` / `LUT_DU` / `LUT_Grey` |
| `0x12` | Display refresh | — | then wait BUSY |
| `0x04` | Power on | — | |
| `0x02` | Power off | — | |
| `0x07` | Deep sleep | `0xA5` | re-init needed after wake |

**BUSY is active-low** (`while (BUSY==0);` = wait while busy). Reset is active-low (low ≥10 ms).
Refresh cadence in the reference: ~every 10 `DU` fast refreshes do 1 `GC` full refresh; large solid
fills use `GC`.

> **VRES note:** the reference programs TRES = `792 × 600` (`0x0258`=600) yet its `#define
> Gate_Pixel 528` and the RAM loop write `792*528/8` bytes — a vendor copy-paste artifact from a
> 600-line panel. For wodle (**792×528**) program **VRES = 528 = `0x0210`**; confirm on hardware via
> `lcd_rreg`/`epd_test`.

## 4-level gray (UC8279 reference)

2 bits/pixel, packed by brightness: black `00`, dark-gray `01`, light-gray `10`, white `11`. The
encoder splits each pixel's 2 bits into two 1-bpp planes → MSB plane to `0x10`, LSB plane to `0x13`
→ then loads `LUT_Grey` (cmds `0x20`–`0x24`) and refreshes (`0x12`). Full waveform table is in the
file.
