# wodle CLI flasher — design

Date: 2026-06-02
Status: approved (design)

## Goal

A pure command-line tool to flash the wodle board over USB recovery, built from the
**vendor ground truth** (`tools/usb_recovery_qt/main.cpp`, the Hiveton GUI). Supersedes
`tools/wodle_usbrec.py`, whose statically-reverse-engineered ERASE/VERIFY frames place
fields wrong (see below) and which is untested against hardware.

Transport: USB-CDC port that enumerates after the device enters recovery mode
(Settings → "USB recovery", or finsh `usb_recovery_mode`). No BLE/UART/BT-PAN.

## Protocol "HVR1" (corrected from vendor)

Serial: **baud 1000000, 8 data bits, no parity, 1 stop, no flow control.**

Port detect: USB VID `0x38f4` / PID `0x1001` (PID optional); fallback glob `/dev/cu.usbmodem*`.

Frame = 24-byte header + payload, all little-endian (`struct <IBBHIIII>`):

```
magic u32 = 0x31525648 "HVR1" | version u8 = 1 | cmd u8 | seq u16
addr u32 | size u32 | crc u32 | value u32 | payload[size]
```

CRC = standard zlib CRC32 (poly 0xEDB88320, reflected, init/xorout 0xFFFFFFFF).

Commands (cmd): 1 HELLO, 2 BEGIN, 3 ERASE, 4 WRITE, 5 VERIFY, 6 COMMIT, 7 REBOOT.
Status (reply `value` field): 0 OK, 1 BAD_FRAME, 2 DENIED, 3 FLASH_ERROR, 4 CRC_ERROR, 5 BAD_PARTITION.

**Per-command field usage** — `value` (8th u32) is always 0 from host; the device returns
its status there. This is the table the old Python tool got wrong:

| cmd | addr | size | crc (7th u32) | payload |
|---|---|---|---|---|
| HELLO / BEGIN / COMMIT / REBOOT | 0 | 0 | 0 | — |
| ERASE | region addr | **region size** | 0 | — |
| WRITE | chunk addr | chunk len | crc32(chunk) | chunk, ≤ 2048 B |
| VERIFY | region addr | **region size** | **expected crc32** | — |

(Old `wodle_usbrec.py` put ERASE size and VERIFY crc in the `value` field with `size`=0 —
device would reject as BAD_FRAME.)

Reply framing (robust reader, ported from vendor `transact`): read until magic appears,
parse 24-byte header, require `version`==1, payload length ≤ 2048, payload CRC matches,
and `cmd`+`seq` match the request — discard stale/mismatched frames, time out otherwise.

HELLO reply payload is `;`-separated `k=v` (`board=...;proto=...;max_payload=...`).
On a VERIFY CRC mismatch the device returns status CRC_ERROR with its actual CRC in the
reply payload (first LE u32).

Timeouts (mirror vendor): HELLO 5s, ERASE 35s, WRITE 6s/chunk, VERIFY 45s, REBOOT 3s.

## Manifest (`update.json`)

Same format the device's SD-card OTA and the GUI consume
(`refs/card_snapshot_0246/firmware/update.json`):

```json
{ "version": "...", "generated_at": "...",
  "files": [ { "name": "hcpu_app.bin", "addr": "0x12218000",
               "region_size": "0x00240000", "size": 2142852, "crc32": "0xE6A5825E" } ] }
```

`addr`/`region_size`/`crc32` accept hex strings or ints (`int(x, 0)`). `name` resolves
relative to the manifest dir. Partition derived from addr/name:
`app`=0x12218000, `ezip`=0x12460000, `font`=0x12AE0000; flashed in order **app → font → ezip** (vendor rank).

Local validation before any write: on-disk size == `size` and crc32 == `crc32`; else abort.

## Flash sequence (`flash` command)

```
HELLO → BEGIN → for each file (app, font, ezip):
    ERASE(addr, region size) → WRITE chunks (2048 B) → VERIFY(addr, region size, crc32)
→ COMMIT → REBOOT
```

`--verify-only` skips BEGIN/ERASE/WRITE/COMMIT and only re-verifies (write=false path in vendor).

## CLI surface (typer)

`uv run --with pyserial --with typer python tools/wodle_flash.py ...`

- `flash <manifest>` — full sequence above. Flags: `--port`, `--dry-run` (print plan, no port),
  `--verify-only`, `--no-reboot`, `--yes` (skip the destructive-op confirm), `--only app,font`.
- low-level verbs for bring-up: `hello`, `erase --addr --size`, `write --addr --file`,
  `verify --addr --file`, `commit`, `reboot`. Shared `--port`.

Default `flash` reboots on success (job done = device running new firmware); `--no-reboot`
keeps it in recovery for re-verify.

## Safety

- Local size+CRC check before writing.
- Confirm prompt before the first erase/write unless `--yes`.
- `--dry-run` prints the full planned frame sequence without opening the port — important
  while the protocol is still hardware-unproven.

## Testing

`tools/test_wodle_flash.py` with an in-memory `FakeDevice` serial stub:
- frame encode/decode round-trip,
- manifest parse (hex + int, partition mapping/order, bad-CRC rejection),
- full `flash` sequence drives the expected HELLO→…→COMMIT frames with correct fields.

Hardware flash is the real validation; the stub catches encoding/sequence regressions offline.

## File layout / supersede

- add `tools/wodle_flash.py` (~300 LOC), `tools/test_wodle_flash.py` (~80 LOC)
- delete `tools/wodle_usbrec.py` (buggy + redundant)
- `tools/fw_usbrec.py` (static disassembly) and the Qt GUI are unaffected.

## Out of scope (YAGNI)

Building `update.json` from raw `.bin`s, multi-device, OTA-over-BLE, non-macOS port globs.
