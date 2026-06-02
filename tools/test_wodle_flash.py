"""Offline tests for wodle_flash — no hardware. A FakeDevice stub stands in for the
serial port so we can assert the exact HVR1 frames the flasher emits."""

import json
import struct
import zlib

import pytest
import wodle_flash as wf


def test_crc32_matches_zlib():
    assert wf.crc32(b"hello world") == zlib.crc32(b"hello world") & 0xFFFFFFFF


def test_frame_roundtrip():
    f = wf.build_frame(wf.Cmd.WRITE, seq=7, addr=0x12218000, size=4, crc=0xDEAD, payload=b"abcd")
    assert len(f) == 24 + 4
    p = wf.Frame.parse(f)
    assert p.cmd == int(wf.Cmd.WRITE)
    assert p.seq == 7
    assert p.addr == 0x12218000
    assert p.size == 4
    assert p.crc == 0xDEAD
    assert p.status == 0  # host frames carry 0 in the 8th field
    assert p.payload == b"abcd"


def test_partition_of():
    assert wf.partition_of(0x12218000, "hcpu_app.bin") == "app"
    assert wf.partition_of(0x12460000, "ezip_image.bin") == "ezip"
    assert wf.partition_of(0x12AE0000, "font_data.bin") == "font"
    assert wf.partition_of(0x1, "mystery.bin") == "unknown"


def _write_manifest(tmp_path, files):
    """files: list of (name, addr, region_size, data)."""
    entries = []
    for name, addr, region, data in files:
        (tmp_path / name).write_bytes(data)
        entries.append(
            {
                "name": name,
                "addr": hex(addr),
                "region_size": hex(region),
                "size": len(data),
                "crc32": hex(wf.crc32(data)),
            }
        )
    path = tmp_path / "update.json"
    path.write_text(json.dumps({"version": "TEST", "files": entries}))
    return path


def test_manifest_loads_sorts_and_validates(tmp_path):
    path = _write_manifest(
        tmp_path,
        [
            ("font_data.bin", 0x12AE0000, 0x400000, b"F" * 10),
            ("ezip_image.bin", 0x12460000, 0x680000, b"E" * 20),
            ("hcpu_app.bin", 0x12218000, 0x240000, b"A" * 30),
        ],
    )
    m = wf.Manifest.load(path)
    assert m.version == "TEST"
    assert [f.partition for f in m.files] == ["app", "font", "ezip"]  # vendor rank order
    assert m.files[0].size == 30
    assert all(f.path.exists() for f in m.files)


def test_manifest_rejects_bad_crc(tmp_path):
    path = _write_manifest(tmp_path, [("hcpu_app.bin", 0x12218000, 0x240000, b"A" * 30)])
    (tmp_path / "hcpu_app.bin").write_bytes(b"B" * 30)  # corrupt after manifest written
    with pytest.raises(ValueError):
        wf.Manifest.load(path)


class FakeDevice:
    """In-memory serial stub: parses each request frame, queues an OK reply."""

    def __init__(self, status_for=None, verify_actual_crc=None):
        self.requests = []
        self._out = bytearray()
        self._status_for = status_for or {}
        self._verify_actual_crc = verify_actual_crc

    def write(self, data):
        req = wf.Frame.parse(bytes(data))
        self.requests.append(req)
        status = int(self._status_for.get(req.cmd, 0))
        payload = b""
        if req.cmd == int(wf.Cmd.HELLO):
            payload = b"board=sf32lb52-lcd_n16r8;proto=1;max_payload=2048"
        elif (
            req.cmd == int(wf.Cmd.VERIFY) and status == int(wf.Status.CRC_ERROR) and self._verify_actual_crc is not None
        ):
            payload = struct.pack("<I", self._verify_actual_crc)
        reply = wf.build_frame(
            req.cmd,
            req.seq,
            addr=req.addr,
            size=len(payload),
            crc=wf.crc32(payload) if payload else 0,
            value=status,
            payload=payload,
        )
        self._out += reply
        return len(data)

    def read(self, n):
        chunk = bytes(self._out[:n])
        del self._out[:n]
        return chunk

    def reset_input_buffer(self):
        self._out.clear()


