# wodle custom-framework — runbook & status

Goal: build your own firmware for the wodle (SF32LB52x N16R8, 528×792 e-paper) and deploy it
**with no wires** via the microSD `tf_ota` loop.

## Proven so far (offline, this Mac)

- **Toolchain** ✅ — OpenSiFli SDK v2.5.0 installed at `~/.sifli` (`arm-none-eabi-gcc 14.2.1`,
  `sftool`). `scons --board=… -j8` builds a symboled `main.elf`. Setup gotchas in
  `firmware-analysis.md` (uv `exclude-newer`, BT Kconfig stub, `SiliconSchema` submodule).
- **Custom board** ✅ — `board/wodle/` (canonical copy in repo; lives in SDK at
  `customer/boards/wodle/`) with `ptab.yaml` linking the HCPU app at **`0x12218000`**. **Build
  verified**: `scons --board=wodle` produced `main.elf` whose LOAD segment is at `0x12218000`
  (entry `0x12237843`) — the exact address the stock vendor bootloader jumps to. The SDK's stock
  board links at `0x12020000`, which would **not boot** on wodle. Canonical pinmux =
  `docs/recovered_pinmux.c`. (To build, the `board/wodle/` dir must be in `customer/boards/`; note
  the SDK has a duplicate-symbol bootloader-link quirk if two near-identical boards coexist — keep
  only one, or ignore since tf_ota needs the app only, not the bootloader.)
- **Flash loop** ✅ — `tf_ota` reads `/firmware/update.json` + 3 bins from the SD card, verifies
  version+CRC32, writes to flash, reboots. Deploy = copy files to card. Recover = restore stock files.
  Stock 0246 backed up at `refs/card_snapshot_0246/`; newer 0422 staged on the card.

## The no-wire dev cycle (once you have a bootable app)

```
edit → scons --board=wodle → build_wodle_hcpu/<app>.bin
  → bump "version" in update.json, set hcpu_app crc32+size
  → copy bin + update.json into card /firmware/
  → card into wodle, power on → it self-flashes
recover if it doesn't boot: restore refs/card_snapshot_0246/firmware/ (or Downloads 0422) to card /firmware/
```

## Blockers before a *custom* app can work on hardware (need your help)

1. **EPD controller is unidentified.** The screen is e-paper at 528×792 over LCDC1 2-lane QSPI
   (CS=PA03 CLK=PA04 D0=PA05 D1=PA06, RESET=PA00, backlight PWM=PA01), but we don't know the
   controller chip → can't write a working display driver yet. **Needs:** `lcd_rreg` over the UART
   console, or you tell me the panel marking.
2. **No debug feedback without UART.** A custom app that mis-boots is invisible (no screen driver, no
   serial). **Needs:** USB-UART on **PA18(RX)/PA19(TX)** for the finsh console + logs. This also
   completes the last unknown GPIOs (EPD BUSY/DC, touch INT/RST among PA21/33/38/42/43).
3. **Vendor-bootloader compatibility unverified.** Our app links to 0x12218000, but whether the
   stock bootloader inits clocks/PSRAM compatibly for an SDK-v2.5.0 app is unproven. **De-risk by**
   flashing the official **0422 first** (already staged) to confirm the loop end-to-end.

## Recommended order

1. **Flash 0422 now** (card is staged + ejected) → reinsert into wodle, power on → confirms the SD
   flash loop works on your unit. Zero risk (official newer firmware).
2. **Wire USB-UART to PA18/PA19** → finsh console: run `lcd_rreg`/`epd_stat` (EPD controller +
   resolution), `pin`/`list_device` (finish pin map), `i2c` scan (confirm addresses).
3. Then a custom **minimal app** (boot + LED/GPIO toggle, or EPD "hello") becomes a real, testable
   target and I can write the board's display + input drivers.
