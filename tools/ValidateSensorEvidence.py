"""Stage 6C real-sample *evidence* gate, not a camera color-calibration fitter.

Read checksum-pinned fixture metadata and actual native RawRenderMatrix output.
Only JSON statistics/provenance are emitted; no original image data is copied.
Camera JPEGs include in-camera looks and are never treated as sensor-linear truth.
"""
from __future__ import annotations

import json
import math
import os
from pathlib import Path
import re
import sys
import tempfile
from typing import Any

_SHA = re.compile(r"[0-9a-f]{64}\Z")
_NUMBER = re.compile(r"^[0-9]+(?:\.[0-9]+)?$")
_MAX_SENSOR_SITES = 200_000_000


class EvidenceError(ValueError):
    pass


def require(condition: bool, message: str) -> None:
    if not condition:
        raise EvidenceError(message)


def obj(value: Any, label: str) -> dict:
    require(type(value) is dict, f"{label}: expected object")
    return value


def seq(value: Any, label: str, length: int | None = None) -> list:
    require(type(value) is list, f"{label}: expected array")
    if length is not None:
        require(len(value) == length, f"{label}: wrong length")
    return value


def digest(value: Any, label: str) -> str:
    require(type(value) is str and _SHA.fullmatch(value) is not None,
            f"{label}: invalid lower-case SHA-256")
    return value


def integer(value: Any, label: str, lo: int = 0, hi: int = _MAX_SENSOR_SITES) -> int:
    require(type(value) is int and lo <= value <= hi, f"{label}: invalid integer/range")
    return value


def finite(value: Any, label: str, lo: float, hi: float) -> float:
    require(type(value) in (int, float) and math.isfinite(value) and lo <= value <= hi,
            f"{label}: non-finite or out of range")
    return float(value)


def iso_number(text: Any) -> int | None:
    if not isinstance(text, str):
        return None
    cleaned = text.strip()
    if not cleaned.isdecimal():
        return None
    number = int(cleaned)
    return number if 1 <= number <= 1_000_000 else None


def shutter_seconds(text: Any) -> float | None:
    if not isinstance(text, str):
        return None
    s = text.strip()
    if "/" in s:
        a, slash, b = s.partition("/")
        if not (a.isdecimal() and b.isdecimal() and int(b) > 0):
            return None
        result = int(a) / int(b)
    elif _NUMBER.fullmatch(s):
        result = float(s)
    else:
        return None
    return result if math.isfinite(result) and 0 < result <= 86400 else None