def test_flash_full_sequence_sends_correct_frames(tmp_path):
    data = b"A" * 5000  # 2048 + 2048 + 904 -> 3 WRITE chunks
    path = _write_manifest(tmp_path, [("hcpu_app.bin", 0x12218000, 0x240000, data)])
    dev = FakeDevice()
    wf.flash(wf.RecoveryLink(dev), wf.Manifest.load(path), write=True, reboot=True)

    C = wf.Cmd
    cmds = [r.cmd for r in dev.requests]
    assert cmds == [
        int(C.HELLO),
        int(C.BEGIN),
        int(C.ERASE),
        int(C.WRITE),
        int(C.WRITE),
        int(C.WRITE),
        int(C.VERIFY),
        int(C.COMMIT),
        int(C.REBOOT),
    ]

    # The corrected field placement vs the old wodle_usbrec.py:
    erase = next(r for r in dev.requests if r.cmd == int(C.ERASE))
    assert (erase.addr, erase.size, erase.crc) == (0x12218000, len(data), 0)
    verify = next(r for r in dev.requests if r.cmd == int(C.VERIFY))
    assert (verify.addr, verify.size, verify.crc) == (0x12218000, len(data), wf.crc32(data))

    writes = [r for r in dev.requests if r.cmd == int(C.WRITE)]
    assert writes[0].addr == 0x12218000
    assert writes[0].size == 2048
    assert writes[0].crc == wf.crc32(data[:2048])
    assert writes[1].addr == 0x12218000 + 2048
    assert writes[2].size == 5000 - 4096
    # WRITE seq numbers strictly increase (matters for the stale-frame discard logic)
    assert [r.seq for r in writes] == sorted({r.seq for r in writes})


def test_verify_only_skips_writes(tmp_path):
    path = _write_manifest(tmp_path, [("hcpu_app.bin", 0x12218000, 0x240000, b"A" * 100)])
    dev = FakeDevice()
    wf.flash(wf.RecoveryLink(dev), wf.Manifest.load(path), write=False, reboot=False)
    C = wf.Cmd
    assert [r.cmd for r in dev.requests] == [int(C.HELLO), int(C.VERIFY)]


def test_flash_raises_on_device_error(tmp_path):
    path = _write_manifest(tmp_path, [("hcpu_app.bin", 0x12218000, 0x240000, b"A" * 100)])
    dev = FakeDevice(status_for={int(wf.Cmd.ERASE): int(wf.Status.FLASH_ERROR)})
    with pytest.raises(wf.RecoveryError):
        wf.flash(wf.RecoveryLink(dev), wf.Manifest.load(path), write=True, reboot=False)


def test_only_filter_selects_subset(tmp_path):
    path = _write_manifest(
        tmp_path,
        [
            ("hcpu_app.bin", 0x12218000, 0x240000, b"A" * 100),
            ("font_data.bin", 0x12AE0000, 0x400000, b"F" * 100),
        ],
    )
    dev = FakeDevice()
    wf.flash(wf.RecoveryLink(dev), wf.Manifest.load(path), write=True, reboot=False, only=["app"])
    verifies = [r for r in dev.requests if r.cmd == int(wf.Cmd.VERIFY)]
    assert len(verifies) == 1
    assert verifies[0].addr == 0x12218000


def test_manifest_single_raw_bin_drives_full_flash(tmp_path):
    data = b"Z" * 1234
    p = tmp_path / "app_new.bin"
    p.write_bytes(data)
    m = wf.Manifest.single(p, 0x12218000)
    (f,) = m.files
    assert (f.partition, f.addr, f.size, f.crc32) == ("app", 0x12218000, 1234, wf.crc32(data))

    dev = FakeDevice()
    wf.flash(wf.RecoveryLink(dev), m, write=True, reboot=True)
    C = wf.Cmd
    assert [r.cmd for r in dev.requests] == [
        int(C.HELLO),
        int(C.BEGIN),
        int(C.ERASE),
        int(C.WRITE),
        int(C.VERIFY),
        int(C.COMMIT),
        int(C.REBOOT),
    ]
