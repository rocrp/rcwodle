# xiaodouzi_demo — community "小豆子" SF32LB52 demo (decoded notes)

User-supplied bundle (`my_project.zip`, 2026-06-07): a working community demo for the same
"AI Dou"/小豆子 hardware family — SF32LB52x + UC8179C 528×792 EPD + CST836U-class touch +
BQ27220 gauge + AW32001E charger + AHT20. Second independent hardware-proven reference after
`refs/spi_epd_demo` (the vendor-quality one). `src/` = the demo's live sources verbatim
(`.bak`/`.clean` variants, `u8g2/` vanilla upstream lib, and build output dropped);
`UPSTREAM_README.md` = its own (Chinese) module docs.

## ⚠ Pin-map caveat — DevKit pins, not wodle

The demo wires per the `sf32lb52-lcd_n16r8` DevKit reference, which **differs from wodle**
(`refs/schematic/` is the wodle authority): demo `KEY2=PA11 / VBUS_DET=PA44 / PA10=audio-PA` vs
wodle `KEY2=PA43 / KEY3=PA44 / PA10=PWR_EN / charger-INT=PA41`. Everything that overlaps wodle
(EPD PA0-6, touch I2C1 PA7/8, frontlight PA1, PWRKEY PA34, PWR_EN-as-latched-high PA10, sensor
bus PA31/32, SD pins PA24/25/28/29) **matches**.

## What crosspoint ADOPTED from it (2026-06-07)

| Finding | Where it landed |
|---|---|
| **PWRKEY PA34 is active-HIGH** (GPIO_PULLDOWN + raw==1 = pressed; wake armed `AON_PIN_MODE_HIGH`). 4th + decisive evidence source — crosspoint's PWR was blind-assumed active-low (= dead power button). | `port/hal/HalGPIO.cpp` readRaw/begin |
| **Hibernate recipe** (`power.c`): arm wake pin0 level-HIGH, `WKUP_CNT=0x000F000F`, quiesce PA24–PA44 as pulled-down GPIOs, LDO18+LDO2+LDO3 off, `EnterHibernate`. Drain the button release before arming (level wake + held button = insta-rewake; the demo waits for release in its key thread). | `HalGPIO::startDeepSleep` (PA31/32 exempted to high-Z — their pullups hang off the always-on charger rail) |
| **BQ27220 ships unconfigured** on this family: demo unseals + forces `DesignCapacity=850mAh` every boot and computes SOC as RM/FCC instead of trusting the SOC register. | Read-only config snapshot logged in `WodleBattery::init`; programming decision = HIL |
| **RTC BKP0R is clobbered** by `HAL_PMU_EnterHibernate` (BOOTOPT defaults, the demo's "0x5050" note) → demo persists to BKP1. For crosspoint BKP1–4 belong to drv_rtc (LPCYCLE/INITIALIZED) — **no BKP is free for app data**. | Comment in `startDeepSleep`; we persist to SD instead |

## Recorded as HIL knobs (not adopted)

- **Gray4 inverse convention**: byte-identical 245-byte LUTs (GC/DU/GREY — good triple
  cross-validation with spi_epd_demo + crosspoint), but the demo writes `0x50=0x00` before the
  gray banks and encodes `(0,0)=white/(1,1)=black` — the inverse of spi_epd_demo/ours. DDX bits
  in 0x50 plausibly make both equivalent. Fallback pair if HIL item 11b shows inverted grays.
- **Frontlight 1kHz + 30–70% duty** (GPTIM1_CH4 PA1, register-level): boost lights from ~30%
  duty at 1kHz → dimmer minimum than our 5kHz/50% floor. Knob in `WodleFrontlight.cpp`.
- **SD bit-bang fallback** (`sd_spi.c`, 212 lines): pure-GPIO SPI mode 0 on PA24/25/28/29 with
  full CMD0/CMD8/ACMD41/CMD58/CMD16 init + sector R/W. The demo claims "SPI1 HAL polling wedges"
  — but the stock wodle fw drives SD over hardware SPI1, so this is a driver-behavior warning,
  not a hardware conflict. Emergency recipe if spi_msd misbehaves at HIL item 3 (note: bit-bang
  is far too slow as a crosspoint primary — reader streams from SD constantly).
- **Non-blocking refresh** (`epd_render_partial_start`/`_poll` + PSRAM pending copy): demo's main
  loop keeps running during EPD refresh. Crosspoint is synchronous by upstream design; revisit
  only if HIL shows UI latency pain.
- `epd_render` writes the just-refreshed frame back to 0x10 after every refresh (old-frame sync)
  — same pattern crosspoint already uses.

## Rejected

- BKP1 brightness persistence (collides with drv_rtc, above).
- Demo's `PM_SHUTDOWN_BOOT`-based wake detection — crosspoint reads PMU WSR PIN0 (finer).
- Dynamic touch min/max auto-calibration — crosspoint uses fixed mapping + SWAP/MIRROR flags.
