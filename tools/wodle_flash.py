#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pyserial", "typer"]
# ///
"""wodle USB-recovery flasher (protocol "HVR1").

Pure command-line flasher for the wodle board (sf32lb52-lcd_n16r8) over the USB-CDC
port that enumerates after the device enters recovery mode (Settings -> "USB recovery",
or finsh `usb_recovery_mode`). Frame format and the full flash sequence are ported from
the vendor reference GUI (tools/usb_recovery_qt/main.cpp) — see
docs/superpowers/specs/2026-06-02-wodle-cli-flasher-design.md.

Frame = 24-byte header + payload, little-endian (struct <IBBHIIII>):
    magic u32 = 0x31525648 "HVR1" | version u8 = 1 | cmd u8 | seq u16
    addr u32 | size u32 | crc u32 | value u32 | payload[size]
`value` is 0 from the host; the device returns its status there. crc = zlib CRC32.

Sequence: HELLO -> BEGIN -> (per file: ERASE -> WRITE 2KB chunks -> VERIFY) -> COMMIT -> REBOOT.

    uv run tools/wodle_flash.py flash output/.../update.json
    uv run tools/wodle_flash.py flash ./fw --only app --dry-run
    uv run tools/wodle_flash.py hello
"""

from __future__ import annotations

import glob
import json
import struct
import time
import zlib
from collections.abc import Callable
from dataclasses import dataclass
from enum import IntEnum
from pathlib import Path

MAGIC = 0x31525648  # "HVR1"
VERSION = 1
MAX_PAYLOAD = 2048
BAUD = 1_000_000
VID = 0x38F4
PID = 0x1001

_HDR = struct.Struct("<IBBHIIII")  # magic, version, cmd, seq, addr, size, crc, value (24 B)
assert _HDR.size == 24

# Per-command transaction timeouts (seconds), mirroring the vendor tool.
T_HELLO, T_BEGIN, T_ERASE, T_WRITE, T_VERIFY, T_REBOOT = 5.0, 5.0, 35.0, 6.0, 45.0, 3.0

# Stock flash addresses (mpi2 XIP base 0x12000000). See board/wodle/ptab.yaml.
ADDR_APP = 0x12218000
ADDR_EZIP = 0x12460000
ADDR_FONT = 0x12AE0000
_RANK = {"app": 0, "font": 1, "ezip": 2}


class Cmd(IntEnum):
    HELLO = 1
    BEGIN = 2
    ERASE = 3
    WRITE = 4
    VERIFY = 5
    COMMIT = 6
    REBOOT = 7


class Status(IntEnum):
    OK = 0
    BAD_FRAME = 1
    DENIED = 2
    FLASH_ERROR = 3
    CRC_ERROR = 4
    BAD_PARTITION = 5


class RecoveryError(Exception):
    """Device returned a non-OK status, or the link failed."""


def crc32(data: bytes) -> int:
    return zlib.crc32(data) & 0xFFFFFFFF


def build_frame(
    cmd: int, seq: int, addr: int = 0, size: int = 0, crc: int = 0, value: int = 0, payload: bytes = b""
) -> bytes:
    return _HDR.pack(MAGIC, VERSION, int(cmd), seq & 0xFFFF, addr, size, crc, value) + payload


@dataclass(slots=True)
class Frame:
    version: int
    cmd: int
    seq: int
    addr: int
    size: int
    crc: int
    status: int  # the 8th u32: 0 in host frames, device status in replies
    payload: bytes

    @classmethod
    def parse(cls, data: bytes) -> Frame:
        magic, version, cmd, seq, addr, size, crc, value = _HDR.unpack(data[: _HDR.size])
        if magic != MAGIC:
            raise ValueError(f"bad magic {magic:#010x}")
        payload = bytes(data[_HDR.size : _HDR.size + size])
        return cls(version, cmd, seq, addr, size, crc, value, payload)


def partition_of(addr: int, name: str) -> str:
    n = name.lower()
    if "app" in n or "hcpu" in n or addr == ADDR_APP:
        return "app"
    if "font" in n or addr == ADDR_FONT:
        return "font"
    if "ezip" in n or "image" in n or addr == ADDR_EZIP:
        return "ezip"
    return "unknown"


def expect_ok(reply: Frame) -> Frame:
    if reply.status != Status.OK:
        name = Status(reply.status).name if reply.status in Status._value2member_map_ else f"UNKNOWN({reply.status})"
        raise RecoveryError(f"device returned {name} (cmd={reply.cmd})")
    return reply


@dataclass(slots=True)
class FirmwareFile:
    name: str
    path: Path
    partition: str
    addr: int
    region_size: int
    size: int
    crc32: int


def _to_int(value: object) -> int:
    return value if isinstance(value, int) else int(str(value), 0)


