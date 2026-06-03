# wodle schematic — official pin map (ground truth)

`wodle_sch_1of2.png` — sheet **1 of 2** of the official "小豆子 (ai_dou)" schematic, from the
vendor dev package `小豆子开发环境包20260530` (2026-05-30). Sheet 1 is the **SF32LB525UC6 pin
assignment** (the SoC and every net leaving it). Sheet 2 (power tree / modem / connectors) was
**not** included.

This **supersedes** the binary-recovered pin map in
[`../../docs/firmware-analysis.md`](../../docs/firmware-analysis.md): every net→PAxx mapping below is
read directly off the schematic (tag **[C-sch]**). It confirms the recovered LCDC1/flash/UART/I²C2/SPI1
pins and resolves the formerly-unknown GPIOs (touch INT, NFC, card-detect, modem UART, key inputs).

## SF32LB525UC6 net → pin map

**Display / touch / power / flash / console (PA0–PA23)**

| PA | SoC default alt-func | wodle net | Role |
|---|---|---|---|
| PA0  | `LCD_RST` | `LCDC1_SPI_RSTB` | **EPD reset** (active-low) |
| PA1  | `BL_PWM` | `LCDC1_BLK_PWM` | **EPD frontlight PWM** |
| PA2  | `LCD_TE` / `I2S_MCLK` | `LCDC1_SPI_TE` | EPD tearing-effect — **likely the EPD BUSY input** *(inferred; no separate BUSY net on sheet 1)* |
| PA3  | `LCD_CS` | `LCDC1_SPI_CS` | **EPD** chip-select |
| PA4  | `LCD_CLK` | `LCDC1_SPI_CLK` | EPD SCK |
| PA5  | `LCD_D0` | `LCDC1_SPI_DIO0` | EPD SDA (data 0) |
| PA6  | `LCD_D1` / `8080_DC` | `LCDC1_SPI_DC` | EPD D/C |
| PA7  | `LCD_D2` / `PDM_CLK` | `TP_SCL` | **Touch I²C (I²C1) SCL** |
| PA8  | `LCD_D3` / `PDM_DATA` | `TP_SDA` | Touch I²C (I²C1) SDA |
| PA9  | `LCD_VDD_EN` | `CAT1_PWR_EN` | **4G modem power-domain enable** |
| PA10 | `AU_PA_EN` | `PWR_EN` | **system / main power enable** |
| PA11 | `MPI2_VDD_EN` / `SD_VDD_EN` | `PA_EN` | **audio power-amp (AW8155) enable** |
| PA12 | `MPI2_CS` / `SD_D2` | `MPI2_QSPI_CS` | **NOR flash** CS |
| PA13 | `MPI2_D1` / `SD_D3` / **`BOOT_STRAP[1]`** | `MPI2_QSPI_IO1` | NOR flash D1 + boot strap 1 |
| PA14 | `MPI2_D2` | `MPI2_QSPI_IO2` | NOR flash D2 |
| PA15 | `MPI2_D0` / `SD_CMD` | `MPI2_QSPI_IO0` | NOR flash D0 |
| PA16 | `MPI2_CLK` | `MPI2_QSPI_CLK` | NOR flash CLK |
| PA17 | `MPI2_D3` / `SD_D1` / **`BOOT_STRAP[0]`** | `MPI2_QSPI_IO3` | NOR flash D3 + boot strap 0 |
| PA18 | `DBG_UART_RXD` / `SWDIO` | `DBG_UART1_RX` | **console / SWD** |
| PA19 | `DBG_UART_TXD` / `SWCLK` | `DBG_UART1_TX` | console / SWD |
| PA20 | `VIB_PWM` | `CAT1EN` | **4G modem enable** |
| PA22 | `XTAL32K_XI` | `Y1` | 32.768 kHz RTC crystal |
| PA23 | `XTAL32K_XO` | `Y1` | 32.768 kHz RTC crystal |

*(PA21 is not routed on sheet 1.)*

