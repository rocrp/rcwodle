#!/usr/bin/env python3
"""Continuously watch for the wodle to appear over BLE; the instant it does, connect,
dump its full GATT, and print a verdict on whether a usable control channel exists.

Runs hands-free: leave it running, then power-cycle the wodle or take it off the phone.
The moment it advertises, this captures the answer. Exits when the device is found.

  uv run --with bleak python tools/wodle_watch.py [max_minutes]
"""

import asyncio
import sys
import time

from bleak import BleakClient, BleakScanner

NAME_HINTS = ("ai dou", "aidou", "wodle", "sifli", "xiaozhi", "dou")
STD = ("00001800", "00001801", "0000180a", "0000180f", "00001812", "00001805", "00001811", "00001804")
# baseline of addresses already seen as "not the wodle" (neighbours / known kit) is built on first pass
baseline = set()


def is_candidate(name, adv):
    if name and any(h in name.lower() for h in NAME_HINTS):
        return True
    # new, close, non-Apple, connectable-ish unnamed device that wasn't in baseline
    apple_only = list(adv.manufacturer_data or {}) == [76]
    return (not name) and (adv.rssi or -99) > -55 and not apple_only


async def dump(addr, name):
    print(f"\n=== CAPTURED candidate {name or '(no name)'} {addr} — dumping GATT ===", flush=True)
    try:
        async with BleakClient(addr, timeout=15.0) as c:
            svc_uuids, writable, notify = [], False, False
            for s in c.services:
                svc_uuids.append(s.uuid)
                print(f"  service {s.uuid} ({s.description})", flush=True)
                for ch in s.characteristics:
                    print(f"    char {ch.uuid} [{','.join(ch.properties)}] ({ch.description})", flush=True)
                    if "write" in ch.properties or "write-without-response" in ch.properties:
                        writable = True
                    if "notify" in ch.properties or "indicate" in ch.properties:
                        notify = True
            custom = [u for u in svc_uuids if not u.lower().startswith(STD)]
            print(f"\n  VERDICT: custom_services={custom or 'NONE'}  writable={writable}  notify={notify}", flush=True)
            if custom and writable and notify:
                print(
                    "  >> USABLE CONTROL CHANNEL LIKELY — build protocol on the custom write+notify chars.", flush=True
                )
            elif name and any(h in name.lower() for h in NAME_HINTS):
                print(
                    "  >> This IS the wodle but exposes no custom write+notify service => no BLE control path.",
                    flush=True,
                )
            return True
    except Exception as e:  # noqa: BLE001
        print(f"  connect failed: {e}", flush=True)
        return False


async def main():
    max_min = float(sys.argv[1]) if len(sys.argv) > 1 else 30.0
    deadline = time.time() + max_min * 60
    rnd = 0
    while time.time() < deadline:
        rnd += 1
        devs = await BleakScanner.discover(timeout=10.0, return_adv=True)
        named = []
        for addr, (d, adv) in devs.items():
            name = d.name or adv.local_name or ""
            if rnd == 1:
                baseline.add(addr)  # first pass = current environment (device is on phone now)
                continue
            if addr in baseline and not (name and any(h in name.lower() for h in NAME_HINTS)):
                continue
            if is_candidate(name, adv):
                named.append((addr, name, adv.rssi))
        if rnd == 1:
            print(
                f"baseline: {len(baseline)} devices present now (wodle not among them). watching for new/named ...",
                flush=True,
            )
        for addr, name, rssi in sorted(named, key=lambda x: -(x[2] or -999)):
            print(f"new candidate: {name or '(no name)'} {addr} rssi={rssi}", flush=True)
            if await dump(addr, name):
                if name and any(h in name.lower() for h in NAME_HINTS):
                    print("\nDONE: wodle captured.", flush=True)
                    return
    print("watch window ended; wodle never advertised (still on the phone / not pairable).", flush=True)


asyncio.run(main())
