#!/usr/bin/env python3
"""wodle classic-Bluetooth SPP CLI (macOS / PyObjC IOBluetooth).

The wodle's primary app transport is BT-Classic (it runs an SPP server + A2DP/HFP/PAN).
This tool does classic inquiry, SDP browse for the SPP RFCOMM channel, and opens an
RFCOMM data channel to send/receive bytes.

STATUS: written, **UNTESTED against the wodle** — the device must first be PAIRED to this
Mac (it isn't yet; it's bonded to the phone). Validate once it's pairable. RFCOMM/SDP code
follows the IOBluetooth API; mac classic-BT support is finicky, so treat as a starting point.

Run from a real Terminal (not zellij/SSH — BT silently denied there):
  uv run --with pyobjc-framework-IOBluetooth python tools/wodle_spp.py inquiry
  uv run --with pyobjc-framework-IOBluetooth python tools/wodle_spp.py sdp <addr>
  uv run --with pyobjc-framework-IOBluetooth python tools/wodle_spp.py connect <addr> [hex|str:text]
"""

import sys
import time

import IOBluetooth
from Foundation import NSDate, NSObject, NSRunLoop


def _pump(seconds):
    end = time.time() + seconds
    while time.time() < end:
        NSRunLoop.currentRunLoop().runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(0.3))


def inquiry(_args):
    found = []

    class D(NSObject):
        def deviceInquiryDeviceFound_device_(self, inq, dev):
            name = dev.name() or "(no name)"
            print(f"  FOUND {name:24} {dev.addressString()}  cod=0x{dev.classOfDevice():06x}")
            found.append(dev.addressString())

        def deviceInquiryComplete_error_aborted_(self, inq, err, ab):
            print(f"inquiry complete err={err} aborted={ab}")

    inq = IOBluetooth.IOBluetoothDeviceInquiry.inquiryWithDelegate_(D.alloc().init())
    inq.setInquiryLength_(12)
    inq.setUpdateNewDeviceNames_(True)
    print("classic inquiry rc=", inq.start(), "(0=ok)")
    _pump(15)
    print("devices:", found or "(none discoverable — put wodle in pairing mode)")


def sdp(args):
    dev = IOBluetooth.IOBluetoothDevice.deviceWithAddressString_(args[0])
    print("SDP query rc=", dev.performSDPQuery_(None))
    _pump(4)
    svcs = dev.services() or []
    print(f"{len(svcs)} SDP service record(s):")
    for rec in svcs:
        name = rec.getServiceName() or "?"
        ch = rec.getRFCOMMChannelID_(None)
        print(f"  {name!r}  rfcomm_channel={ch}")


def connect(args):
    addr = args[0]
    payload = args[1] if len(args) > 1 else None
    dev = IOBluetooth.IOBluetoothDevice.deviceWithAddressString_(addr)
    dev.performSDPQuery_(None)
    _pump(3)
    # find first SPP-like RFCOMM channel
    chan_id = None
    for rec in dev.services() or []:
        ok, ch = rec.getRFCOMMChannelID_(None)
        if ok == IOBluetooth.kIOReturnSuccess:
            chan_id = ch
            print(f"using RFCOMM channel {ch} ({rec.getServiceName()!r})")
            break
    if chan_id is None:
        sys.exit("no RFCOMM/SPP channel found (device paired? SPP exposed?)")

    class Ch(NSObject):
        def rfcommChannelData_data_length_(self, ch, data, length):
            b = bytes(data[:length])
            try:
                print(f"<- {b.hex()}  {b.decode('utf-8')!r}")
            except UnicodeDecodeError:
                print(f"<- {b.hex()}")

        def rfcommChannelClosed_(self, ch):
            print("channel closed")

    ok, ch = dev.openRFCOMMChannelSync_withChannelID_delegate_(None, chan_id, Ch.alloc().init())
    if ok != IOBluetooth.kIOReturnSuccess:
        sys.exit(f"openRFCOMMChannel failed: {ok}")
    print("RFCOMM open")
    if payload:
        data = payload[4:].encode() if payload.startswith("str:") else bytes.fromhex(payload)
        ch.writeSync_length_(data, len(data))
        print(f"-> {data.hex()}")
    print("listening 30s (Ctrl-C to stop) ...")
    _pump(30)


CMDS = {"inquiry": inquiry, "sdp": sdp, "connect": connect}
if __name__ == "__main__":
    if len(sys.argv) < 2 or sys.argv[1] not in CMDS:
        print(__doc__)
        sys.exit(1)
    CMDS[sys.argv[1]](sys.argv[2:])
