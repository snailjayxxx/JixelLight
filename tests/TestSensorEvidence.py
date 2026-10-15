"""No-network regression checks for Stage 6C sensor evidence acceptance gates."""
from __future__ import annotations

import copy
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import ValidateSensorEvidence as evidence


def fixture_report() -> tuple[dict, dict]:
    pairs, items, scenes = [], [], []
    for i, (rawcode, jpgcode) in enumerate(zip("abc", "def", strict=True)):
        ident = f"camera-scene-{i}"
        raw = f"camera-{i}.arw"
        jpg = f"camera-{i}.jpg"
        role = "train" if i < 2 else "validation"
        pairs.append({"id": ident, "raw": raw, "jpeg": jpg, "role": role,
                      "auditRequired": True})
        items.extend([{"file": raw, "sha256": rawcode * 64},
                      {"file": jpg, "sha256": jpgcode * 64}])
        parity = [{"pixels": 4, "min": 100, "max": 200, "mean": 150.} for _ in range(4)]
        audit = {
            "schema": 1, "available": True, "rgb_demosaiced": False,
            "black_subtracted": False, "raw_width": 4, "raw_height": 4,
            "active_bounds": [0, 0, 4, 4], "sample_count": 16,
            "minimum_code": 100, "maximum_code": 200, "mean_code": 150.,
            "p50_code": 140, "p99_code": 190, "p999_code": 198,
            "nominal_black_code": 100, "nominal_white_code": 200,
            "at_or_above_nominal_white_code": 1,
            "at_or_below_nominal_black_code": 1,
            "at_code_65535": 0, "pixel_sha256": "f"*64,
            "cfa_kind": "bayer_like_filters", "libraw_version": "fixture",
            "parity_2x2_raw_grid_not_cfa_color": parity,
        }
        routes = [
            {"name": "paired-camera-jpeg", "stats": {"valid": True, "highlightClipPercent": .5}},
            {"name": "cpu-raw-neutral", "stats": {"valid": True},
             "vsPairedCameraJpeg": {"valid": True, "rmse": .11}},
            {"name": "cpu-raw-as-shot", "stats": {"valid": True},
             "vsPairedCameraJpeg": {"valid": True, "rmse": .04}},
        ]
        scenes.append({
            "id": ident, "role": role, "rawSha256": rawcode*64,
            "jpegSha256": jpgcode*64, "sensorUnpackedAudit": audit,
            "cameraEvidence": {
                "make": "SONY", "model": "ILCE-7M4",
                "isoText": ["100", "200", "400"][i],
                "shutterText": ["1/60", "1/125", "1/250"][i],
                "captureTime": f"2026:10:15 10:00:0{i}",
                "subSecTime": "",
            },
            "metadataError": "", "decodeError": "", "routes": routes,
        })
    manifest = {"schema": 1, "items": items, "pairs": pairs,
                "expected": {"model": "ILCE-7M4"}}
    native = {"schema": 1, "engineVersion": "fixture", "sceneCount": 3, "scenes": scenes}
    return manifest, native


