#!/usr/bin/env python3
"""wodle USB-CDC recovery client (protocol "HVR1", cracked from dfu_pan.bin V1.4.0.0853).

Speaks the device's recovery flash protocol over the CDC serial port that appears
(`/dev/cu.usbmodem*`) after entering recovery mode (device Settings -> "USB recovery",
or finsh `usb_recovery_mode`). No BLE / UART / BT-PAN needed.

Frame = 24-byte header + payload, all little-endian:
    magic u32 = 0x31525648 "HVR1" | version u8 =1 | cmd u8 | seq u16
    addr u32 | size u32 | crc32 u32 | status u32 | payload[size]
CRC = zlib.crc32(payload).  cmd: 1 HELLO 2 BEGIN 3 ERASE 4 WRITE 5 VERIFY 6 COMMIT 7 REBOOT.
status: 0 OK 1 BAD_FRAME 2 DENIED 3 FLASH_ERROR 4 CRC_ERROR 5 BAD_PARTITION.

WARNING: untested against hardware (needs the device in recovery mode). The frame format
is recovered statically; the only unconfirmed bit is transport delimiting (we send each
frame as one contiguous write, which the device's length-prefixed reader should accept).

    uv run --with pyserial python tools/wodle_usbrec.py hello
    uv run --with pyserial python tools/wodle_usbrec.py write --addr 0x12218000 --file app_new.bin
"""

import argparse
import glob
import struct
import sys
import zlib

import serial  # pyserial

MAGIC = 0x31525648  # "HVR1"
VERSION = 1
HDR = struct.Struct("<IBBHIIII")  # magic,version,cmd,seq,addr,size,crc,status  = 24 bytes
MAX_PAYLOAD = 2048

CMD = {"HELLO": 1, "BEGIN": 2, "ERASE": 3, "WRITE": 4, "VERIFY": 5, "COMMIT": 6, "REBOOT": 7}
STATUS = {0: "OK", 1: "BAD_FRAME", 2: "DENIED", 3: "FLASH_ERROR", 4: "CRC_ERROR", 5: "BAD_PARTITION"}


def frame(cmd: int, seq: int, addr: int = 0, payload: bytes = b"", aux: int = 0) -> bytes:
    crc = zlib.crc32(payload) & 0xFFFFFFFF if payload else 0
    return HDR.pack(MAGIC, VERSION, cmd, seq & 0xFFFF, addr, len(payload), crc, aux) + payload


def autodetect() -> str:
    ports = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not ports:
        sys.exit("no /dev/cu.usbmodem* found — is the device in USB recovery mode?")
    return ports[0]


class Link:
    def __init__(self, port: str, baud: int):
        self.s = serial.Serial(port, baud, timeout=3)
        self.seq = 0

    def xact(self, cmd: int, addr: int = 0, payload: bytes = b"", aux: int = 0) -> dict:
        self.seq = (self.seq + 1) & 0xFFFF
        self.s.write(frame(cmd, self.seq, addr, payload, aux))
        return self._read()

    def _read(self) -> dict:
        # sync on the magic, then read the 24-byte header + size payload
        win = b""
        magic_le = struct.pack("<I", MAGIC)
        while magic_le not in win:
            b = self.s.read(1)
            if not b:
                raise TimeoutError("no response (timeout)")
            win = (win + b)[-4:]
        hdr = magic_le + self.s.read(HDR.size - 4)
        magic, ver, cmd, seq, addr, size, crc, status = HDR.unpack(hdr)
        payload = self.s.read(size) if size else b""
        return dict(
            cmd=cmd,
            seq=seq,
            addr=addr,
            size=size,
            crc=crc,
            status=status,
            payload=payload,
            status_str=STATUS.get(status, f"?{status}"),
        )


def show(tag: str, r: dict):
    print(f"{tag}: status={r['status_str']} seq={r['seq']} addr=0x{r['addr']:08x} size={r['size']}")
    if r["payload"]:
        try:
            print("  payload:", r["payload"].decode("latin1"))
        except Exception:
            print("  payload:", r["payload"][:64].hex())


def main():
    ap = argparse.ArgumentParser(description="wodle USB-CDC recovery client (HVR1)")
    ap.add_argument("--port", default=None, help="serial port (default: auto /dev/cu.usbmodem*)")
    ap.add_argument("--baud", type=int, default=1000000)
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("hello")
    sub.add_parser("begin")
    sub.add_parser("commit")
    sub.add_parser("reboot")
    pe = sub.add_parser("erase")
    pe.add_argument("--addr", required=True)
    pe.add_argument("--size", required=True)
    pw = sub.add_parser("write")
    pw.add_argument("--addr", required=True)
    pw.add_argument("--file", required=True)
    pv = sub.add_parser("verify")
    pv.add_argument("--addr", required=True)
    pv.add_argument("--file", required=True)
    a = ap.parse_args()

    port = a.port or autodetect()
    print(f"port={port} baud={a.baud}")
    link = Link(port, a.baud)
    h = lambda v: int(v, 0)

    if a.cmd == "hello":
        show("HELLO", link.xact(CMD["HELLO"]))
    elif a.cmd == "begin":
        show("BEGIN", link.xact(CMD["BEGIN"]))
    elif a.cmd == "commit":
        show("COMMIT", link.xact(CMD["COMMIT"]))
    elif a.cmd == "reboot":
        show("REBOOT", link.xact(CMD["REBOOT"]))
    elif a.cmd == "erase":
        show("ERASE", link.xact(CMD["ERASE"], addr=h(a.addr), aux=h(a.size)))
    elif a.cmd in ("write", "verify"):
        data = open(a.file, "rb").read()
        show("BEGIN", link.xact(CMD["BEGIN"]))
        addr = h(a.addr)
        for off in range(0, len(data), MAX_PAYLOAD):
            chunk = data[off : off + MAX_PAYLOAD]
            cmd = CMD["WRITE"] if a.cmd == "write" else CMD["VERIFY"]
            r = link.xact(
                cmd,
                addr=addr + off,
                payload=chunk if a.cmd == "write" else b"",
                aux=zlib.crc32(chunk) & 0xFFFFFFFF if a.cmd == "verify" else 0,
            )
            if r["status"] != 0:
                show(f"{a.cmd.upper()}@0x{addr + off:08x}", r)
                sys.exit(f"abort: {r['status_str']}")
            print(f"  {a.cmd} 0x{addr + off:08x} +{len(chunk)}  {off + len(chunk)}/{len(data)}", end="\r")
        print()
        if a.cmd == "write":
            show("COMMIT", link.xact(CMD["COMMIT"]))


if __name__ == "__main__":
    main()