**microSD / 4G UART / sensor I²C / NFC / USB / keys (PA24–PA44)**

| PA | SoC default alt-func | wodle net | Role |
|---|---|---|---|
| PA24 | `SPI1_DIO` / `I2S_MCLK` / `GPS_EN` | `SPI1_DIO` | **microSD (TF)** data IO |
| PA25 | `SPI1_DI` / `I2S_SDO` | `SPI1_DI` | microSD data in |
| PA26 | `NFC_I2C_SDA` / `GPS_UART_TXD` | `UART2_RX` | **4G modem (CAT1) USART RX** |
| PA27 | `NFC_I2C_SCL` / `GPS_UART_RXD` | `UART2_TX` | 4G modem (CAT1) USART TX |
| PA28 | `SPI1_CLK` / `I2S_SDI` / `GPADC_CH1` | `SPI1_CLK` | microSD CLK |
| PA29 | `SPI1_CS` / `I2S_BCK` / `GPADC_CH2` | `SPI1_CS` | microSD CS |
| PA30 | `I2S_LRCK` / `GPADC_CH3` | `IIC_INT` | **sensor I²C (I²C2) interrupt** |
| PA31 | `GPADC_CH4` | `IIC_SCL` | **sensor I²C2 SCL** (charger + gauge) |
| PA32 | `GPADC_CH5` | `IIC_SDA` | sensor I²C2 SDA |
| PA33 | `GPADC_CH6` | `TFDET` | **microSD card-detect** |
| PA34 | `LONGPRESS_RST` / `GPADC_CH7` | `PWRKEY` | **power key (KEY1) / long-press reset** |
| PA35 | `USB_DP` | `USB_DP` | USB D+ (HVR1 recovery CDC) |
| PA36 | `USB_DM` | `USB_DM` | USB D− |
| PA37 | `SPI2_DIO` / `8080_D2` | `SPI2_DIO` | **NFC** data IO |
| PA38 | `SPI2_DI` | `SPI2_DI` | NFC data in |
| PA39 | `SPI2_CLK` / `8080_D3` | `SPI2_CLK` | NFC CLK |
| PA40 | `SPI2_CS` / `8080_D4` / `XRST` | `SPI2_CS` | NFC CS |
| PA41 | `8080_D5` | `PWR_INT` | **charger / PMIC interrupt** |
| PA42 | `8080_D6` | `TP_INT` | **touch interrupt** |
| PA43 | `8080_D7` | `KEY2` | button |
| PA44 | — | `KEY3` | button |

Sheet notes: *"All GPIO can assign to UART, I2C, T1…"* and *"Recommend PA39~42 to I2C."*

## What this changes vs the RE map

- **Confirms** LCDC1 EPD (PA3–6), NOR flash MPI2 (PA12–17, straps PA13/PA17), console UART1
  (PA18/19), sensor I²C2 (SCL PA31 / SDA PA32), and SPI1 = microSD (PA24/25/28/29).
- **Resolves** the formerly-unknown GPIOs: EPD `RST=PA0`, `TE=PA2`; touch `I²C1 = SCL PA7 / SDA PA8`,
  `INT=PA42`; `TFDET=PA33`; `PWRKEY=PA34`; keys `KEY2=PA43 / KEY3=PA44`; **NFC on SPI2 (PA37–40)**;
  **4G modem on UART2 (PA26/27)** with enables `CAT1_PWR_EN=PA9` + `CAT1EN=PA20`.
- **Corrects** two earlier RE inferences that had borrowed the DevKit `board.conf`:
  - audio PA-enable is **PA11** (`PA_EN`), not PA10 — **PA10 = system `PWR_EN`**.
  - charger/PMIC INT is **PA41** (`PWR_INT`), not PA44 — **PA44 = KEY3 button**.

Still open (sheet 2 / hardware): EPD BUSY net (likely PA2/TE), PA21's role, the audio mic/codec
routing, and the modem/power tree.
