"""Enforce opt-in FP32 trial refusal and real-sensor readiness on CI.

This does NOT supply an invented camera calibration or invoke a real-photo FP32
candidate. A successful result explicitly states the experimental path was
not executed and is not approved for default RAW rendering.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
import subprocess
import sys
from tempfile import TemporaryDirectory


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def execute(exe: Path, mode: str, source: Path, target: Path, success: bool) -> None:
    command = [str(exe), mode, str(source), str(target)]
    result = subprocess.run(command, capture_output=True, text=True, timeout=90)
    check((result.returncode == 0) == success,
          f"{mode} returned {result.returncode}, expected success={success}: {result.stderr[-2000:]}")


def validate(exe: Path, evidence: Path, output: Path) -> None:
    check(exe.is_file() and evidence.is_file(), "Native trial CLI or real Sony evidence missing")
    check(not output.exists(), "Refuse to overwrite an existing readiness report")
    actual = json.loads(evidence.read_text(encoding="utf-8"))
    check(actual["evidenceIntegrityPassed"] is True, "Real camera evidence failed")
    check(actual["cameraCalibrationAccepted"] is False, "Unverified camera calibration was accepted")
    with TemporaryDirectory(prefix="jix-float-trial-") as root:
        temp = Path(root)
        execute(exe, "--readiness", evidence, output, True)
        result = json.loads(output.read_text(encoding="utf-8"))
        check(result["status"] == "not-executed" and
              result["realRawTrialExecuted"] is False and
              result["cameraCalibrationAccepted"] is False and
              result["defaultRawReplacementAllowed"] is False,
              "Unverified camera source was erroneously promoted")
        original = hashlib.sha256(output.read_bytes()).hexdigest()
        execute(exe, "--readiness", evidence, output, False)
        check(hashlib.sha256(output.read_bytes()).hexdigest() == original,
              "Existing evidence report overwritten")

        altered = dict(actual)
        altered["cameraCalibrationAccepted"] = True
        unsafe = temp / "unsafe.json"
        unsafe.write_text(json.dumps(altered), encoding="utf-8")
        denied = temp / "unsafe-output.json"
        execute(exe, "--readiness", unsafe, denied, False)
        check(not denied.exists(), "False camera-calibration state wrote an output")

        raw = temp / "fake.arw"
        raw.write_bytes(b"This is not a real RAW")
        sensor_evidence = temp / "sensor-evidence.txt"
        sensor_evidence.write_bytes(b"Sensor calibration research evidence test bytes")
        color_evidence = temp / "color-evidence.txt"
        color_evidence.write_bytes(b"Separate color calibration research evidence test bytes")
        sensor_sha = hashlib.sha256(sensor_evidence.read_bytes()).hexdigest()
        color_sha = hashlib.sha256(color_evidence.read_bytes()).hexdigest()
        plan = {
            "schema": 1,
            "purpose": "experimental-research-only",
            "source": {"file": str(raw), "sha256": "a" * 64},
            "sensorCalibration": {
                "sha256": sensor_sha,
                "evidenceFile": str(sensor_evidence),
                "provenance": "Independent calibration required; test fixture only",
                "cfa": [0, 1, 3, 2],
                "black": [0, 0, 0, 0],
                "white": [1000, 1000, 1000, 1000],
            },
            "colorCalibration": {
                "sha256": color_sha,
                "evidenceFile": str(color_evidence),
                "provenance": "Independent matrix required; test fixture only",
                "whiteBalanceBySite": [1, 1, 1, 1],
                "cameraToProPhoto": [1, 0, 0, 0, 1, 0, 0, 0, 1],
            },
            "sampleRoi": [0, 0, 4, 4],
            "outputRoi": [0, 0, 4, 4],
        }
        plan_file = temp / "invalid-source-plan.json"
        plan_file.write_text(json.dumps(plan), encoding="utf-8")
        never_written = temp / "failed-source-report.json"
        execute(exe, "--run", plan_file, never_written, False)
        check(not never_written.exists(), "Invalid RAW generated an experimental report")
        # A correct-looking profile hash must actually match independent local
        # provenance bytes before even attempting RAW decoding.
        plan["sensorCalibration"]["sha256"] = "b" * 64
        plan_file.write_text(json.dumps(plan), encoding="utf-8")
        execute(exe, "--run", plan_file, never_written, False)
        check(not never_written.exists(), "Unverifiable sensor evidence produced a report")
        plan["sensorCalibration"]["sha256"] = sensor_sha
        plan["colorCalibration"]["cameraToProPhoto"] = [0] * 9
        plan_file.write_text(json.dumps(plan), encoding="utf-8")
        execute(exe, "--run", plan_file, never_written, False)
        check(not never_written.exists(), "Invalid matrix produced a report")

        plan["colorCalibration"]["cameraToProPhoto"] = [1, 0, 0, 0, 1, 0, 0, 0, 1]
        plan["purpose"] = "production-enable"
        plan_file.write_text(json.dumps(plan), encoding="utf-8")
        execute(exe, "--run", plan_file, never_written, False)
        check(not never_written.exists(), "Unapproved trial mode produced an output")

    print(json.dumps({
        "stage": "F3-Stage6C2-readiness",
        "realCameraSensorEvidencePassed": True,
        "trialExecutableValidated": True,
        "realRawTrialExecuted": False,
        "cameraCalibrationAccepted": False,
        "defaultRawReplacementAllowed": False,
        "source": actual["source"],
    }, indent=2))


if __name__ == "__main__":
    if len(sys.argv) != 4:
        raise SystemExit("Usage: ValidateFloatRawTrial.py native-exe sensor-evidence.json new-output.json")
    validate(Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve(),
             Path(sys.argv[3]).resolve())