class Manifest:
    """Parsed + locally-validated update.json."""

    def __init__(self, version: str, files: list[FirmwareFile]):
        self.version = version
        self.files = files

    @classmethod
    def load(cls, path: str | Path) -> Manifest:
        path = Path(path)
        root = json.loads(path.read_text())
        files: list[FirmwareFile] = []
        for obj in root.get("files", []):
            name = obj["name"]
            fpath = path.parent / name
            addr = _to_int(obj["addr"])
            size = int(obj["size"])
            crc = _to_int(obj["crc32"])
            data = fpath.read_bytes()  # FAIL FAST if missing
            if len(data) != size:
                raise ValueError(f"{name}: on-disk size {len(data)} != manifest {size}")
            if crc32(data) != crc:
                raise ValueError(f"{name}: on-disk crc {crc32(data):#010x} != manifest {crc:#010x}")
            files.append(
                FirmwareFile(name, fpath, partition_of(addr, name), addr, _to_int(obj.get("region_size", 0)), size, crc)
            )
        if not files:
            raise ValueError(f"{path}: manifest has no files")
        files.sort(key=lambda f: _RANK.get(f.partition, 99))
        return cls(root.get("version", "--"), files)

    @classmethod
    def single(cls, path: str | Path, addr: int, *, name: str | None = None) -> Manifest:
        """A one-file manifest for a raw .bin at `addr` — the firmware dev-loop driver."""
        path = Path(path)
        data = path.read_bytes()
        name = name or path.name
        ff = FirmwareFile(name, path, partition_of(addr, name), addr, len(data), len(data), crc32(data))
        return cls("(raw)", [ff])


class RecoveryLink:
    """One recovery session over a serial-like object (write/read[/reset_input_buffer])."""

    def __init__(self, serial):
        self.ser = serial
        self.seq = 0

    def transact(
        self, cmd: int, addr: int = 0, size: int = 0, crc: int = 0, payload: bytes = b"", timeout: float = T_HELLO
    ) -> Frame:
        self.seq = (self.seq + 1) & 0xFFFF
        seq = self.seq
        frame = build_frame(cmd, seq, addr=addr, size=size, crc=crc, payload=payload)
        if hasattr(self.ser, "reset_input_buffer"):
            self.ser.reset_input_buffer()
        self.ser.write(frame)

        magic = struct.pack("<I", MAGIC)
        buf = bytearray()
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            chunk = self.ser.read(256)
            if chunk:
                buf += chunk
            while True:
                idx = buf.find(magic)
                if idx < 0:
                    del buf[:-3]  # keep a possible partial magic tail
                    break
                if idx:
                    del buf[:idx]
                if len(buf) < _HDR.size:
                    break
                payload_len = _HDR.unpack(buf[: _HDR.size])[5]
                if payload_len > MAX_PAYLOAD:
                    del buf[:4]  # bogus length, resync past this magic
                    continue
                total = _HDR.size + payload_len
                if len(buf) < total:
                    break
                reply = Frame.parse(bytes(buf[:total]))
                del buf[:total]
                if reply.version != VERSION:
                    continue
                if reply.payload and crc32(reply.payload) != reply.crc:
                    raise RecoveryError("device reply CRC mismatch")
                if reply.cmd == int(cmd) and reply.seq == seq:
                    return reply
                # stale / out-of-order reply: discard and keep scanning
            if not chunk:
                time.sleep(0.002)
        raise RecoveryError(f"timeout waiting for reply: cmd={int(cmd)} seq={seq}")


def flash(
    link: RecoveryLink,
    manifest: Manifest,
    *,
    write: bool = True,
    reboot: bool = True,
    only: list[str] | None = None,
    progress: Callable[[FirmwareFile, int, int], None] | None = None,
) -> None:
    """Run the full HVR1 sequence. `write=False` re-verifies without erasing/writing."""
    files = manifest.files
    if only:
        wanted = set(only)
        files = [f for f in files if f.partition in wanted]
        if not files:
            raise RecoveryError(f"no manifest files match --only {sorted(wanted)}")

    expect_ok(link.transact(Cmd.HELLO, timeout=T_HELLO))
    if write:
        expect_ok(link.transact(Cmd.BEGIN, timeout=T_BEGIN))

    for f in files:
        data = f.path.read_bytes()
        if len(data) != f.size or crc32(data) != f.crc32:
            raise RecoveryError(f"local file changed since manifest load: {f.name}")
        if write:
            expect_ok(link.transact(Cmd.ERASE, addr=f.addr, size=f.size, timeout=T_ERASE))
            for off in range(0, len(data), MAX_PAYLOAD):
                chunk = data[off : off + MAX_PAYLOAD]
                expect_ok(
                    link.transact(
                        Cmd.WRITE, addr=f.addr + off, size=len(chunk), crc=crc32(chunk), payload=chunk, timeout=T_WRITE
                    )
                )
                if progress:
                    progress(f, off + len(chunk), len(data))
        expect_ok(link.transact(Cmd.VERIFY, addr=f.addr, size=f.size, crc=f.crc32, timeout=T_VERIFY))

    if write:
        expect_ok(link.transact(Cmd.COMMIT, timeout=T_BEGIN))
    if reboot:
        expect_ok(link.transact(Cmd.REBOOT, timeout=T_REBOOT))


