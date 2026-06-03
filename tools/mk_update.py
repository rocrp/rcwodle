#!/usr/bin/env python3
# /// script
# requires-python = ">=3.13"
# dependencies = ["typer"]
# ///
"""Regenerate a tf_ota update.json (crc32 + size + addr) from built firmware bins.

The wodle bootloader reads /firmware/update.json off the SD card, verifies each file's
crc32 + size, and only flashes if "version" is newer than what's installed. This tool
reproduces that manifest from the .bin(s) so a custom build drops onto a recovery card.

The pure helpers (build_entry / build_manifest) import nothing beyond the stdlib, so the
tests don't need typer; the CLI lazy-imports typer in _cli().
"""

import json
import zlib
from pathlib import Path

# Stock region layout (mpi2 base 0x12000000) — see board/wodle/ptab.yaml.
REGIONS = {
    "hcpu_app.bin": (0x12218000, 0x240000, 0),
    "ezip_image.bin": (0x12460000, 0x680000, 1),
    "font_data.bin": (0x12AE0000, 0x400000, 2),
}


def build_entry(path: Path, *, addr: int, region_size: int, file_id: int) -> dict:
    data = Path(path).read_bytes()
    return {
        "name": Path(path).name,
        "addr": f"0x{addr:08X}",
        "region_size": f"0x{region_size:08X}",
        "size": len(data),
        "crc32": f"0x{zlib.crc32(data) & 0xFFFFFFFF:08X}",
        "file_id": file_id,
    }


def build_manifest(firmware_dir: Path, version: str, generated_at: str) -> dict:
    files = []
    for name, (addr, region, fid) in REGIONS.items():
        p = Path(firmware_dir) / name
        if p.exists():
            files.append(build_entry(p, addr=addr, region_size=region, file_id=fid))
    return {"version": version, "generated_at": generated_at, "files": files}


def _cli() -> None:
    import typer

    app = typer.Typer(add_completion=False)

    @app.command()
    def main(
        firmware_dir: Path = typer.Argument(..., help="dir holding the .bin(s)"),
        version: str = typer.Option(..., "--version", "-v", help='e.g. "V1.4.0.9001"'),
        generated_at: str = typer.Option("1970-01-01T00:00:00Z", "--at"),
        out: Path = typer.Option(None, "--out", "-o", help="default: <firmware_dir>/update.json"),
    ):
        manifest = build_manifest(firmware_dir, version, generated_at)
        if not manifest["files"]:
            raise typer.BadParameter(f"no known bins found in {firmware_dir}")
        dest = out or (firmware_dir / "update.json")
        Path(dest).write_text(json.dumps(manifest, indent=2, ensure_ascii=False))
        typer.echo(f"wrote {dest} ({len(manifest['files'])} files, version {version})")

    app()


if __name__ == "__main__":
    _cli()
