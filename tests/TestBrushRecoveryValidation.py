"""Negative controls for prefix-recovery admission and failure captures."""
import copy
from pathlib import Path
import struct
import sys
import tempfile
import unittest
import zlib

# This fixture is synthetic; actual canvas acceptance comes from native reports.
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ValidateBrushRecovery import REQUIRED, LABELS, validate_recovery_report, copy_captures, sha256


def valid_report():
    d = {key: True for key in REQUIRED}
    d.update({key: False for key in ("physical_OS_activation_verified", "physical_tablet_verified",
        "native_gpu_brush", "native_float_raw_decoder", "production_accepted", "continuous_unique_jitter_fixed")})
    d.update(passed=True, error="", source_kind="owned-rgba64-png", source_pixels=512 * 320,
        source_alpha_u16=65535, source_sha256="a" * 64, source_commit="synthetic-control",
        injection="Qt-QPA-controlled-pressure-stylus", elapsed_ms=3000, deadline_ms=60000,
        dpr=1, actual_qpa="offscreen", gpu_active=False, backend="CPU", recovered_mode="paint",
        language="en_US", single_stroke_dab_limit=4096, document_dab_limit=16384,
        recovered_dabs=4096, first_rejected_packet=8192, second_rejected_packet=8192,
        third_rejected_packet=8192)
    return d


def png():
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0)) + chunk(b"IDAT", zlib.compress(b"\0\x80\x80\x80\xff")) + chunk(b"IEND", b"")


class RecoveryValidationTests(unittest.TestCase):
    def reject(self, data):
        with self.assertRaises((AssertionError, KeyError, TypeError, ValueError)):
            validate_recovery_report(data, "cpu", False, False, "offscreen")

    def test_synthetic_positive_scope(self):
        validate_recovery_report(valid_report(), "cpu", False, False, "offscreen")

    def test_every_behavior_is_required(self):
        for key in REQUIRED:
            with self.subTest(key=key):
                d = valid_report(); d[key] = False; self.reject(d)
                del d[key]; self.reject(d)

    def test_unverified_claims_stay_false(self):
        for key in ("physical_OS_activation_verified", "physical_tablet_verified", "native_gpu_brush",
                    "native_float_raw_decoder", "production_accepted", "continuous_unique_jitter_fixed"):
            with self.subTest(key=key):
                d = valid_report(); d[key] = True; self.reject(d)

    def test_original_capacity_and_deadline(self):
        for key, value in (("single_stroke_dab_limit",4097),("document_dab_limit",16385),
                           ("deadline_ms",60001),("elapsed_ms",60000),("recovered_dabs",4097),
                           ("recovered_dabs",3999)):
            with self.subTest(key=key,value=value):
                d = valid_report(); d[key] = value; self.reject(d)

    def test_all_three_unique_gestures_reach_capacity(self):
        for key in ("first_rejected_packet","second_rejected_packet","third_rejected_packet"):
            for value in (0,7999,9001,True):
                d = valid_report(); d[key] = value; self.reject(d)

    def test_source_mode_language_and_backend(self):
        for key, value in (("source_alpha_u16",42000),("recovered_mode","erase"),
                           ("language","zh_CN"),("source_commit","unknown"),
                           ("gpu_active",True),("injection","direct-controller")):
            d = valid_report(); d[key] = value; self.reject(d)

    def test_dimensions_and_real_qpa(self):
        for key, value in (("source_pixels",0),("dpr",0),("dpr",float("nan")),
                           ("actual_qpa","windows"),("source_sha256","invalid")):
            d = valid_report(); d[key] = value; self.reject(d)

    def test_failed_native_report_is_not_accepted(self):
        d = valid_report(); d["passed"] = False; self.reject(d)
        d = valid_report(); d["error"] = "incomplete"; self.reject(d)

    def capture_case(self, mutate=None):
        with tempfile.TemporaryDirectory() as temp:
            root=Path(temp); private=root/"private"; private.mkdir(); published=root/"published"; published.mkdir()
            native=private/"case.native.json"; path=Path(str(native)+".before-owned.png"); path.write_bytes(png())
            entry={"label":"before","path":str(path),"width":1,"height":1,"dpr":1,"sha256":sha256(path)}
            data={"dpr":1,"captures":[entry]}
            if mutate: mutate(data,path,published)
            return copy_captures(data,native,published/"case.json")

    def test_valid_partial_failure_capture_is_preserved(self):
        copied, errors = self.capture_case()
        self.assertEqual(set(copied), {"before"}); self.assertEqual(errors, [])
        self.assertNotEqual(set(copied), LABELS)  # A partial failure is never complete acceptance.

    def test_corrupt_png_or_hash_is_rejected(self):
        def corrupt(data,path,published):
            b=bytearray(path.read_bytes()); b[20]^=1; path.write_bytes(b)
            data["captures"][0]["sha256"]=sha256(path)
        for change in (corrupt, lambda d,p,o: d["captures"][0].update(sha256="0"*64)):
            copied, errors=self.capture_case(change); self.assertFalse(copied); self.assertTrue(errors)

    def test_existing_capture_is_preserved(self):
        def existing(data,path,published): (published/path.name).write_bytes(b"user-owned")
        copied, errors=self.capture_case(existing); self.assertFalse(copied); self.assertTrue(errors)

    def test_unknown_duplicate_and_changed_path_are_rejected(self):
        mutations=(lambda d,p,o: d["captures"][0].update(label="other"),
                   lambda d,p,o: d["captures"].append(copy.deepcopy(d["captures"][0])),
                   lambda d,p,o: d["captures"][0].update(path=str(p.parent/"other.png")))
        for change in mutations:
            copied, errors=self.capture_case(change); self.assertTrue(errors)


if __name__ == "__main__":
    unittest.main()