# --------------------------------------------------------------------------- CLI


def find_port(explicit: str | None = None) -> str:
    if explicit:
        return explicit
    from serial.tools import list_ports

    for p in list_ports.comports():
        if p.vid == VID and p.pid in (None, PID):
            return p.device
    cands = sorted(glob.glob("/dev/cu.usbmodem*"))
    if not cands:
        raise RecoveryError("no recovery port found — is the device in USB recovery mode?")
    return cands[0]


def open_link(port: str) -> RecoveryLink:
    import serial

    ser = serial.Serial(
        port,
        BAUD,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_NONE,
        stopbits=serial.STOPBITS_ONE,
        timeout=0.1,
    )
    return RecoveryLink(ser)


def _resolve_manifest(manifest: str) -> Path:
    p = Path(manifest)
    return p / "update.json" if p.is_dir() else p


def _main() -> None:
    import typer

    app = typer.Typer(add_completion=False, help="wodle USB-recovery flasher (HVR1)")
    port_opt = typer.Option(None, "--port", help="serial port (default: auto by VID/PID)")

    @app.command("flash")
    def cmd_flash(
        manifest: str = typer.Argument(..., help="update.json path or a dir containing it"),
        port: str = port_opt,
        dry_run: bool = typer.Option(False, "--dry-run", help="print the plan, don't open the port"),
        verify_only: bool = typer.Option(False, "--verify-only", help="re-verify without erasing/writing"),
        reboot: bool = typer.Option(True, "--reboot/--no-reboot", help="reboot after a successful flash"),
        yes: bool = typer.Option(False, "--yes", "-y", help="skip the erase/write confirmation"),
        only: str = typer.Option(None, "--only", help="comma list of partitions: app,font,ezip"),
    ) -> None:
        m = Manifest.load(_resolve_manifest(manifest))
        sel = [s.strip() for s in only.split(",")] if only else None
        files = [f for f in m.files if not sel or f.partition in set(sel)]
        write = not verify_only
        typer.echo(f"manifest {m.version}: {len(files)} partition(s), reboot={reboot} write={write}")
        for f in files:
            typer.echo(f"  {f.partition:5} {f.name:16} addr={f.addr:#010x} size={f.size} crc={f.crc32:#010x}")
        if dry_run:
            return
        if write and not yes:
            typer.confirm(f"Erase + write {len(files)} partition(s)?", abort=True)
        link = open_link(find_port(port))

        def progress(f: FirmwareFile, done: int, total: int) -> None:
            typer.echo(f"\r  {f.partition} {done}/{total} ({100 * done // total}%)", nl=False)

        flash(link, m, write=write, reboot=reboot, only=sel, progress=progress)
        typer.echo("\ndone.")

    @app.command("write")
    def cmd_write(
        file: str = typer.Argument(..., help="raw .bin to flash"),
        addr: str = typer.Option(..., "--addr", help="flash address, e.g. 0x12218000"),
        port: str = port_opt,
        reboot: bool = typer.Option(True, "--reboot/--no-reboot", help="reboot after flashing"),
        yes: bool = typer.Option(False, "--yes", "-y", help="skip the erase/write confirmation"),
    ) -> None:
        """Flash one raw .bin to an address in a single session (firmware dev loop)."""
        m = Manifest.single(file, int(addr, 0))
        f = m.files[0]
        typer.echo(f"{f.partition} {f.name} addr={f.addr:#010x} size={f.size} crc={f.crc32:#010x}")
        if not yes:
            typer.confirm(f"Erase + write {f.size} bytes at {f.addr:#010x}?", abort=True)
        link = open_link(find_port(port))

        def progress(ff: FirmwareFile, done: int, total: int) -> None:
            typer.echo(f"\r  {done}/{total} ({100 * done // total}%)", nl=False)

        flash(link, m, write=True, reboot=reboot, progress=progress)
        typer.echo("\ndone.")

    @app.command("hello")
    def cmd_hello(port: str = port_opt) -> None:
        reply = expect_ok(open_link(find_port(port)).transact(Cmd.HELLO, timeout=T_HELLO))
        typer.echo(reply.payload.decode("latin1") or "(empty)")

    @app.command("reboot")
    def cmd_reboot(port: str = port_opt) -> None:
        expect_ok(open_link(find_port(port)).transact(Cmd.REBOOT, timeout=T_REBOOT))
        typer.echo("rebooted.")

    app()


if __name__ == "__main__":
    _main()
