# hello_wodle — our own minimal validation firmware

Clean-room RT-Thread app for the wodle (SF32LB525, `board/wodle`), built on the SiFli-SDK — **not**
the xiaozhi app. Brings the device up **simplest-first**; see the design spec
[`docs/superpowers/specs/2026-06-03-wodle-eink-validation-firmware-design.md`](../../docs/superpowers/specs/2026-06-03-wodle-eink-validation-firmware-design.md)
and plan [`docs/superpowers/plans/2026-06-03-wodle-bringup-part1-proof-of-life.md`](../../docs/superpowers/plans/2026-06-03-wodle-bringup-part1-proof-of-life.md).

**Stage now: S1 — proof-of-life.** `main()` asserts `PWR_EN` (PA10) to hold power, blinks the
frontlight (PA1) ~1 Hz, and prints an incrementing `[hello_wodle] hb N` heartbeat to UART1. If it
blinks/prints, a custom SDK-built app boots past the stock bootloader (answers secure-boot +
SDK/bootloader compatibility). S2 (buses/sensors) and S3 (UC8179C eink) come next.

## One-time setup

The board def is single-sourced from this repo into the SDK via a symlink:
```bash
SDK=~/w/_hw/SiFli-SDK
rm -rf "$SDK/customer/boards/wodle"
ln -s "$(git -C . rev-parse --show-toplevel)/board/wodle" "$SDK/customer/boards/wodle"
```

## Build

```bash
source ~/w/_hw/SiFli-SDK/export.sh          # SIFLI_SDK + arm-none-eabi-gcc + sftool
cd firmware/hello_wodle/project
scons --board=wodle -j8
```

Outputs (under `build_wodle_hcpu/`, gitignored):
- `output/main.bin` — the HCPU app, flashes to **`0x12218000`** (our `hcpu_app.bin`).
- `ftab.bin` — flash table @ `0x12000000` (only for a full sftool flash; tf_ota does **not** use it).

Static gate (must hold):
```bash
arm-none-eabi-readelf -l build_wodle_hcpu/main.elf | grep -m1 -A1 LOAD   # VirtAddr 0x12218000
```

## Flash (all region-safe — only the app region is written; HVR1 denies ftab+bootloader → can't brick)

Package once (used by the GUI and the SD path), from `project/`:
```bash
cp build_wodle_hcpu/output/main.bin ../dist/hcpu_app.bin
uv run ../../../tools/mk_update.py ../dist --version V1.4.0.9001 --at 2026-06-03T00:00:00Z
```

For the USB paths, put the device in recovery first: **Settings → "USB recovery"** (or finsh
`usb_recovery_mode`) → it enumerates as `/dev/cu.usbmodem*` (VID `0x38f4`/PID `0x1001`).

1. **USB-CDC — vendor Qt GUI** `tools/usb_recovery_qt/` = the **vendor's officially-tested** HVR1
   flasher → **use this for the first real flash.** Already built:
   ```bash
   open ../../../tools/usb_recovery_qt/build/hiveton-usb-recovery.app
   ```
   Load `firmware/hello_wodle/dist/update.json` (lists the `app` partition @ `0x12218000`), verify, flash.
2. **USB-CDC — our CLI** (same HVR1 protocol, **our port — not yet hardware-proven**; use once the GUI
   has confirmed the channel):
   ```bash
   uv run ../../../tools/wodle_flash.py hello                                          # probe link
   uv run ../../../tools/wodle_flash.py write build_wodle_hcpu/output/main.bin --addr 0x12218000
   ```
3. **SD `tf_ota`** (no wires, proven by the stock updater): copy `../dist/{hcpu_app.bin,update.json}` →
   recovery card `/firmware/`, power on. Only flashes a **newer** version — bump `--version` each time.

**Restore to stock** if a build doesn't boot: re-flash `refs/card_snapshot_0246/firmware/hcpu_app.bin`
@ `0x12218000` (any channel), or copy it back to the card `/firmware/`. Keep that snapshot as the un-brick.

## SDK quirks this project works around

- **Bootloader sub-build skipped** (`AddBootLoader` commented in `project/SConstruct`): building it hits
  the SDK's duplicate-symbol link quirk, and tf_ota/HVR1 flash the app region only — the stock
  bootloader stays in flash.
- **Unique board symbol** `BSP_USING_BOARD_WODLE` (in `board/wodle/.../Kconfig.board` + `SConscript`):
  wodle was copied from `sf32lb52-lcd_n16r8` and kept its symbol, so the SDK board-walker pulled
  `sf32lb52-lcd_base` into the link twice. A unique symbol fixes it (only the active board includes the base).
- **LCD stays CO5300** for S1: the panel probe is lazy (only on opening `"lcd"`, which S1 never does), so
  it's harmless. The UC8179C driver replaces it in S3.
