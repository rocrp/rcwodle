# wodle custom-framework — runbook & status

Goal: build your own firmware for the wodle (SF32LB525 N16R8, 528×792 e-paper) and deploy it
**with no wires** via the microSD `tf_ota` loop.

## Big lever: start from upstream source, not clean-room

The stock app is a fork of **[`78/xiaozhi-sf32`](https://github.com/78/xiaozhi-sf32)** on board
`sf32lb52-lcd_n16r8` (see [`README.md`](../README.md); the vendor's reference SDK is SiFli's official
fork [`OpenSiFli/xiaozhi-sf32`](https://github.com/OpenSiFli/xiaozhi-sf32), same board). That repo is
the **real XiaoZhi app source** — audio (Opus + 3A), BT-PAN networking, MCP server, LVGL UI, state
machine, OTA. So a custom framework has two clean entry points:

- **Reuse the upstream app**, re-targeted to `board/wodle/` (link @ `0x12218000`) — swap only the
  hiveton-specific bits: the **e-paper display driver** (upstream is ST7789/CO5300; wodle is a
  **UC8179C** EPD — command set + LUTs in [`../refs/epd/`](../refs/epd/)) and the **reader/books UI**.
  Everything else (BT-PAN, audio, cloud protocol) is already written.
- **Clean board bring-up** (own RT-Thread app) using `board/wodle/` — more control, but you re-implement
  the XiaoZhi stack.

The former **gating unknowns are now closed by the official package**: EPD controller (UC8179C) and the
full pin map (`../refs/schematic/`). A UART console is still worth wiring for boot logs and to settle
the EPD BUSY line — but the upstream-reuse path is now unblocked.

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
edit → scons --board=wodle → build_wodle_hcpu/output/main.bin   (links @ 0x12218000)
  → cp output/main.bin <dir>/hcpu_app.bin
  → uv run tools/mk_update.py <dir> --version V1.4.0.900N      (regenerates update.json: crc32+size)
  → copy <dir>/{hcpu_app.bin,update.json} into card /firmware/
  → card into wodle, power on → it self-flashes (only if version is newer — bump N each time)
recover if it doesn't boot: restore refs/card_snapshot_0246/firmware/ (or Downloads 0422) to card /firmware/
```

> Our own validation firmware lives in [`firmware/hello_wodle/`](../firmware/hello_wodle/) — start there,
> not a clean-room rewrite of the xiaozhi app. `tools/mk_update.py` regenerates `update.json` from the
> built `.bin` (crc32+size verified against the stock manifest), so no manual CRC.

## Blockers before a *custom* app can work on hardware

1. ~~**EPD controller is unidentified.**~~ **RESOLVED — UltraChip UC8179C** (3.68″ 528×792), wired to
   LCDC1 SPI (`CS=PA03 CLK=PA04 SDA(DIO0)=PA05 DC=PA06 RST=PA00`, frontlight `PWM=PA01`). Full init
   sequence + GC/DU/4-gray LUTs in [`../refs/epd/`](../refs/epd/) → the display driver can be written
   now. One detail to confirm on hardware: the **BUSY** net (likely PA02/TE) and the exact TRES (792×528).
2. **No debug feedback without UART** *(reduced — still nice-to-have).* The pin map is now known from the
   schematic, so a custom app no longer flies blind on pins; but a mis-boot is still invisible without a
   screen or serial. **Wire USB-UART on PA18(RX)/PA19(TX)** for boot logs + to confirm the EPD BUSY line.
3. **Vendor-bootloader compatibility unverified.** Our app links to 0x12218000, but whether the
   stock bootloader inits clocks/PSRAM compatibly for an SDK-v2.5.0 app is unproven. **De-risk by**
   flashing the official **0422 first** (already staged) to confirm the loop end-to-end.

## Recommended order

1. **Flash 0422 now** (card is staged + ejected) → reinsert into wodle, power on → confirms the SD
   flash loop works on your unit. Zero risk (official newer firmware).
2. **Write the UC8179C driver** (LCDC1 SPI; `refs/epd/` command set + LUTs) and **wire USB-UART to
   PA18/PA19** → use `lcd_rreg`/`epd_test` + boot logs to confirm BUSY + TRES against the real panel.
3. Then a custom **minimal app** (boot + EPD "hello") becomes a real, testable target — display + input
   drivers can be written straight from the schematic pin map.
