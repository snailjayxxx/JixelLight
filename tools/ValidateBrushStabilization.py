"""Validate an explicit pen input rule through the actual application canvas."""
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

LABELS = {"selected", "held", "applied", "undo", "redo", "mouse", "raw-refused", "finished"}
REQUIRED = ("settings_isolated", "source_unchanged", "actual_photo_app_UI", "test_fixture",
            "controlled_qpa_tablet", "window_focus_ready", "actual_app_controlled_QPA",
            "default_off", "explicit_checkbox_selected", "checkbox_disabled_while_held",
            "all9000_packets_accepted_when_selected", "single_undo", "canvas_pixel_changed",
            "undo_pixel_and_history_restored", "redo_pixel_and_history_restored",
            "selected_checkbox_keeps_mouse_raw", "off_mode_original_refusal",
            "escape_discards_offer_keeps_brush", "selected_rule_changes_raw_intent")
FALSE_SCOPE = ("physical_OS_activation_verified", "physical_tablet_verified", "native_gpu_brush",
               "native_float_raw_decoder", "production_accepted", "continuous_unique_jitter_fixed",
               "raw_unfiltered_jitter_fixed", "stabilizer_specific_Sony_verified")


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def strict_json(text):
    def pairs(items):
        result = {}
        for key, value in items:
            if key in result:
                raise ValueError("Duplicate JSON key")
            result[key] = value
        return result
    def invalid_constant(value):
        raise ValueError("Non-finite JSON number")
    def finite_float(value):
        parsed = float(value)
        if not math.isfinite(parsed):
            raise ValueError("Non-finite JSON number")
        return parsed
    return json.loads(text, object_pairs_hook=pairs, parse_constant=invalid_constant,
                      parse_float=finite_float)

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
    if not isinstance(captures, list) or len(captures) > len(LABELS) + 1:
        return copied, ["Invalid capture list"]
    for entry in captures:
        try:
            label = entry["label"]
            if label not in LABELS | {"failure"} or label in seen:
                raise ValueError("Invalid or duplicate capture label")
            seen.add(label)
            if (type(entry["width"]) is not int or type(entry["height"]) is not int
                    or min(entry["width"], entry["height"]) <= 0
                    or not number(entry["dpr"]) or entry["dpr"] <= 0):
                raise ValueError("Invalid capture dimensions or DPR")
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


def dabs(value):
    assert type(value) is list and 0 < len(value) <= 4096
    assert all(type(dab) is list and len(dab) == 3
               and all(number(v) for v in dab)
               and all(0 <= v <= 1 for v in dab) for dab in value)


def mouse_reference(start, end):
    # Independent full-pressure branch of the unchanged sampler. This QA path
    # moves less than half a source pixel; it does not use the selected rule.
    width, height, spacing = 512, 320, .002 * .25
    distance = math.hypot((end[0] - start[0]) * width / min(width, height),
                          (end[1] - start[1]) * height / min(width, height))
    assert distance > 0
    result = [[*start, 1]]
    traversed, to_go = 0.0, distance
    while to_go + spacing * 1e-9 >= spacing:
        traversed += spacing
        t = min(1.0, max(0.0, traversed / distance))
        result.append([start[0] + (end[0] - start[0]) * t,
                       start[1] + (end[1] - start[1]) * t, 1])
        to_go = max(0.0, distance - traversed)
    last = result[-1]
    remainder = math.hypot((last[0] - end[0]) * width / min(width, height),
                           (last[1] - end[1]) * height / min(width, height))
    if remainder > spacing * .1:
        result.append([*end, 1])
    return result


def same_arithmetic(actual, reference):
    assert len(actual) == len(reference)
    for a, b in zip(actual, reference):
        assert a[2] == b[2] == 1
        for x, y in zip(a[:2], b[:2]):
            # Only account for libm/interpolation rounding between Python and
            # C++; the native actual-vs-native-oracle dabs must match exactly.
            assert x == y or abs(x-y) <= 8 * max(math.ulp(x), math.ulp(y))


