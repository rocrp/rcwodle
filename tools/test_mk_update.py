# /// script
# requires-python = ">=3.13"
# dependencies = ["pytest"]
# ///
import json
import zlib
from pathlib import Path

from mk_update import build_entry, build_manifest

STOCK = Path(__file__).parent.parent / "refs/card_snapshot_0246/firmware"


def test_build_entry_crc_and_size(tmp_path):
    data = b"hello wodle" * 1000
    f = tmp_path / "hcpu_app.bin"
    f.write_bytes(data)
    entry = build_entry(f, addr=0x12218000, region_size=0x240000, file_id=0)
    assert entry["size"] == len(data)
    assert entry["crc32"] == f"0x{zlib.crc32(data) & 0xFFFFFFFF:08X}"
    assert entry["addr"] == "0x12218000"
    assert entry["region_size"] == "0x00240000"


def test_matches_stock_manifest():
    """Regenerating from the stock bins must reproduce the stock crc32+size."""
    stock = json.loads((STOCK / "update.json").read_text())
    by_name = {e["name"]: e for e in stock["files"]}
    for name, addr, region in [
        ("hcpu_app.bin", 0x12218000, 0x240000),
        ("ezip_image.bin", 0x12460000, 0x680000),
        ("font_data.bin", 0x12AE0000, 0x400000),
    ]:
        got = build_entry(STOCK / name, addr=addr, region_size=region, file_id=by_name[name]["file_id"])
        assert got["crc32"].lower() == by_name[name]["crc32"].lower(), name
        assert got["size"] == by_name[name]["size"], name


def test_build_manifest_skips_absent(tmp_path):
    (tmp_path / "hcpu_app.bin").write_bytes(b"x" * 16)
    m = build_manifest(tmp_path, version="V1.4.0.9001", generated_at="2026-06-03T00:00:00Z")
    names = [e["name"] for e in m["files"]]
    assert names == ["hcpu_app.bin"]
    assert m["version"] == "V1.4.0.9001"