def audit_evidence(audit: Any, *, required: bool) -> dict:
    a = obj(audit, "sensorUnpackedAudit")
    require(a.get("schema") == 1 and type(a.get("available")) is bool,
            "Sensor audit schema/availability missing")
    if not a["available"]:
        require(type(a.get("reason")) is str and bool(a["reason"]),
                "Unavailable sensor audit lacks a reason")
        require(not required, "Required real Bayer sensor audit unavailable: " + a["reason"])
        return {"available": False, "reason": a["reason"]}
    require(a.get("rgb_demosaiced") is False and a.get("black_subtracted") is False,
            "Sensor audit is not pre-development single-channel sensor data")
    raw_w = integer(a.get("raw_width"), "raw_width", 1, 32768)
    raw_h = integer(a.get("raw_height"), "raw_height", 1, 32768)
    bounds = seq(a.get("active_bounds"), "active_bounds", 4)
    x, y = bounds[:2]
    w, h = bounds[2:]
    integer(x, "active x", 0, raw_w - 1)
    integer(y, "active y", 0, raw_h - 1)
    integer(w, "active width", 1, raw_w)
    integer(h, "active height", 1, raw_h)
    require(x + w <= raw_w and y + h <= raw_h, "Sensor active bounds outside RAW array")
    count = integer(a.get("sample_count"), "sensor samples", 1)
    require(count == w * h, "Active sensor area and sample count differ")
    minimum = integer(a.get("minimum_code"), "minimum_code", 0, 65535)
    maximum = integer(a.get("maximum_code"), "maximum_code", minimum, 65535)
    mean = finite(a.get("mean_code"), "mean_code", minimum, maximum)
    p50 = integer(a.get("p50_code"), "p50", minimum, maximum)
    p99 = integer(a.get("p99_code"), "p99", p50, maximum)
    p999 = integer(a.get("p999_code"), "p999", p99, maximum)
    black = integer(a.get("nominal_black_code"), "nominal black", 0, 65535)
    white = integer(a.get("nominal_white_code"), "nominal white", 1, 65535)
    require(black < white, "Nominal white must exceed nominal black")
    at_white = integer(a.get("at_or_above_nominal_white_code"), "raw >= nominal white", 0, count)
    at_black = integer(a.get("at_or_below_nominal_black_code"), "raw <= nominal black", 0, count)
    at_65535 = integer(a.get("at_code_65535"), "raw == 65535", 0, count)
    require(at_65535 <= at_white, "Code 65535 cannot be less than nominal white")
    pattern = seq(a.get("parity_2x2_raw_grid_not_cfa_color"), "raw 2x2 parity", 4)
    total = 0
    means = []
    for i, entry in enumerate(pattern):
        e = obj(entry, f"raw parity[{i}]")
        n = integer(e.get("pixels"), f"raw parity[{i}].pixels", 1, count)
        low = integer(e.get("min"), f"raw parity[{i}].min", minimum, maximum)
        high = integer(e.get("max"), f"raw parity[{i}].max", low, maximum)
        means.append(finite(e.get("mean"), f"raw parity[{i}].mean", low, high))
        total += n
    require(total == count, "Sensor parity counts do not cover active samples")
    pixel_sha = digest(a.get("pixel_sha256"), "sensor pixel fingerprint")
    cfa = a.get("cfa_kind")
    require(cfa in ("bayer_like_filters", "xtrans_6x6", "unknown_or_non_mosaic"),
            "Unexpected/absent CFA kind")
    return {
        "available": True, "rawArray": [raw_w, raw_h],
        "activeBounds": bounds, "sampleCount": count,
        "pixelSha256": pixel_sha,
        "unpackedCode": {"min": minimum, "mean": mean, "p50": p50, "p99": p99,
                         "p999": p999, "max": maximum},
        "nominalBlack": black, "nominalWhite": white,
        "atOrBelowNominalBlack": at_black,
        "atOrAboveNominalWhite": at_white,
        "atCode65535": at_65535,
        "nominalWhiteTailFraction": at_white / count,
        "parityMeansNotCfaColors": means,
        "cfaKindUnverifiedPhase": cfa,
        "libRawVersion": str(a.get("libraw_version", "")),
        "limitations": [
            "Unpacked CFA code distribution is NOT RGB or a measured sensor response curve",
            "Nominal-white tail is NOT recoverable highlight dynamic range",
            "2x2 parity cells do NOT establish absolute R/G/B CFA phase",
        ],
    }


def route_metrics(scene: dict) -> dict:
    routes = seq(scene.get("routes"), "RAW/JPEG comparison routes")
    indexed = {}
    for entry in routes:
        route = obj(entry, "route")
        name = route.get("name")
        require(type(name) is str and name not in indexed, "Duplicate/invalid comparison route")
        indexed[name] = route
    for name in ("paired-camera-jpeg", "cpu-raw-neutral", "cpu-raw-as-shot"):
        require(name in indexed, f"Missing real camera comparison: {name}")
        require(obj(indexed[name].get("stats"), f"{name}.stats").get("valid") is True,
                f"Invalid image statistics for {name}")
    result = {}
    for name, field in (("cpu-raw-neutral", "neutralRmseToCameraJpeg"),
                        ("cpu-raw-as-shot", "asShotRmseToCameraJpeg")):
        m = obj(indexed[name].get("vsPairedCameraJpeg"), name + ".vsPairedCameraJpeg")
        require(m.get("valid") is True, f"Invalid paired-camera JPEG comparator: {name}")
        result[field] = finite(m.get("rmse"), field, 0., 1.)
    camera = obj(indexed["paired-camera-jpeg"]["stats"], "camera JPEG stats")
    result["cameraJpegHighlightClipPercent"] = finite(
        camera.get("highlightClipPercent"), "camera JPEG highlight clip %", 0., 100.)
    return result


