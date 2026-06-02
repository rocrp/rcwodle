# firmware254 decode — V1.4.0.0853 (full build, not just OTA)

Source: `/Users/rocry/Downloads/firmware254/`. Unlike the earlier OTA-only packages, this is a
**complete build**: `app_new.bin`, `bootloader.bin`, `dfu_pan.bin`, `ftab.bin`, plus the OTA trio
(`hcpu_app.bin`+`ezip_image.bin`+`font_data.bin`) and `*_new.bin` resources. Version **V1.4.0.0853**
(newer than 0422). All images are real Cortex-M apps (SP=0x20002000 vector tables); none compressed.

## 1. Definitive boot chain & flash map (parsed from `ftab.bin`, magic `FCES`)

| Region | Flash addr | Size | Notes |
|---|---|---|---|
| **ftab** | `0x12000000` | 32 KB | the flash table itself |
| **LCPU / DFU image** | `0x12008000` | ~2 MB | `dfu_pan.bin` (LCPU+BT core + DFU/recovery) |
| **bootloader** | `0x12208000` | 128 KB | runs in SRAM `0x20020000` (`bootloader.bin`, 58 KB) |
| **HCPU app** | `0x12218000` | ~3.4 MB region | XIP — matches our recovered ptab ✓ |
| LCPU exec RAM | `0x20050000` | — | LPSYS RAM |

Corrects the earlier guess: the bootloader sits at **`0x12208000`** (just before the app), not 0x12010000.

## 2. Three update / communication mechanisms (cracked)

### a) SD-card OTA can now flash the WHOLE stack (bootloader-transition)
The OTA `hcpu_app.bin` (V0853, 1.1 MB) is a **transition updater**, not the real app. Its logic
(`__cmd_tf_bootloader_transition`, `[tf_ota] bootloader transition …`):
- `update.json` may carry a **`bootloader` entry** (object: name/addr/region_size/size/crc32/file_id +
  a board field + a transition marker) → reads **`/firmware/bootloader.bin`** + **`/firmware/ftab.bin`**.
- Validates **board match**, **CRC32**, and **allowed target addresses** (`target 0x%08x is not allowed`),
  then `rebooting for bootloader transition…` → flashes new bootloader+ftab, then installs the real
  app (`app_new.bin`, 2.3 MB).
- **Implication:** a *fully custom* bootloader + flash-table + app can be installed over the SD card
  (full device ownership), subject to the board-match + allowed-address guard. Big lever for a custom framework.

### b) DFU-over-PAN (how the official app pushes firmware)
`dfu_pan.bin` runs a firmware-download service over the **BT-PAN IP link**. Commands:
`dfu_pan_download_firmware`, `dfu_pan_finish`, `dfu_pan_set_update_flags`, `dfu_pan_clear_files`,
`dfu_pan_print_files`. Handshake banner: `board=sf32lb52-lcd_n16r8;proto=1;max_payload=%u;`.
(IP-over-PAN, so reaching it needs to be the PAN NAP — not scriptable on macOS, but this is the app's path.)

### c) **USB-CDC recovery** — the Mac-accessible channel ★
`dfu_pan.bin` implements `[usb_recovery] start USB CDC recovery service`: in recovery mode the device
enumerates as a **USB CDC serial port** (`/dev/cu.usbmodem*`) — directly usable from macOS (no BLE,
no PAN). **Entry: from the device's own Settings menu** (`settings: usb recovery requested`) or the
`__cmd_usb_recovery_mode` console command → writes a flag, reboots into recovery, "Connect the PC tool."

**Reverse-engineered protocol** (sequence-framed; `FRAME reject … nbytes=%u magic=0x%08x version=%u`):
- Frame = `{ magic:u32, version, …, cmd, seq:u32, payload, crc }`; per-chunk CRC (`CRC_ERROR`).
- Commands: **HELLO** (`seq, max_payload, regions` — device announces capabilities), **BEGIN**,
  **ERASE** (`part, addr, size, erase, align`), **WRITE** (chunked, CRC-checked, progress), **VERIFY**
  (`addr, size, expected` CRC), **COMMIT**, **REBOOT**.
- Status: OK / BAD_FRAME / CRC_ERROR / FLASH_ERROR.
- **Open item:** the exact `magic` u32 constant wasn't recovered statically (the recovery code's string
  addressing didn't resolve at base 0x12008000 — likely a separate segment). Recover via: deeper
  disasm (ADR/segment-aware), or capture the official PC tool's first HELLO frame over the CDC port.

## 3. `app_new.bin` = the real V0853 app
2.3 MB, `chip_model sf32lb563`, `xiaozhi.me`, `__cmd_xiaozhi2` — the actual new application installed
after the bootloader transition. (Same family as 0422; diff for new features is TODO.)

## Why this matters for "talk to the device from the computer"
USB-CDC recovery is the **tractable channel BLE never was**: enter it from the device's Settings menu
(no wires, no pairing), it appears as a normal serial port on the Mac, and it speaks a documented
flash protocol (read/erase/write/verify/reboot). A Python CLI over `pyserial` can drive it once the
frame magic is known. This is a better answer to the original goal than BLE/SPP.

## Tools
`tools/fw_usbrec.py` (frame-format recovery, WIP), plus the existing `fw_*` analyzers reusable on
`app_new.bin`/`bootloader.bin`.
