"""Keep partial pressure-QA screenshots on failure without weakening validation."""
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch
import hashlib
import json
import struct
import sys
import tempfile
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ValidateBrushPressure as pressure


def make_png() -> bytes:
    def chunk(kind: bytes, data: bytes) -> bytes:
        return (struct.pack(">I", len(data)) + kind + data +
                struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff))
    header = struct.pack(">IIBBBBB", 2, 2, 8, 6, 0, 0, 0)
    pixels = b"\0" + bytes((255, 0, 0, 255)) * 2
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) +
            chunk(b"IDAT", zlib.compress(pixels * 2)) + chunk(b"IEND", b""))


class BrushPressureFailureEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="jixellight-pressure-像素-")
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.work, self.reports = self.base / "work", self.base / "reports"
        self.work.mkdir()
        self.reports.mkdir()
        self.native = self.work / "pressure.native.json"
        self.report = self.reports / "pressure.json"
        self.image = make_png()
        self.digest = hashlib.sha256(self.image).hexdigest()

    def capture(self, label, *, sony=False, **changes):
        suffix = "-controls.png" if sony else "-owned.png"
        path = Path(str(self.native) + "." + label + suffix)
        path.write_bytes(self.image)
        result = dict(label=label, path=str(path), dpr=1, width=3 if sony else 2,
                      height=2, sha256=self.digest)
        if sony:
            result.update(controls_width=2, controls_height=2)
        result.update(changes)
        return result

    def preserve(self, captures, *, sony=False):
        return pressure.preserve_failure_captures(
            {"dpr": 1, "captures": captures}, self.native, self.report, sony=sony)

    def test_actual_wrapper_keeps_failure_and_verified_partial_captures(self):
        def failed_process(command, **kwargs):
            self.assertEqual(Path(command[2]), self.native)
            data = {"dpr": 1, "error": "synthetic timeout",
                    "captures": [self.capture("before"), self.capture("failure")]}
            self.native.write_text(json.dumps(data), encoding="utf-8")
            kwargs["stdout"].write(b"synthetic failure\n")
            return SimpleNamespace(returncode=1)

        with patch.object(pressure.tempfile, "mkdtemp", return_value=str(self.work)), \
             patch.object(pressure.subprocess, "run", side_effect=failed_process):
            result = pressure.validate_brush_pressure(
                Path(sys.executable), {}, self.base / "home", self.report, "cpu")
        self.assertFalse(result["passed"])
        self.assertEqual(result["returncode"], 1)
        self.assertEqual(result["error"], "Pressure validation predicate failed")
        self.assertTrue(result["private_output_preserved"])
        self.assertTrue(result["failure_captures_partial"])
        self.assertEqual(result["failure_capture_errors"], [])
        self.assertEqual(set(result["failure_capture_artifacts"]), {"before", "failure"})
        for path in result["failure_capture_artifacts"].values():
            self.assertEqual(Path(path).read_bytes(), self.image)
        self.assertEqual(json.loads(self.report.read_text(encoding="utf-8")), result)

    def test_rejects_untrusted_label_path_digest_and_size(self):
        captures = [self.capture("before", sha256="0" * 64),
                    self.capture("painted", width=3),
                    self.capture("erased", path=str(self.base / "outside.png")),
                    {"label": "../outside", "path": str(self.base / "outside.png"), "dpr": 1}]
        copied, errors = self.preserve(captures)
        self.assertFalse(copied)
        self.assertEqual(len(errors), 4)
        self.assertFalse((self.base / "outside.png").exists())
        self.assertEqual(list(self.reports.iterdir()), [])

    def test_rejects_duplicate_without_overwriting(self):
        capture = self.capture("before")
        copied, errors = self.preserve([capture, capture])
        self.assertEqual(set(copied), {"before"})
        self.assertEqual(len(errors), 1)
        self.assertEqual(Path(copied["before"]).read_bytes(), self.image)

    def test_never_overwrites_different_existing_artifact(self):
        capture = self.capture("before")
        destination = self.reports / Path(capture["path"]).name
        destination.write_bytes(b"previous")
        copied, errors = self.preserve([capture])
        self.assertFalse(copied)
        self.assertEqual(len(errors), 1)
        self.assertEqual(destination.read_bytes(), b"previous")

    def test_rejects_symbolic_link_sources(self):
        capture = self.capture("before")
        source = Path(capture["path"])
        real_is_symlink = Path.is_symlink
        with patch.object(Path, "is_symlink", lambda path: path == source or real_is_symlink(path)):
            copied, errors = self.preserve([capture])
        self.assertFalse(copied)
        self.assertEqual(len(errors), 1)

    def test_sony_preserves_only_controls_crops(self):
        capture = self.capture("failure", sony=True)
        copied, errors = self.preserve([capture], sony=True)
        self.assertEqual(set(copied), {"failure"})
        self.assertEqual(errors, [])

    def test_sony_full_window_is_rejected(self):
        capture = self.capture("failure", sony=True, width=2)
        copied, errors = self.preserve([capture], sony=True)
        self.assertFalse(copied)
        self.assertEqual(len(errors), 1)

    def test_missing_or_excessive_capture_lists_are_rejected(self):
        for data in ({}, {"captures": None}, {"captures": [{}] * 100}):
            copied, errors = pressure.preserve_failure_captures(
                data, self.native, self.report, sony=False)
            self.assertFalse(copied)
            self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
