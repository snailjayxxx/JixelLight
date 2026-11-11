"""Validate a native committed preview, preserving original reports and limits.

Live pressure input still uses the existing CPU overlay. The Sony mode requires
the original full CPU statistics/three plots and independent exact PNG16/ICC
export verification; native diagnostic samples are not full-image GPU parity.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from NativeBrushEvidence import BUDGET, MODES, SONY_PIXELS, SONY_SHA256, commit, validate_report

CAPTURES = {"route": 4, "delivery": 7, "pending": 1, "retired-render": 1, "monitor": 3, "sony": 3}
FLAGS = {"route": "--native-brush-route-smoke-report", "monitor": "--native-brush-monitor-smoke-report", "sony": "--native-brush-sony-smoke-report"}
QML_ERRORS = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]", "QQmlApplicationEngine failed", "Binding loop detected")


def digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def backend_environment(environment, home, mode, platform=None):
    platform = platform or sys.platform
    env = environment.copy()
    for key in ("HOME", "USERPROFILE", "XDG_CACHE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
        env[key] = str(home)
    for key in ("JIXELLIGHT_FORCE_CPU", "JIXELLIGHT_REQUIRE_GPU", "QT_QUICK_BACKEND", "JIXELLIGHT_PRIVATE_QML_ROOT", "QSG_RHI_PREFER_SOFTWARE_RENDERER", "QSG_RHI_BACKEND", "QT_SCALE_FACTOR"):
        env.pop(key, None)
    qpa, backend, expected = ("windows", "d3d11", "Direct3D 11") if platform == "win32" else ("cocoa", "metal", "Metal") if platform == "darwin" else ("xcb", "opengl", "OpenGL")
    env.update(QT_QPA_PLATFORM=qpa, QSG_RHI_BACKEND=backend, QSG_RENDER_LOOP="threaded", JIXELLIGHT_REQUIRE_GPU="1", JIXELLIGHT_CPU_THREADS="2", QML_DISABLE_DISK_CACHE="1")
    if platform == "win32":
        env["QSG_RHI_PREFER_SOFTWARE_RENDERER"] = "1"
    elif platform.startswith("linux"):
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    if mode == "sony":
        # Only the new native-preview case uses this explicit display scale.
        # Every original57 package mode retains its existing environment.
        env["QT_SCALE_FACTOR"] = ".5"
    return env, qpa, expected


def command(binary, native, mode, raw=None):
    args = [str(binary), FLAGS.get(mode, "--native-brush-delivery-smoke-report"), str(native)]
    if mode in ("delivery", "pending", "retired-render"):
        args += ["--native-brush-delivery-focus", "all" if mode == "delivery" else mode]
    if mode == "sony":
        if raw is None:
            raise ValueError("Native Sony validation requires the pinned RAW")
        args.append(str(raw))
    return args


def png_evidence(path, row):
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n"
    at, payload, shape, ended = 8, [], None, False
    while at < len(data):
        n, = struct.unpack(">I", data[at:at+4])
        kind, chunk = data[at+4:at+8], data[at+8:at+8+n]
        crc, = struct.unpack(">I", data[at+8+n:at+12+n])
        assert zlib.crc32(kind+chunk) & 0xffffffff == crc
        at += n+12
        if kind == b"IHDR":
            shape = struct.unpack(">IIBBBBB", chunk)
        elif kind == b"IDAT":
            payload.append(chunk)
        elif kind == b"IEND":
            ended = True
            break
    assert ended and at == len(data) and shape is not None
    width, height, depth, color, compression, filtering, interlace = shape
    assert (width, height) == (row["width"], row["height"])
    assert depth == 8 and color in (2, 6) and compression == filtering == interlace == 0
    stride = width * (3 if color == 2 else 4)
    pixels = zlib.decompress(b"".join(payload))
    assert len(pixels) == (stride+1)*height
    assert all(pixels[y*(stride+1)] <= 4 for y in range(height))
    value = digest(path)
    if "sha256" in row:
        assert row["sha256"] == value
    return {"sha256": value, "width": width, "height": height, "bytes": len(data), "CRC_all_chunks": True, "inflated_rows_checked": height}


def reject_alias(path, protected):
    for original in protected:
        if path.resolve() == original.resolve() or (path.exists() and original.exists() and os.path.samefile(path, original)):
            raise ValueError("Evidence output must not replace an input or executable")


def validate_native_brush(binary, environment, home, report, mode, *, raw=None, verifier=None, expected_commit=None):
    binary, home, report = Path(binary).resolve(), Path(home).resolve(), Path(report).resolve()
    if mode not in MODES:
        raise ValueError("Unknown native validation mode")
    if mode == "sony" and (raw is None or verifier is None):
        raise ValueError("Sony mode requires a RAW and independent export verifier")
    if mode != "sony" and (raw is not None or verifier is not None):
        raise ValueError("Sony inputs are valid only for Sony mode")
    raw = Path(raw).resolve() if raw is not None else None
    verifier = Path(verifier).resolve() if verifier is not None else None
    protected = [p for p in (binary, raw, verifier) if p is not None]
    for suffix in ("", ".native.json", ".log", ".frozen.json", ".verifier.json"):
        reject_alias(Path(str(report)+suffix), protected)
    if any(Path(str(report)+suffix).exists() for suffix in ("", ".native.json", ".log", ".frozen.json", ".verifier.json")):
        raise ValueError("Preserve an existing result; choose a fresh evidence path")
    if raw is not None and (not raw.is_file() or raw.stat().st_size != 69120000 or digest(raw) != SONY_SHA256):
        raise ValueError("The required pinned Sony07 ARW is missing or differs")
    if not binary.is_file() or verifier is not None and not verifier.is_file():
        raise ValueError("The deployed binary/verifier is absent")
    home.mkdir(parents=True, exist_ok=True)
    report.parent.mkdir(parents=True, exist_ok=True)
    private = Path(tempfile.mkdtemp(prefix="jixel-native-Sony-private-")) if mode == "sony" else report.parent / (report.stem+"-captures")
    private.mkdir(parents=True, exist_ok=True)
    native = private / "native.json"
    env, qpa, backend = backend_environment(environment, home, mode)
    result = {"mode": "native-brush-"+mode, "passed": False, "actual_returncode": None,
              "native_report_preserved": True, "physical_tablet_or_full_raw_jitter_accepted": False,
              "full_native_image_CPU_parity_claim": False, "photographs_published": False,
              "live_overlay_remains_CPU": True}
    logs, capture_proofs, data, cleanup_photo = [], [], {}, None
    try:
        started = time.monotonic()
        run = subprocess.run(command(binary, native, mode, raw), cwd=home, env=env,
                             capture_output=True, text=True, errors="replace", timeout=185 if mode == "sony" else 80)
        result["actual_returncode"] = run.returncode
        logs.append(run.stdout+"\n"+run.stderr)
        if not native.is_file():
            raise ValueError("The native process produced no report")
        shutil.copyfile(native, Path(str(report)+".native.json"))
        data = json.loads(native.read_text(encoding="utf-8"))
        result["source_commit"] = data.get("source_commit")
        if run.returncode != 0:
            raise ValueError("The native process did not complete successfully")
        validate_report(data, mode, expected_commit=expected_commit, expected_backend=backend, expected_qpa=qpa)
        for j, row in enumerate(data["captures"]):
            image = Path(row["path"])
            assert image.resolve().is_relative_to(private.resolve()) and image.suffix.lower() == ".png"
            if mode == "sony":
                assert image.name.endswith("-controls.png")
            proof = png_evidence(image, row)
            destination = Path(str(report)+f".capture-{j}.png")
            reject_alias(destination, protected)
            shutil.copyfile(image, destination)
            proof["artifact"] = destination.name
            capture_proofs.append(proof)
        assert len(capture_proofs) == CAPTURES[mode]
        if mode == "sony":
            frozen, photo = Path(str(native)+".frozen.json"), Path(str(native)+".export.png")
            if not frozen.is_file() or not photo.is_file():
                raise ValueError("The actual frozen UI export is incomplete")
            frozen_data = json.loads(frozen.read_text(encoding="utf-8"))
            commit(frozen_data["source_commit"], data["source_commit"])
            assert frozen_data["source_sha256"] == SONY_SHA256
            shutil.copyfile(frozen, Path(str(report)+".frozen.json"))
            verification = subprocess.run([str(verifier), str(raw), str(frozen), str(photo)],
                cwd=home, env=env, capture_output=True, text=True, errors="replace", timeout=125)
            logs.append(verification.stderr)
            Path(str(report)+".verifier.json").write_text(verification.stdout, encoding="utf-8")
            check = json.loads(verification.stdout)
            assert verification.returncode == 0 and check["passed"] is True
            assert check["exact_pixels_and_ICC"] is True and check["plan_hash_matches_actual_UI_export"] is True
            commit(check["source_commit"], data["source_commit"])
            assert check["plan_data_sha256"] == frozen_data["plan_data_sha256"]
            assert check["raw_sha256"] == SONY_SHA256 and check["source_pixels"] == check["decoded_pixels"] == SONY_PIXELS
            assert type(check["operation_budget_bytes"]) is int and check["operation_budget_bytes"] == BUDGET
            assert type(check["operation_peak_reserved_bytes"]) is int and 0 < check["operation_peak_reserved_bytes"] <= BUDGET
            assert type(check["operation_end_charged_bytes"]) is int and check["operation_end_charged_bytes"] == 0
            assert digest(raw) == SONY_SHA256
            photo_sha = digest(photo)
            combined = round((time.monotonic()-started)*1000)-data["gui_elapsed_ms"]
            assert 0 < combined < 120000
            result["export_reference_wall_ms"] = combined
            result["independent_exact_export_verified"] = True
            result["export_sha256"] = photo_sha
            result["source_unchanged_verified_after_process"] = True
            result["native_pending_flags_not_rewritten"] = True
            # The complete review photograph never enters an artifact folder.
            # Failed exports stay at their private original path for diagnosis.
            cleanup_photo = photo
        errors = [line for log in logs for line in log.splitlines() if any(mark in line for mark in QML_ERRORS)]
        if errors:
            raise ValueError("QML runtime errors: "+"; ".join(errors))
        if cleanup_photo is not None:
            cleanup_photo.unlink()
        result.update(passed=True, captures=capture_proofs, native_report_sha256=digest(Path(str(report)+".native.json")),
                      actual_qpa=data["actual_qpa"], actual_backend=data.get("actual_backend", data.get("backend")),
                      report=data)
    except (AssertionError, KeyError, TypeError, ValueError, OSError, subprocess.TimeoutExpired) as error:
        if isinstance(error, subprocess.TimeoutExpired):
            for value in (error.stdout, error.stderr):
                if value:
                    logs.append(value.decode("utf-8", errors="replace") if isinstance(value, bytes) else value)
        result["error"] = type(error).__name__+": "+str(error)
        result["captures"] = capture_proofs
        if native.is_file() and not Path(str(report)+".native.json").exists():
            shutil.copyfile(native, Path(str(report)+".native.json"))
        if mode == "sony":
            result["private_failure_directory"] = str(private)
    Path(str(report)+".log").write_text("\n".join(logs), encoding="utf-8")
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2)+"\n", encoding="utf-8")
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("mode", choices=MODES)
    parser.add_argument("--raw", type=Path)
    parser.add_argument("--verifier", type=Path)
    parser.add_argument("--expected-commit")
    args = parser.parse_args()
    try:
        with tempfile.TemporaryDirectory(prefix="jixel-native-home-") as home:
            result = validate_native_brush(args.binary, os.environ.copy(), Path(home), args.report, args.mode,
                raw=args.raw, verifier=args.verifier, expected_commit=args.expected_commit)
    except ValueError as error:
        print(json.dumps({"mode": "native-brush-"+args.mode, "passed": False, "error": str(error)}), flush=True)
        return 2
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
