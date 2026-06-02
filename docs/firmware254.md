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

**Reverse-engineered protocol — FULLY CRACKED 2026-06-02** (static RE of `dfu_pan.bin`; validator
fn @ `0x1200f914`, dispatch tbh @ `0x1200f998`, CRC @ `0x1200f4e0`). The earlier blocker (frame magic)
is solved: previous tool anchored *mid-string*, but code loads the format string's **start**; fixing the
anchor + walking `push.w` prologues exposed the `cmp` against the magic constant.

**Frame magic = `0x31525648` = ASCII `"HVR1"`** (little-endian on the wire: bytes `48 56 52 31`).
Validator: `cmp nbytes,#0x17` (≥24) · `ldr [frame]==0x31525648` · `frame[4]==1` (version) → else
`FRAME reject`. **`HVR1`** ≈ *Hiveton Recovery v1* (vendor = hiveton).

**Unified 24-byte header** (same layout both directions), payload follows:

| off | sz | field | meaning |
|----|----|-------|---------|
| 0  | 4  | magic   | `0x31525648` `"HVR1"` (LE) |
| 4  | 1  | version | `1` |
| 5  | 1  | cmd     | 1=HELLO 2=BEGIN 3=ERASE 4=WRITE 5=VERIFY 6=COMMIT 7=REBOOT |
| 6  | 2  | seq     | u16, echoed in response |
| 8  | 4  | addr    | flash target addr (ERASE/WRITE/VERIFY) |
| 12 | 4  | size    | length; for WRITE = payload length (must == nbytes−24, ≤ `0x800`=2048) |
| 16 | 4  | crc32   | CRC-32 of payload (WRITE expected / response actual) |
| 20 | 4  | status  | response status code (request: aux/unused) |
| 24 | n  | payload | ≤ 2048 bytes |

**CRC = standard CRC-32** (poly `0xEDB88320` reflected, init `0xFFFFFFFF`, xorout `0xFFFFFFFF`) →
exactly Python `zlib.crc32(payload)`. Confirmed from the bitwise routine at `0x1200f4e0`.

**Status codes** (table @ `0x12121f28`): `0 OK · 1 BAD_FRAME · 2 DENIED · 3 FLASH_ERROR · 4 CRC_ERROR · 5 BAD_PARTITION`.
`DENIED` = addr is `0x12000000` (ftab) or `0x20020000` (bootloader SRAM) — the "target not allowed" guard
(`0x1200f558` validates the flash addr/partition before erase/write).

**Command behaviour:**
- **HELLO** → device replies banner `board=sf32lb52-lcd_n16r8;proto=1;max_payload=2048;` + up to **3
  writable regions** appended as `%s=0x%08x:%u;` (name=addr:size); `regions=3`, `max_payload=0x800`.
- **BEGIN** → opens a session. **COMMIT** → finalizes. **REBOOT** → reboots ~150 ms later.
- **ERASE** (addr,size) → sector-aligned erase; emits `ERASE start/done … align=…`.
- **WRITE** (addr,size,crc) → rejects `BAD_FRAME` if `size!=nbytes-24` or `size>2048`; rejects
  `CRC_ERROR` if `zlib.crc32(payload)!=crc`; else programs flash, emits `write progress … %u%%`.
- **VERIFY** (addr,size,expected_crc) → reads back, CRCs, compares.

**Transport:** device assembles a frame into buffer `0x2001efc0` (len counter `0x2001e7a4`), signals the
worker, then validates. `RX error len=%u expected=%u` ⇒ a length-prefixed read (header→payload). Host
side: send the whole frame in one write; read responses by syncing on `HVR1`, reading 24-byte header,
then `size` payload bytes. (One transport nuance — header-then-payload split vs single write — is the
only thing left to confirm live; `tools/wodle_flash.py` sends contiguous, the robust choice.)

**Entry:** device Settings → "USB recovery", or finsh `usb_recovery_mode` → reboots into CDC; appears as
`/dev/cu.usbmodem*` on macOS. Drive with **`tools/wodle_flash.py`** (pyserial, no BLE/UART/PAN needed).

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
