#!/usr/bin/env python3
"""Extract the BLE GATT database from hcpu_app.bin — definitively map what services
/characteristics the device exposes (without needing it to advertise).

Strategy:
1. Find 16-bit SIG UUIDs: GATT declaration attrs (0x2800/0x2801/0x2803/0x2902/0x2901)
   and standard service UUIDs (0x1800 GAP, 0x1801 GATT, 0x180A DIS, 0x180F battery,
   0x1812 HID, 0x1811 ANS, ...) as little-endian 2-byte.
2. Find 128-bit custom UUID byte arrays (16-byte runs with UUID-like entropy), and
   any UUID stored as ASCII string.
3. Report clustering so we can tell if there's a custom writable service.

Run: uv run python tools/fw_gatt.py
"""

import re

FW = "/Users/rocry/Downloads/firmware/hcpu_app.bin"
BASE = 0x12218000
data = open(FW, "rb").read()

SIG = {
    0x2800: "PRIMARY_SVC_DECL",
    0x2801: "SECONDARY_SVC_DECL",
    0x2802: "INCLUDE",
    0x2803: "CHAR_DECL",
    0x2900: "CHAR_EXT_PROP",
    0x2901: "CHAR_USER_DESC",
    0x2902: "CCCD",
    0x2904: "CHAR_FMT",
    0x1800: "svc:GAP",
    0x1801: "svc:GATT",
    0x1802: "svc:ImmAlert",
    0x180A: "svc:DeviceInfo",
    0x180F: "svc:Battery",
    0x1812: "svc:HID",
    0x1811: "svc:AlertNotif",
    0x1805: "svc:CurrentTime",
    0x110A: "A2DP_Source",
    0x110B: "A2DP_Sink",
    0x111E: "Handsfree",
    0x1101: "SPP",
    0x1115: "PAN_PANU",
    0x1116: "PAN_NAP",
    0x1200: "PnP_DI",
    0x2A00: "char:DeviceName",
    0x2A01: "char:Appearance",
    0x2A19: "char:BatteryLevel",
    0x2A29: "char:Manufacturer",
    0x2A24: "char:ModelNumber",
    0x2A4D: "char:HIDReport",
}

print("=== 16-bit SIG / service / char UUID counts (LE) ===")
hits = {}
for u, name in SIG.items():
    pat = u.to_bytes(2, "little")
    locs = [m.start() for m in re.finditer(re.escape(pat), data)]
    if locs:
        hits[u] = (name, locs)
for u in sorted(hits):
    name, locs = hits[u]
    show = ", ".join(hex(BASE + x) for x in locs[:6])
    print(f"  0x{u:04X} {name:22} x{len(locs):<4} {show}{' ...' if len(locs) > 6 else ''}")

# GATT DB heuristic: primary-service decl (0x2800) closely followed by char decls (0x2803)
print("\n=== GATT-DB structure check: 0x2800 decls with nearby 0x2803 (chars) ===")
svc = sorted(hits.get(0x2800, ("", []))[1])
chr_ = set(hits.get(0x2803, ("", []))[1])
cccd = set(hits.get(0x2902, ("", []))[1])
if not svc:
    print("  NO 0x2800 primary-service declarations found as LE 16-bit.")
    print("  => GATT DB likely built dynamically (sibles) or uses a non-table layout.")
else:
    for s in svc:
        near_ch = [c for c in chr_ if 0 < c - s < 400]
        near_cc = [c for c in cccd if 0 < c - s < 600]
        print(f"  0x2800 @0x{BASE + s:08x}  chars_within_400B={len(near_ch)} cccd={len(near_cc)}")

print("\n=== 128-bit UUID candidates: ASCII-form ===")
for m in re.finditer(rb"[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}", data):
    print(f"  @0x{BASE + m.start():08x}  {m.group().decode()}")

# 128-bit byte-array candidates: 16-byte runs that look like a UUID (not all-0/all-FF,
# decent byte diversity), sampled near any char decl to reduce noise.
print("\n=== 128-bit UUID candidates: byte-array near CHAR_DECL (0x2803) windows ===")
seen = set()
for c in sorted(chr_)[:200]:
    win = data[c : c + 64]
    for i in range(0, len(win) - 16):
        b = win[i : i + 16]
        uniq = len(set(b))
        if uniq >= 10 and b.count(0) <= 4 and b.count(0xFF) <= 2:
            key = bytes(b)
            if key in seen:
                continue
            seen.add(key)
            u = b[::-1].hex()
            us = f"{u[0:8]}-{u[8:12]}-{u[12:16]}-{u[16:20]}-{u[20:]}"
            print(f"  @0x{BASE + c + i:08x}  {us}")
            break
if not chr_:
    print("  (no 0x2803 char decls to anchor on)")