def validate_stabilization_report(data: dict, mode: str, transparent: bool,
                                  erase: bool, qpa: str | None = None) -> None:
    assert mode in ("cpu", "gpu") and data["passed"] is True and data["error"] == ""
    assert all(data[key] is True for key in REQUIRED)
    assert all(data[key] is False for key in FALSE_SCOPE)
    assert data["source_kind"] == "owned-rgba64-png"
    for key, value in (("source_width",512),("source_height",320),("source_pixels",512*320),
                       ("source_alpha_u16",42000 if transparent else 65535),
                       ("single_stroke_dab_limit",4096),("document_dab_limit",16384),
                       ("selected_final_dabs",2)):
        assert type(data[key]) is int and data[key] == value
    assert re.fullmatch(r"[0-9a-f]{64}", data["source_sha256"])
    assert re.fullmatch(r"[0-9a-f]{7,40}", data["source_commit"])
    assert data["injection"] == "Qt-QPA-controlled-pressure-stylus"
    assert data["window_focus_injection"] == "Qt-QPA-controlled-window-focus"
    assert type(data["elapsed_ms"]) is int and 0 < data["elapsed_ms"] < data["deadline_ms"] == 60000
    assert type(data["deadline_ms"]) is int
    assert number(data["dpr"]) and data["dpr"] > 0
    assert data["actual_qpa"] in ("offscreen","cocoa","windows","xcb")
    assert not qpa or data["actual_qpa"] == qpa
    assert data["gpu_active"] is (mode == "gpu")
    if mode == "gpu":
        backend = "Direct3D 11" if sys.platform == "win32" else "Metal" if sys.platform == "darwin" else "OpenGL"
        assert backend in data["backend"]
    else:
        assert data["backend"].startswith("CPU")
    assert data["mode"] == ("erase" if erase else "paint")
    assert data["language"] == ("zh_CN" if mode == "gpu" else "en_US")
    assert number(data["position_deadband_source_pixels"]) and data["position_deadband_source_pixels"] == .5
    assert data["selected_input_rule"] == "half-image-pixel-anchor"
    style = data["selected_style"]
    assert type(style) is dict and all(number(v) for v in style.values())
    assert style == {"radius":.04,"hardness":1,"opacity":.7}
    selected = data["selected_dabs"]; dabs(selected)
    assert len(selected) == 2 and selected[0][:2] == selected[1][:2]
    assert selected[0][2] == .501 and selected[1][2] == 0
    assert type(data["off_mode_first_refused_packet"]) is int and 8000 <= data["off_mode_first_refused_packet"] <= 9000
    start, end = data["mouse_start_source"], data["mouse_end_source"]
    assert all(type(p) is list and len(p) == 2 and all(number(v) and 0 <= v <= 1 for v in p) for p in (start,end))
    movement = math.hypot((end[0]-start[0])*512, (end[1]-start[1])*320)
    observed = data["mouse_move_source_pixels"]
    assert number(observed) and 0 < observed <= .5 and 0 < movement <= .5
    assert abs(movement-observed) <= 8 * max(math.ulp(movement), math.ulp(observed))
    actual, expected = data["mouse_actual_dabs"], data["mouse_expected_dabs"]
    dabs(actual); dabs(expected)
    assert len(actual) > 1 and actual == expected
    same_arithmetic(expected, mouse_reference(start, end))
    captures = data["captures"]
    assert type(captures) is list and len(captures) == len(LABELS)
    assert {row["label"] for row in captures} == LABELS
    for row in captures:
        assert type(row["width"]) is int and type(row["height"]) is int
        assert row["width"] == 1180 * data["dpr"] and row["height"] == 720 * data["dpr"]
        assert number(row["dpr"]) and row["dpr"] == data["dpr"]
        assert type(row["path"]) is str and row["path"]
        assert re.fullmatch(r"[0-9a-f]{64}", row["sha256"])


def validate_brush_stabilization(binary: Path, environment: dict[str, str], home: Path,
                                  report: Path, mode: str, *, transparent: bool = False,
                                  qpa: str | None = None, erase: bool = False, one_pixel: bool = False) -> dict:
    if type(one_pixel) is not bool or mode not in ("cpu", "gpu") or qpa not in (None, "offscreen", "cocoa", "windows", "xcb"):
        raise ValueError("Invalid brush-stabilization mode or QPA")
    if report.is_symlink():
        raise ValueError("Existing report link is preserved")
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
    work = Path(tempfile.mkdtemp(prefix="jixellight-brush-stabilization-"))
    native = work / (report.stem + ".native.json")
    log = work / (report.stem + ".native.log")
    flag = (("transparent-one-pixel-brush-stabilization-smoke-report" if transparent else "one-pixel-brush-stabilization-smoke-report") if one_pixel else
            ("transparent-brush-stabilization-smoke-report" if transparent else "brush-stabilization-smoke-report"))
    prefix = "transparent-" if transparent else ""
    result = {"mode": prefix + ("one-pixel-brush-stabilization-" if one_pixel else "brush-stabilization-") + ("erase-" if erase else "paint-") + mode, "passed": False,
              "physical_tablet_verified": False, "native_gpu_brush": False,
              "native_float_raw_decoder": False, "production_accepted": False,
              "continuous_unique_jitter_fixed": False, "raw_unfiltered_jitter_fixed": False,
              "stabilizer_specific_Sony_verified": False, "selected_rule_changes_raw_intent": True,
              "private_output_directory": str(work), "report": {}}
    try:
        with log.open("xb") as stream:
            command = [str(binary), "--" + flag, str(native)]
            if erase:
                command.append("--brush-stabilization-erase")
            run = subprocess.run(command, cwd=home, env=env,
                                 stdout=stream, stderr=subprocess.STDOUT, timeout=75)
        result["returncode"] = run.returncode
        data = strict_json(native.read_text(encoding="utf-8"))
        result["report"] = data
        assert run.returncode == 0
        if one_pixel:
            from ValidateOnePixelBrush import validate_one_pixel_report
            validate_one_pixel_report(data, mode, transparent, erase, qpa)
        else:
            validate_stabilization_report(data, mode, transparent, erase, qpa)
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
    parser.add_argument("--erase", action="store_true")
    parser.add_argument("--qpa", choices=("offscreen", "cocoa", "windows", "xcb"))
    parser.add_argument("--one-pixel", action="store_true", help="Require explicitly selected1px input; default keeps half-pixel validation")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="jixellight-brush-stabilization-home-") as home:
        result = validate_brush_stabilization(args.executable, os.environ.copy(), Path(home),
                                               args.report, args.mode, transparent=args.transparent,
                                               qpa=args.qpa, erase=args.erase, one_pixel=args.one_pixel)
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    sys.exit(main())
