"""Validate actual photo-canvas zero pressure and stationary positive pressure."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import zlib

LABELS = {"before", "zero-new", "pulse", "undo", "redo", "zero-existing"}
REQUIRED = ("settings_isolated", "source_unchanged", "actual_photo_app_UI",
            "controlled_qpa_tablet", "window_focus_ready", "zero_new_noop",
            "zero_new_pixels_unchanged", "positive_move_delivered", "stationary_peak_preserved",
            "stationary_pixel_changed", "single_undo", "undo_pixels_restored", "zero_kept_redo",
            "redo_restored", "redo_pixels_restored", "zero_existing_paint_noop",
            "zero_existing_erase_noop", "zero_existing_pixels_unchanged")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def png_dimensions(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("Invalid PNG signature")
    pos, dimensions, ended = 8, None, False
    while pos < len(data):
        if pos + 12 > len(data):
            raise ValueError("Truncated PNG chunk")
        size = struct.unpack(">I", data[pos:pos + 4])[0]
        kind, end = data[pos + 4:pos + 8], pos + size + 12
        if end > len(data) or zlib.crc32(data[pos + 4:pos + 8 + size]) & 0xffffffff != struct.unpack(">I", data[pos + 8 + size:end])[0]:
            raise ValueError("PNG chunk CRC mismatch")
        if kind == b"IHDR":
            if dimensions is not None or size != 13:
                raise ValueError("Invalid PNG header")
            dimensions = struct.unpack(">II", data[pos + 8:pos + 16])
        pos = end
        if kind == b"IEND":
            ended = size == 0 and pos == len(data)
            break
    if not ended or not dimensions or min(dimensions) <= 0:
        raise ValueError("Incomplete PNG")
    return dimensions


def copy_captures(data: dict, native_path: Path, report: Path) -> tuple[dict[str, str], list[str]]:
    copied, errors, seen = {}, [], set()
    captures = data.get("captures", []) if isinstance(data, dict) else []
    if not isinstance(captures, list) or len(captures) > len(LABELS):
        return copied, ["Invalid capture list"]
    for entry in captures:
        try:
            label = entry["label"]
            if label not in LABELS or label in seen:
                raise ValueError("Invalid or duplicate capture label")
            seen.add(label)
            path = Path(str(native_path) + "." + label + "-owned.png")
            if Path(entry["path"]) != path or entry["dpr"] != data["dpr"]:
                raise ValueError("Capture path or DPR mismatch")
            if path.is_symlink() or not path.is_file() or path.parent.resolve() != native_path.parent.resolve():
                raise ValueError("Capture is not a regular private output")
            if png_dimensions(path) != (entry["width"], entry["height"]) or sha256(path) != entry["sha256"]:
                raise ValueError("Capture dimensions or SHA256 mismatch")
            target = report.parent / path.name
            if target.exists() or target.is_symlink():
                raise ValueError("Existing capture is preserved")
            with path.open("rb") as source, target.open("xb") as output:
                shutil.copyfileobj(source, output)
            if sha256(target) != entry["sha256"]:
                raise ValueError("Copied capture differs")
            copied[label] = str(target)
        except (OSError, ValueError, TypeError, KeyError, struct.error) as error:
            errors.append(str(error))
    return copied, errors


def validate_brush_pressure_edges(binary: Path, environment: dict[str, str], home: Path,
                                  report: Path, mode: str, *, transparent: bool = False,
                                  qpa: str | None = None) -> dict:
    if mode not in ("cpu", "gpu") or qpa not in (None, "offscreen", "cocoa", "windows", "xcb"):
        raise ValueError("Invalid pressure-edge mode or QPA")
    binary, report = binary.resolve(), report.resolve()
    if not binary.is_file() or report.exists() or list(report.parent.glob(report.stem + ".*")):
        raise ValueError("Use an existing binary and fresh report path")
    home.mkdir(parents=True, exist_ok=True)
    report.parent.mkdir(parents=True, exist_ok=True)
    env = environment.copy()
    for key in ("HOME", "USERPROFILE", "XDG_CACHE_HOME", "XDG_CONFIG_HOME",
                "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
        env[key] = str(home)
    for key in ("JIXELLIGHT_REQUIRE_GPU", "JIXELLIGHT_FORCE_CPU", "QSG_RHI_BACKEND",
                "QSG_RHI_PREFER_SOFTWARE_RENDERER"):
        env.pop(key, None)
    if sys.platform == "darwin":
        env["QT_QPA_PLATFORM"] = "cocoa"
    if mode == "gpu":
        env["JIXELLIGHT_REQUIRE_GPU"] = "1"
        env["QSG_RHI_BACKEND"] = ("d3d11" if sys.platform == "win32" else
                                  "metal" if sys.platform == "darwin" else "opengl")
        if sys.platform == "win32":
            env["QSG_RHI_PREFER_SOFTWARE_RENDERER"] = "1"
        elif sys.platform.startswith("linux"):
            env.update(QT_QPA_PLATFORM="xcb", LIBGL_ALWAYS_SOFTWARE="1")
    else:
        env["JIXELLIGHT_FORCE_CPU"] = "1"
        if sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "offscreen"
    if qpa:
        env["QT_QPA_PLATFORM"] = qpa
    work = Path(tempfile.mkdtemp(prefix="jixellight-pressure-edges-"))
    native = work / (report.stem + ".native.json")
    log = work / (report.stem + ".native.log")
    flag = ("transparent-brush-pressure-edges-smoke-report" if transparent else
            "brush-pressure-edges-smoke-report")
    prefix = "transparent-" if transparent else ""
    result = {"mode": prefix + "brush-pressure-edges-" + mode, "passed": False,
              "physical_tablet_verified": False, "native_gpu_brush": False,
              "native_float_raw_decoder": False, "production_accepted": False,
              "private_output_directory": str(work), "report": {}}
    try:
        with log.open("xb") as stream:
            run = subprocess.run([str(binary), "--" + flag, str(native)], cwd=home, env=env,
                                 stdout=stream, stderr=subprocess.STDOUT, timeout=75)
        result["returncode"] = run.returncode
        data = json.loads(native.read_text(encoding="utf-8"))
        result["report"] = data
        assert run.returncode == 0 and data["passed"] is True and not data["error"]
        assert all(data[key] is True for key in REQUIRED)
        assert all(data[key] is False for key in ("physical_OS_activation_verified", "physical_tablet_verified",
                                                   "native_gpu_brush", "native_float_raw_decoder", "production_accepted"))
        assert data["source_kind"] == "owned-rgba64-png" and data["source_pixels"] == 512 * 320
        assert data["source_alpha_u16"] == (42000 if transparent else 65535)
        assert re.fullmatch(r"[0-9a-f]{64}", data["source_sha256"])
        assert data["source_commit"] and data["source_commit"] != "unknown"
        assert data["injection"] == "Qt-QPA-controlled-pressure-stylus"
        assert type(data["elapsed_ms"]) is int and 0 < data["elapsed_ms"] < data["deadline_ms"] == 60000
        assert type(data["dpr"]) in (int, float) and math.isfinite(data["dpr"]) and data["dpr"] > 0
        assert not qpa or data["actual_qpa"] == qpa
        assert data["gpu_active"] is (mode == "gpu")
        if mode == "gpu":
            backend = "Direct3D 11" if sys.platform == "win32" else "Metal" if sys.platform == "darwin" else "OpenGL"
            assert backend in data["backend"]
        assert abs(data["stationary_peak_pressure"] - .8) < 1e-9 and data["stationary_pixel_delta"] >= 3
        copied, errors = copy_captures(data, native, report)
        result["capture_artifacts"], result["capture_errors"] = copied, errors
        assert not errors and set(copied) == LABELS
        text = log.read_text(encoding="utf-8", errors="replace")
        errors = [line for line in text.splitlines() if any(x in line for x in
                  ("TypeError:", "ReferenceError:", "Unable to assign [undefined]"))]
        result["qml_errors"] = errors
        assert not errors
        result.update(passed=True, captures_verified=True, source_commit=data["source_commit"],
                      backend=data["backend"])
    except (OSError, ValueError, TypeError, KeyError, AssertionError, subprocess.TimeoutExpired) as error:
        result["error_type"] = type(error).__name__
        result["error"] = str(error)
        if not result.get("capture_artifacts") and result.get("report"):
            copied, errors = copy_captures(result["report"], native, report)
            result["failure_capture_artifacts"], result["failure_capture_errors"] = copied, errors
    for source in (native, log):
        if source.is_file() and not source.is_symlink():
            target = report.parent / source.name
            if not target.exists() and not target.is_symlink():
                with source.open("rb") as src, target.open("xb") as dst:
                    shutil.copyfileobj(src, dst)
    with report.open("x", encoding="utf-8") as stream:
        json.dump(result, stream, ensure_ascii=False, indent=2)
        stream.write("\n")
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("executable", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("mode", choices=("cpu", "gpu"))
    parser.add_argument("--transparent", action="store_true")
    parser.add_argument("--qpa", choices=("offscreen", "cocoa", "windows", "xcb"))
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="jixellight-pressure-edges-home-") as home:
        result = validate_brush_pressure_edges(args.executable, os.environ.copy(), Path(home),
                                               args.report, args.mode, transparent=args.transparent,
                                               qpa=args.qpa)
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