def analyze(manifest: Any, matrix: Any) -> dict:
    m = obj(manifest, "fixture manifest")
    report = obj(matrix, "native RAW/JPEG report")
    require(m.get("schema") == 1 and report.get("schema") == 1, "Unsupported evidence schema")
    items = seq(m.get("items"), "pinned public source files")
    pairs = seq(m.get("pairs"), "training and held-out pairs")
    scenes = seq(report.get("scenes"), "native measurement scenes")
    require(2 <= len(pairs) <= 100 and len(items) == 2 * len(pairs),
            "Expected paired RAW/JPEG manifest without missing source files")
    require(report.get("sceneCount") == len(scenes) == len(pairs),
            "Native scene count mismatches pinned fixture list")
    files = {}
    for entry in items:
        item = obj(entry, "fixture")
        filename = item.get("file")
        require(type(filename) is str and filename and Path(filename).name == filename
                and "/" not in filename and "\\" not in filename and filename not in files,
                "Duplicate or unsafe fixture filename")
        files[filename] = digest(item.get("sha256"), "pinned source " + filename)
    pinned = {}
    roles = {"train": 0, "validation": 0}
    raw_sha_seen, jpg_sha_seen = set(), set()
    for entry in pairs:
        pair = obj(entry, "pinned pair")
        ident = pair.get("id")
        role = pair.get("role")
        raw, jpg = pair.get("raw"), pair.get("jpeg")
        require(type(ident) is str and re.fullmatch(r"[A-Za-z0-9][\w-]{0,63}", ident)
                and ident not in pinned, "Duplicate/unsafe paired scene ID")
        require(role in roles, "Each pair needs train or independent validation role")
        require(raw in files and jpg in files and raw != jpg,
                "Pair does not resolve to pinned RAW and JPEG files")
        require(raw.lower().endswith((".arw", ".dng", ".cr3", ".nef", ".raf"))
                and jpg.lower().endswith((".jpg", ".jpeg")), "Invalid RAW/JPEG pairing")
        rh, jh = files[raw], files[jpg]
        require(rh not in raw_sha_seen and jh not in jpg_sha_seen,
                "Training/validation source bytes are duplicated")
        raw_sha_seen.add(rh)
        jpg_sha_seen.add(jh)
        roles[role] += 1
        pinned[ident] = {"role": role, "raw": rh, "jpeg": jh,
                         "auditRequired": pair.get("auditRequired", False)}
        require(type(pair.get("auditRequired", False)) is bool, "auditRequired must be boolean")
    require(roles["train"] >= 1 and roles["validation"] >= 1,
            "No independent train/validation scenes")
    result_scenes = []
    seen_scenes, captures = set(), set()
    models, isos, shutters = set(), set(), set()
    for value in scenes:
        scene = obj(value, "native measurement scene")
        ident = scene.get("id")
        require(ident in pinned and ident not in seen_scenes, "Unexpected/duplicate native scene")
        seen_scenes.add(ident)
        expected = pinned[ident]
        require(scene.get("role") == expected["role"], "Native scene role differs from pinned split")
        require(digest(scene.get("rawSha256"), "native raw SHA") == expected["raw"] and
                digest(scene.get("jpegSha256"), "native jpeg SHA") == expected["jpeg"],
                "Native RAW/JPEG bytes differ from pinned fixture manifest")
        require(not scene.get("decodeError") and not scene.get("metadataError") and
                not scene.get("error"), "Native RAW decode/reference comparison reported an error")
        camera = obj(scene.get("cameraEvidence"), "native camera metadata")
        make = camera.get("make")
        model = camera.get("model")
        capture = camera.get("captureTime")
        require(isinstance(make, str) and bool(make.strip()) and
                isinstance(model, str) and bool(model.strip()), "Camera make/model missing")
        require(isinstance(capture, str) and bool(capture.strip()),
                "Camera capture timestamp missing; cannot prove independent scenes")
        identity = (model, capture, str(camera.get("subSecTime", "")))
        require(identity not in captures, "Duplicate camera capture identity in hold-out split")
        captures.add(identity)
        if "expected" in m and m["expected"].get("model"):
            require(model == m["expected"]["model"], "Fixture RAW camera differs from pinned model")
        models.add(model)
        iso = iso_number(camera.get("isoText"))
        shutter = shutter_seconds(camera.get("shutterText"))
        if iso is not None:
            isos.add(iso)
        if shutter is not None:
            shutters.add(shutter)
        audit = audit_evidence(scene.get("sensorUnpackedAudit"),
                               required=expected["auditRequired"])
        compare = route_metrics(scene)
        result_scenes.append({
            "id": ident, "role": expected["role"], "model": model, "make": make,
            "rawSha256": expected["raw"], "jpegSha256": expected["jpeg"],
            "iso": iso, "shutterSeconds": shutter,
            "captureTime": capture,
            "sensorAudit": audit, "pairedCameraJpeg": compare,
        })
    require(len(seen_scenes) == len(pairs), "Pinned scenes missing from native report")
    result_scenes.sort(key=lambda scene: scene["id"])
    coverage_gaps = []
    if len(models) < 2:
        coverage_gaps.append("only one camera model")
    if len(isos) < 3:
        coverage_gaps.append("fewer than 3 independent ISO settings")
    if len(shutters) < 3:
        coverage_gaps.append("fewer than 3 distinct shutter exposures")
    if sum(1 for s in result_scenes if s["sensorAudit"]["available"]) < len(result_scenes):
        coverage_gaps.append("one or more sensor mosaics unavailable")
    coverage_gaps.extend([
        "no independently measured CFA phase / black-white / gain-nonlinearity chart",
        "no independently verified D50 camera matrix or 4-site white balance",
        "paired camera JPEGs are tone/look encoded, not scene-linear ground truth",
        "no Stage 6B real-file output-vs-reference color-error/DeltaE validation",
        "held-out scenes cover too few cameras, illuminants, exposures and ISO values",
    ])
    return {
        "schema": 1, "stage": "F3-Stage6C1-real-sensor-evidence",
        "evidenceIntegrityPassed": True,
        "cameraCalibrationAccepted": False,
        "defaultRawReplacementAllowed": False,
        "stage6BRealCameraFidelityValidated": False,
        "interpretation": "Pinned real source audits and camera-JPEG regression; not a physical calibration",
        "source": {"manifestSha256Required": True,
                   "nativeRawRenderMatrix": str(report.get("engineVersion", "")),
                   "sceneCount": len(result_scenes),
                   "trainCount": roles["train"], "heldoutCount": roles["validation"]},
        "coverage": {"models": sorted(models), "distinctIso": sorted(isos),
                     "distinctShutterSeconds": sorted(shutters),
                     "remainingEvidence": coverage_gaps},
        "scenes": result_scenes,
    }


