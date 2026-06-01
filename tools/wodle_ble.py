#!/usr/bin/env python3
"""wodle BLE CLI — talk to the "AI Dou" (SiFli SF32LB52x) over BLE GATT.

The device exposes a custom 128-bit GATT service (found in firmware:
  258EAFA5-E914-47DA-95CA-C5AB0DC85B11  — SiFli "BLE command" / serial-transport).
This tool scans, dumps the GATT table, subscribes to notifications, and writes.

macOS note: Bluetooth is **silently denied under zellij/tmux/SSH**. Run from a normal
Ghostty/Terminal window (grant Bluetooth when macOS prompts). If `scan` finds nothing,
that's the most likely cause — not the device.

Usage (via uv, auto-installs bleak):
  uv run --with bleak python tools/wodle_ble.py scan
  uv run --with bleak python tools/wodle_ble.py info  <addr-or-name>
  uv run --with bleak python tools/wodle_ble.py listen <addr-or-name> [char-uuid]
  uv run --with bleak python tools/wodle_ble.py send  <addr-or-name> <char-uuid> <hex|str:text>
"""

import asyncio
import sys

from bleak import BleakClient, BleakScanner

SIFLI_SVC = "258eafa5-e914-47da-95ca-c5ab0dc85b11"  # custom service from firmware
NAME_HINTS = ("ai dou", "aidou", "wodle", "sifli", "xiaozhi", "dou")


async def _find(target, timeout=10.0):
    """Resolve a name substring or address to a BLE device."""
    print(f"scanning {timeout:.0f}s for '{target}' ...", file=sys.stderr)
    devs = await BleakScanner.discover(timeout=timeout, return_adv=True)
    t = target.lower()
    for addr, (d, adv) in devs.items():
        name = d.name or adv.local_name or ""
        if t in addr.lower() or (name and t in name.lower()):
            print(f"matched {name!r} {addr} rssi={adv.rssi}", file=sys.stderr)
            return addr
    print(f"no match for {target!r}", file=sys.stderr)
    return None


async def cmd_scan(_args):
    devs = await BleakScanner.discover(timeout=10.0, return_adv=True)
    if not devs:
        print("(no BLE devices — BT off/denied [zellij?], or nothing advertising)")
        return
    rows = sorted(devs.items(), key=lambda x: -(x[1][1].rssi or -999))
    for addr, (d, adv) in rows:
        name = d.name or adv.local_name or "?"
        hint = "  <-- candidate" if any(h in name.lower() for h in NAME_HINTS) else ""
        svcs = ",".join(adv.service_uuids or [])
        md = {k: v.hex() for k, v in (adv.manufacturer_data or {}).items()}
        print(f"{adv.rssi:>4} dBm  {name:26.26} {addr}  svc=[{svcs}] mfg={md}{hint}")


async def cmd_info(args):
    addr = args[0]
    if "-" not in addr and ":" not in addr and len(addr) < 30:
        addr = await _find(addr) or sys.exit("device not found")
    async with BleakClient(addr) as c:
        print(f"connected: {addr}\n")
        for s in c.services:
            print(f"service {s.uuid}  ({s.description})")
            for ch in s.characteristics:
                print(f"  char {ch.uuid}  [{','.join(ch.properties)}]  ({ch.description})")
                for d in ch.descriptors:
                    print(f"    desc {d.uuid} handle={d.handle}")


async def cmd_listen(args):
    addr = args[0]
    char = args[1] if len(args) > 1 else None
    if "-" not in addr and ":" not in addr and len(addr) < 30:
        addr = await _find(addr) or sys.exit("device not found")

    def on_notify(_h, data: bytearray):
        try:
            txt = data.decode("utf-8")
            print(f"<- {data.hex()}   {txt!r}")
        except UnicodeDecodeError:
            print(f"<- {data.hex()}")

    async with BleakClient(addr) as c:
        targets = (
            [char]
            if char
            else [
                ch.uuid
                for s in c.services
                for ch in s.characteristics
                if "notify" in ch.properties or "indicate" in ch.properties
            ]
        )
        for t in targets:
            try:
                await c.start_notify(t, on_notify)
                print(f"subscribed {t}", file=sys.stderr)
            except Exception as e:  # noqa: BLE001
                print(f"skip {t}: {e}", file=sys.stderr)
        print("listening (Ctrl-C to stop) ...", file=sys.stderr)
        await asyncio.Event().wait()


async def cmd_send(args):
    addr, char, payload = args[0], args[1], args[2]
    if "-" not in addr and ":" not in addr and len(addr) < 30:
        addr = await _find(addr) or sys.exit("device not found")
    data = payload[4:].encode() if payload.startswith("str:") else bytes.fromhex(payload)
    async with BleakClient(addr) as c:
        await c.write_gatt_char(char, data, response=True)
        print(f"-> {data.hex()} ({len(data)}B) to {char}")


async def cmd_probe(args):
    """Exhaustive scan; auto-connect to strong/unnamed candidates and dump their GATT.
    Finds the device even if it advertises with no name. RSSI gate avoids neighbours."""
    secs = float(args[0]) if args else 15.0
    gate = -60  # only auto-connect to very close devices (your own, in the room)
    print(f"exhaustive scan {secs:.0f}s ...", file=sys.stderr)
    devs = await BleakScanner.discover(timeout=secs, return_adv=True)
    rows = sorted(devs.items(), key=lambda x: -(x[1][1].rssi or -999))
    print(f"\n{len(devs)} devices seen:")
    cands = []
    for addr, (d, adv) in rows:
        name = d.name or adv.local_name or ""
        md = {k: v.hex() for k, v in (adv.manufacturer_data or {}).items()}
        is_apple = list(adv.manufacturer_data or {}) == [76]
        flag = ""
        named_hint = name and any(h in name.lower() for h in NAME_HINTS)
        # candidate = close + not an obvious Apple/named-earbud, OR name-hinted
        if named_hint or (adv.rssi and adv.rssi > gate and not is_apple):
            flag = "  <-- PROBE"
            cands.append((addr, name, adv.rssi))
        print(f"  {adv.rssi:>4} {name or '(no name)':24.24} {addr} svc={adv.service_uuids} mfg={md}{flag}")

    print(f"\nprobing {len(cands)} candidate(s) (RSSI>{gate} or name-matched) — GATT dump:")
    for addr, name, rssi in cands:
        print(f"\n=== connect {name or '(no name)'} {addr} rssi={rssi} ===")
        try:
            async with BleakClient(addr, timeout=12.0) as c:
                svc_uuids = []
                for s in c.services:
                    svc_uuids.append(s.uuid)
                    print(f"  service {s.uuid} ({s.description})")
                    for ch in s.characteristics:
                        print(f"    char {ch.uuid} [{','.join(ch.properties)}] ({ch.description})")
                # heuristic verdict per device
                custom = [
                    u
                    for u in svc_uuids
                    if not u.lower().startswith(
                        ("00001800", "00001801", "0000180a", "0000180f", "00001812", "00001805", "00001811")
                    )
                ]
                writable = any("write" in ch.properties for s in c.services for ch in s.characteristics)
                print(f"  >> custom services: {custom or 'NONE (standard only)'};  writable char present: {writable}")
        except Exception as e:  # noqa: BLE001
            print(f"  connect failed: {e}")


CMDS = {"scan": cmd_scan, "info": cmd_info, "listen": cmd_listen, "send": cmd_send, "probe": cmd_probe}

if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in CMDS:
        print(__doc__)
        sys.exit(1)
    asyncio.run(CMDS[sys.argv[1]](sys.argv[2:]))
