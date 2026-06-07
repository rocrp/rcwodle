#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["pyserial", "typer", "pillow"]
# ///
"""Host driver for the crosspoint firmware's `wodle` MSH debug command.

Talks to the RT-Thread finsh console on the WCH-Link UART (1M baud). Pairs
with port/hal/WodleDebugCmds.cpp:

    uv run tools/wodle_console.py stat
    uv run tools/wodle_console.py key down
    uv run tools/wodle_console.py key power --hold 2500     # real hold-to-sleep
    uv run tools/wodle_console.py open /books/foo.epub
    uv run tools/wodle_console.py dump screen.png           # SEE the e-ink screen
    uv run tools/wodle_console.py raw "free"                # any msh command

The dump path decodes the WODLE_DUMP_BEGIN/END base64 stream (1-bit panel
framebuffer, bit set = white) and verifies its CRC32 — a UART glitch fails
loudly instead of producing a subtly wrong image.
"""

import base64
import binascii
import glob
import re
import time

import serial
import typer

app = typer.Typer(add_completion=False, help=__doc__)

BAUD = 1_000_000
DUMP_BEGIN = re.compile(rb"WODLE_DUMP_BEGIN w=(\d+) h=(\d+) bytes=(\d+) crc32=([0-9A-Fa-f]{8})")


def find_port(explicit: str | None) -> str:
    if explicit:
        return explicit
    # Prefer the firmware's own USB-CDC console (VID 0x38F4 PID 0x1003,
    # /dev/cu.usbmodem*) over a WCH-Link UART; never grab the HVR recovery port.
    cdc = sorted(p for p in glob.glob("/dev/cu.usbmodem*") if "HVR_RECOVERY" not in p)
    uart = sorted(glob.glob("/dev/cu.usbserial*") + glob.glob("/dev/cu.wchusbserial*"))
    candidates = cdc + uart
    if not candidates:
        typer.echo("No console port found (usbmodem/usbserial) — pass --port", err=True)
        raise typer.Exit(1)
    if len(candidates) > 1:
        typer.echo(f"Multiple ports, using {candidates[0]} (override with --port): {candidates}", err=True)
    return candidates[0]


def open_console(port: str | None) -> serial.Serial:
    s = serial.Serial(find_port(port), BAUD, timeout=0.3)
    s.reset_input_buffer()
    return s


def send(s: serial.Serial, cmd: str) -> None:
    s.write(cmd.encode() + b"\r\n")
    s.flush()


def read_until_quiet(s: serial.Serial, quiet_s: float = 0.4, max_s: float = 5.0) -> bytes:
    out = bytearray()
    last = time.monotonic()
    start = last
    while True:
        chunk = s.read(4096)
        now = time.monotonic()
        if chunk:
            out += chunk
            last = now
        elif now - last > quiet_s or now - start > max_s:
            break
    return bytes(out)


def run_cmd(port: str | None, cmd: str) -> str:
    with open_console(port) as s:
        send(s, cmd)
        out = read_until_quiet(s)
    text = out.decode(errors="replace")
    typer.echo(text.strip())
    return text


@app.command()
def raw(cmd: str, port: str = typer.Option(None)) -> None:
    """Send any console line and print the reply."""
    run_cmd(port, cmd)


@app.command()
def stat(port: str = typer.Option(None)) -> None:
    """One-line device status (battery/heap/clock/...)."""
    run_cmd(port, "wodle stat")


@app.command()
def key(
    name: str = typer.Argument(..., help="up|down|left|right|confirm|back|power"),
    hold: int = typer.Option(0, help="hold duration ms (power: 100 default on device)"),
    port: str = typer.Option(None),
) -> None:
    """Inject a key press through the real input pipeline."""
    run_cmd(port, f"wodle key {name} {hold}" if hold else f"wodle key {name}")


@app.command(name="open")
def open_book(path: str, port: str = typer.Option(None)) -> None:
    """Open a book by SD path (validated on the device's main thread)."""
    run_cmd(port, f"wodle open {path}")


@app.command()
def nosleep(state: str = typer.Argument(..., help="on|off"), port: str = typer.Option(None)) -> None:
    """Force the auto-sleep inhibit on or off."""
    run_cmd(port, f"wodle nosleep {state}")


@app.command()
def dump(
    out: str = typer.Argument("screen.png"),
    port: str = typer.Option(None),
    timeout: float = typer.Option(10.0, help="seconds to wait for the full dump"),
) -> None:
    """Capture the live framebuffer to a PNG (CRC-verified)."""
    from PIL import Image

    with open_console(port) as s:
        send(s, "wodle dump")
        deadline = time.monotonic() + timeout
        buf = bytearray()
        while time.monotonic() < deadline:
            chunk = s.read(8192)
            if chunk:
                buf += chunk
            if b"WODLE_DUMP_END" in buf:
                break
        else:
            typer.echo("Timed out waiting for WODLE_DUMP_END", err=True)
            raise typer.Exit(1)

    m = DUMP_BEGIN.search(buf)
    if not m:
        typer.echo("WODLE_DUMP_BEGIN header not found", err=True)
        raise typer.Exit(1)
    width, height, nbytes, crc_expected = int(m[1]), int(m[2]), int(m[3]), int(m[4], 16)

    body = buf[m.end() : buf.index(b"WODLE_DUMP_END")]
    b64 = b"".join(body.split())  # tolerate \r\n and any console framing
    try:
        data = base64.b64decode(b64, validate=True)
    except binascii.Error as e:
        typer.echo(f"base64 decode failed ({e}) — noisy UART capture?", err=True)
        raise typer.Exit(1) from None

    if len(data) != nbytes:
        typer.echo(f"size mismatch: got {len(data)}, header says {nbytes}", err=True)
        raise typer.Exit(1)
    crc_actual = binascii.crc32(data) & 0xFFFFFFFF
    if crc_actual != crc_expected:
        typer.echo(f"CRC mismatch: got {crc_actual:08X}, header says {crc_expected:08X}", err=True)
        raise typer.Exit(1)

    # 1-bit panel buffer, MSB-first per byte, bit set = white.
    img = Image.frombytes("1", (width, height), bytes(data))
    img.convert("L").save(out)
    typer.echo(f"OK {width}x{height} crc32={crc_actual:08X} -> {out}")


if __name__ == "__main__":
    app()