def save_atomic(path: Path, data: dict) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp_name = None
    try:
        with tempfile.NamedTemporaryFile("w", encoding="utf-8", dir=path.parent,
                                         prefix=".sensor-evidence-", suffix=".tmp",
                                         delete=False) as tmp:
            tmp_name = tmp.name
            json.dump(data, tmp, indent=2, ensure_ascii=False, allow_nan=False)
            tmp.write("\n")
        os.replace(tmp_name, path)
    finally:
        if tmp_name is not None:
            Path(tmp_name).unlink(missing_ok=True)


def main(argv: list[str]) -> int:
    if len(argv) != 4:
        print("Usage: ValidateSensorEvidence.py pinned-manifest.json raw-render-matrix.json output.json",
              file=sys.stderr)
        return 2
    manifest_path, matrix_path, output_path = map(Path, argv[1:])
    try:
        evidence = analyze(json.loads(manifest_path.read_text(encoding="utf-8")),
                           json.loads(matrix_path.read_text(encoding="utf-8")))
        # Refuse a CI 'success' report which misrepresents camera calibration.
        require(evidence["evidenceIntegrityPassed"] and
                evidence["cameraCalibrationAccepted"] is False and
                evidence["defaultRawReplacementAllowed"] is False,
                "Unsafe camera calibration acceptance state")
        save_atomic(output_path, evidence)
        print(json.dumps({"evidenceIntegrityPassed": True,
                          "cameraCalibrationAccepted": False,
                          "scenes": evidence["source"]["sceneCount"],
                          "remainingEvidence": evidence["coverage"]["remainingEvidence"]},
                         ensure_ascii=False, indent=2))
        return 0
    except (OSError, UnicodeError, ValueError, json.JSONDecodeError) as exc:
        print(f"Sensor evidence REJECTED: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