class SensorEvidenceTests(unittest.TestCase):
    def setUp(self) -> None:
        self.manifest, self.matrix = fixture_report()

    def check_rejected(self) -> None:
        with self.assertRaises(evidence.EvidenceError):
            evidence.analyze(self.manifest, self.matrix)

    def test_complete_measurements_do_not_accept_camera_calibration(self):
        result = evidence.analyze(self.manifest, self.matrix)
        self.assertTrue(result["evidenceIntegrityPassed"])
        self.assertFalse(result["cameraCalibrationAccepted"])
        self.assertFalse(result["defaultRawReplacementAllowed"])
        self.assertFalse(result["stage6BRealCameraFidelityValidated"])
        self.assertEqual(result["source"]["heldoutCount"], 1)
        self.assertEqual(result["coverage"]["distinctIso"], [100, 200, 400])
        self.assertEqual(len(result["coverage"]["distinctShutterSeconds"]), 3)
        self.assertAlmostEqual(result["scenes"][0]["sensorAudit"]["nominalWhiteTailFraction"], 1 / 16)
        self.assertTrue(any("not scene-linear" in gap for gap in result["coverage"]["remainingEvidence"]))

    def test_changed_raw_sha_rejects(self):
        self.matrix["scenes"][0]["rawSha256"] = "e"*64
        self.check_rejected()

    def test_changed_jpeg_sha_rejects(self):
        self.matrix["scenes"][0]["jpegSha256"] = "a"*64
        self.check_rejected()

    def test_no_heldout_validation_rejects(self):
        self.manifest["pairs"][2]["role"] = "train"
        self.matrix["scenes"][2]["role"] = "train"
        self.check_rejected()

    def test_holdout_scene_reused_source_rejects(self):
        self.manifest["items"][4]["sha256"] = "a"*64
        self.matrix["scenes"][2]["rawSha256"] = "a"*64
        self.check_rejected()

    def test_fake_camera_capture_identity_rejects(self):
        self.matrix["scenes"][2]["cameraEvidence"]["captureTime"] = (
            self.matrix["scenes"][0]["cameraEvidence"]["captureTime"])
        self.check_rejected()

    def test_active_sensor_area_wrong_count_rejects(self):
        self.matrix["scenes"][1]["sensorUnpackedAudit"]["sample_count"] = 15
        self.check_rejected()

    def test_raw_2x2_parity_count_incomplete_rejects(self):
        self.matrix["scenes"][1]["sensorUnpackedAudit"][
            "parity_2x2_raw_grid_not_cfa_color"][0]["pixels"] = 3
        self.check_rejected()

    def test_audit_after_demosaic_rejects(self):
        self.matrix["scenes"][0]["sensorUnpackedAudit"]["rgb_demosaiced"] = True
        self.check_rejected()

    def test_unavailable_required_sensor_audit_rejects(self):
        self.matrix["scenes"][0]["sensorUnpackedAudit"] = {
            "schema": 1, "available": False, "reason": "unsupported pitch"}
        self.check_rejected()

    def test_unavailable_optional_audit_reported_but_not_calibrated(self):
        self.manifest["pairs"][0]["auditRequired"] = False
        self.matrix["scenes"][0]["sensorUnpackedAudit"] = {
            "schema": 1, "available": False, "reason": "unsupported pitch"}
        result = evidence.analyze(self.manifest, self.matrix)
        self.assertTrue(result["evidenceIntegrityPassed"])
        self.assertFalse(result["scenes"][0]["sensorAudit"]["available"])
        self.assertFalse(result["cameraCalibrationAccepted"])

    def test_invalid_percentile_rejects(self):
        self.matrix["scenes"][0]["sensorUnpackedAudit"]["p99_code"] = 135
        self.check_rejected()

    def test_nonfinite_jpeg_rmse_rejects(self):
        self.matrix["scenes"][0]["routes"][1]["vsPairedCameraJpeg"]["rmse"] = float("nan")
        self.check_rejected()

    def test_missing_comparison_route_rejects(self):
        del self.matrix["scenes"][2]["routes"][2]
        self.check_rejected()

    def test_non_numeric_iso_is_missing_coverage_not_fake_iso(self):
        self.matrix["scenes"][1]["cameraEvidence"]["isoText"] = "unknown"
        result = evidence.analyze(self.manifest, self.matrix)
        self.assertEqual(len(result["coverage"]["distinctIso"]), 2)
        self.assertTrue(any("ISO" in gap for gap in result["coverage"]["remainingEvidence"]))
        self.assertFalse(result["cameraCalibrationAccepted"])

    def test_save_report_is_atomic_and_does_not_copy_image(self):
        output = evidence.analyze(self.manifest, self.matrix)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "evidence.json"
            evidence.save_atomic(path, output)
            on_disk = json.loads(path.read_text(encoding="utf-8"))
            self.assertFalse(on_disk["cameraCalibrationAccepted"])
            self.assertEqual(on_disk["source"]["sceneCount"], 3)
            self.assertEqual(list(Path(directory).iterdir()), [path])


if __name__ == "__main__":
    unittest.main()
