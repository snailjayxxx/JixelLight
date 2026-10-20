"""Exercise the offline pre-demosaic RAW probe using public checksum-pinned
Sony fixtures in CI. It never reads a private user's Drive IDs or photos.
"""
from __future__ import annotations
import hashlib
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def require(value: bool, why: str) -> None:
    if not value:
        raise AssertionError(why)


def call(exe: Path, manifest: Path, report: Path, success: bool) -> str:
    proc = subprocess.run([str(exe), str(manifest), str(report)],
                          capture_output=True, text=True, timeout=180)
    require((proc.returncode == 0) == success,
            f"Unexpected offline RAW probe exit {proc.returncode}: {proc.stderr[-1000:]}")
    return proc.stdout


def verify(exe: Path, fixture_manifest: Path, root: Path, output: Path) -> None:
    require(exe.is_file(), "Native offline RAW probe missing")
    require(not output.exists(), "New output file required")
    spec = json.loads(fixture_manifest.read_text(encoding="utf-8"))
    pinned = {x["file"]: x for x in spec["items"]}
    choose = [spec["pairs"][0], spec["pairs"][-1]]
    pairs = []
    for pair in choose:
        raw, jpg = pinned[pair["raw"]], pinned[pair["jpeg"]]
        require((root / raw["file"]).is_file() and (root / jpg["file"]).is_file(),
                "Checksum-pinned real Sony fixtures are required")
        pairs.append({
            "id": pair["id"], "role": pair["role"],
            "expectedCameraModel": spec["expected"]["model"],
            "source": {"file": str(root / raw["file"]), "sha256": raw["sha256"]},
            "jpeg": {"file": str(root / jpg["file"]), "sha256": jpg["sha256"]},
        })
    with TemporaryDirectory(prefix="jix-offline-raw-probe-") as td:
        directory = Path(td)
        manifest = directory / "public-fixture-probe.json"
        requested = {"schema": 1, "purpose": "offline-local-raw-sensor-audit-v1",
                     "cases": pairs}
        manifest.write_text(json.dumps(requested), encoding="utf-8")
        call(exe, manifest, output, True)
        content = output.read_bytes()
        report = json.loads(content)
        require(report["stage"] == "F3-Stage6C3-offline-private-unpacked-audit",
                "Unexpected offline probe schema")
        require(report["evidenceIntegrityPassed"] is True and
                report["realRawSourceUnpackPassed"] is True,
                "Public real-file RAW unpack failed")
        require(report["fullRgbDevelopExecuted"] is False and
                report["realFP32TrialExecuted"] is False and
                report["cameraCalibrationAccepted"] is False and
                report["defaultRawReplacementAllowed"] is False,
                "Unpacked data incorrectly interpreted as scene-linear calibrated RAW")
        require(report["trainCount"] == 1 and report["heldoutCount"] == 1 and
                report["pairedCameraJpegCount"] == 2,
                "No independent pair/holdout identity")
        observed = {c["id"]: c for c in report["cases"]}
        require(set(observed) == set(x["id"] for x in pairs),
                "Unexpected/duplicate real-file cases")
        for selected in pairs:
            item = observed[selected["id"]]
            require(item["cameraModel"] == spec["expected"]["model"] and
                    item["rawSourceSha256"] == selected["source"]["sha256"],
                    "Camera and raw-file provenance mismatch")
            require(item["cameraJpeg"]["verified"] is True and
                    item["cameraJpeg"]["jpegSourceSha256"] == selected["jpeg"]["sha256"],
                    "Paired original camera JPEG rejected")
            facts = item["preDemosaic"]
            audit = facts["sensorAudit"]
            require(facts["available"] is True and audit["available"] is True and
                    audit["rgb_demosaiced"] is False and
                    audit["black_subtracted"] is False and
                    audit["sample_count"] > 1_000_000 and
                    len(audit["pixel_sha256"]) == 64,
                    "Native U16 sensor audit invalid")
        result_text = json.dumps(report, ensure_ascii=False)
        require(all(s not in result_text for s in (str(root),
                "google_drive", "drive.google.com", "source.file", "/home/runner/",
                "D:\\a\\")),
                "A private path / cloud source escaped into the numeric report")
        prior = hashlib.sha256(content).hexdigest()
        call(exe, manifest, output, False)
        require(hashlib.sha256(output.read_bytes()).hexdigest() == prior,
                "Existing report was overwritten")
        duplicate = {"schema": 1, "purpose": requested["purpose"],
                     "cases": [pairs[0], {**pairs[0], "id": "duplicated-holdout",
                                         "role": "validation"}]}
        bad = directory / "duplicate.json"
        bad.write_text(json.dumps(duplicate), encoding="utf-8")
        rejected = directory / "bad-output.json"
        call(exe, bad, rejected, False)
        require(not rejected.exists(), "Duplicate source was accepted")
        tampered = json.loads(json.dumps(requested))
        tampered["cases"][0]["source"]["sha256"] = "a" * 64
        changed = directory / "tampered.json"
        changed.write_text(json.dumps(tampered), encoding="utf-8")
        call(exe, changed, rejected, False)
        require(not rejected.exists(), "Invalid RAW hash produced an audit")

    print(json.dumps({"stage": "F3-Stage6C3",
                      "source": "checksum-pinned public ARW+JPEG fixtures",
                      "auditedCases": len(pairs), "heldoutCases": 1,
                      "userPrivateFilesProcessedByCi": False,
                      "realFP32TrialExecuted": False,
                      "cameraCalibrationAccepted": False,
                      "result": "success"}, indent=2))


if __name__ == "__main__":
    if len(sys.argv) != 5:
        raise SystemExit("Usage: ValidateOfflineRawProbe.py executable pinned-fixtures.json fixtures-folder NEW-report.json")
    verify(Path(sys.argv[1]).resolve(),Path(sys.argv[2]).resolve(),
           Path(sys.argv[3]).resolve(),Path(sys.argv[4]).resolve())
