"""Bounded native Paint/Erase workflow; controls-only artifacts, pinned Sony input."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import struct
import subprocess
import sys
import tempfile

SONY_BYTES = 69_120_000
SONY_SHA256 = "aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"
CAPTURES = ("before", "live-painted", "painted", "live-erased", "erased", "undo", "selected", "deleted", "reopen", "locale-selected", "final", "english")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_brush_erase(binary: Path, environment: dict[str, str], home: Path,
                         report: Path, mode: str, raw: Path | None = None) -> dict:
    if mode not in ("cpu", "gpu"):
        raise ValueError("Unknown rendering mode")
    binary, report = binary.resolve(), report.resolve()
    if raw is not None:
        raw = raw.resolve()
        if raw == report or (raw.exists() and report.exists() and os.path.samefile(raw, report)):
            raise ValueError("Report aliases the original source")
        if (raw.suffix.lower() != ".arw" or raw.stat().st_size != SONY_BYTES
                or sha256(raw) != SONY_SHA256):
            raise ValueError("Pinned public Sony ARW missing/different; no substitute allowed")
    # Never overwrite evidence from an earlier attempt, including failed attempts.
    if report.exists() or report.with_suffix(".log").exists() or list(report.parent.glob(report.name + ".*")):
        raise ValueError("Use a fresh report path; existing evidence is preserved")
    env = environment.copy()
    home.mkdir(parents=True, exist_ok=True)
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
            env["QT_QPA_PLATFORM"] = "xcb"
            env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    else:
        env["JIXELLIGHT_FORCE_CPU"] = "1"
        if sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "offscreen"
    report.parent.mkdir(parents=True, exist_ok=True)
    command = [str(binary), "--sony-brush-erase-canvas-smoke-report" if raw else
               "--brush-erase-canvas-smoke-report", str(report)]
    if raw is not None:
        command.append(str(raw))
    deadline = 60_000 if raw else 45_000
    try:
        run = subprocess.run(command, cwd=home, env=env, capture_output=True, text=True,
                             errors="replace", check=False, timeout=80 if raw else 65)
        code, log = run.returncode, run.stdout + "\n" + run.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        code, log = -1, str(error)
    report.with_suffix(".log").write_text(log, encoding="utf-8")
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        data = {}
    if not isinstance(data, dict):
        data = {}
    markers = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]",
               "QQmlApplicationEngine failed", "Binding loop detected")
    errors = [line for line in log.splitlines() if any(marker in line for marker in markers)]
    required = ("erase_no_layer_refusal", "native_paint", "native_erase_mode", "live_uncommitted",
                "escape_cancel", "one_undo", "display_changed", "undo_redo", "selected_erase_delete",
                "project_reopen", "selected_locale_preserved", "loaded_layer_cancel", "scope_controls", "full_scopes",
                "controls_visible", "english_controls_visible", "source_unchanged", "settings_isolated", "budget_admitted",
                "live_color_paint", "live_color_erase", "live_cancel_paint", "live_cancel_erase",
                "live_pointer_held", "live_committed_revision_unchanged", "live_display_current")
    width, height = (7028, 4688) if raw else (2203, 1207)
    pixels = width * height
    captures = data.get("captures", {})
    valid_captures = isinstance(captures, dict) and set(captures) == set(CAPTURES)
    for label in CAPTURES:
        capture = captures.get(label, {}) if isinstance(captures, dict) else {}
        path = Path(str(report) + "." + label + "-controls.png")
        try:
            with path.open("rb") as stream:
                header = stream.read(24)
            png_size = struct.unpack(">II", header[16:24])
            valid_captures = (valid_captures and isinstance(capture, dict)
                              and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR"
                              and png_size == (capture.get("controls_width"), capture.get("controls_height"))
                              and 0 < png_size[0] < capture.get("width", 0)
                              and 0 < png_size[1] <= capture.get("height", 0)
                              and capture.get("device_scale", 0) > 0
                              and re.fullmatch(r"[0-9a-f]{64}", str(capture.get("sha256", ""))) is not None
                              and sha256(path) == capture.get("controls_sha256")
                              and not Path(str(report) + "." + label + ".png").exists())
        except (OSError, ValueError, TypeError, struct.error):
            valid_captures = False
    geometry = data.get("compact_scope_reflow_geometry")
    valid_geometry = geometry == {} if raw else isinstance(geometry, dict) and set(geometry) == {"before", "after"}
    if raw is None and valid_geometry:
        for phase, expected_mode in (("before", "histogram"), ("after", "waveform")):
            record = geometry.get(phase, {})
            valid_geometry = (valid_geometry and isinstance(record, dict) and record.get("mode") == expected_mode
                              and record.get("automatic_owner") is True and record.get("pending") is False
                              and record.get("document_unchanged") is True and record.get("history_unchanged") is True
                              and record.get("viewport_height", 0) > 0 and record.get("exposure_top", -2) >= -1
                              and 0 < record.get("exposure_bottom", 0) <= record.get("viewport_height", 0) + 1)
    budget = 3 * 1024 ** 3
    checks = {
        "process": code == 0,
        "native_report": data.get("passed") is True and data.get("error") == "",
        "source_commit": re.fullmatch(r"[0-9a-f]{7,40}", str(data.get("source_commit", ""))) is not None,
        "qml": not errors,
        "native_workflow": all(data.get(key) is True for key in required),
        "compact_scope_geometry": bool(valid_geometry),
        "scope_layout_ownership": all(data.get(key) is (raw is None) for key in
                                      ("compact_scope_reflow_checked", "scope_layout_checked", "automatic_scope_reveal",
                                       "manual_scope_scroll_preserved", "scope_reentry_revealed")),
        "original_deadline": data.get("deadline_ms") == deadline and 0 < data.get("elapsed_ms", deadline) < deadline,
        "render_mode": data.get("gpu_active") is (mode == "gpu") and (mode == "cpu" or
                       ("Direct3D 11" if sys.platform == "win32" else "Metal" if sys.platform == "darwin"
                        else "OpenGL Compute") in data.get("backend", "")),
        "full_source": data.get("source_width") == width and data.get("source_height") == height
                       and data.get("source_pixels") == pixels and data.get("expected_source_pixels") == pixels,
        "source_identity": data.get("source_kind") == ("public-sony-arw" if raw else "owned-rgba64-png")
                           and data.get("synthetic_fixture") is (raw is None)
                           and data.get("source_override") is False and data.get("source_is_raw") is (raw is not None)
                           and re.fullmatch(r"[0-9a-f]{64}", str(data.get("source_sha256", ""))) is not None,
        "pinned_sony": raw is None or (data.get("source_bytes") == SONY_BYTES
                                      and data.get("source_sha256") == SONY_SHA256 and sha256(raw) == SONY_SHA256),
        "visible_pixels": data.get("paint_rgb_delta", 0) >= 3 and data.get("erase_rgb_delta", 0) >= 3
                          and 0 <= data.get("undo_rgb_delta", 999) <= 2
                          and 0 <= data.get("redo_rgb_delta", 999) <= 2,
        "live_visible_pixels": data.get("live_paint_rgb_delta", 0) >= 3 and data.get("live_erase_rgb_delta", 0) >= 3
                               and 0 <= data.get("live_cancel_paint_rgb_delta", 999) <= 2
                               and 0 <= data.get("live_cancel_erase_rgb_delta", 999) <= 2
                               and isinstance(data.get("live_input_moves"), int) and data["live_input_moves"] >= 4,
        "full_scopes": data.get("scope_pixels") == pixels and data.get("scope_plot_pixels") == pixels
                       and data.get("scope_display_ink_samples", 0) >= 64,
        "shared_budget": data.get("operation_budget_bytes") == budget
                         and 0 < data.get("operation_peak_reserved_bytes", 0) <= budget
                         and 0 <= data.get("operation_live_charged_bytes", budget + 1) <= budget,
        "controls_only_captures": bool(valid_captures),
        "producer_contract": data.get("producer") == "cpu-reference" and data.get("native_gpu_brush") is False
                             and data.get("native_float_raw_decoder") is False and data.get("photographs_published") is False,
    }
    result = {"mode": ("sony-" if raw else "") + "brush-erase-" + mode,
              "passed": all(checks.values()), "returncode": code, "checks": checks,
              "source_commit": data.get("source_commit"), "qml_errors": errors,
              "controls_captures_verified": bool(valid_captures), "report": data}
    if not result["passed"]:
        print(log[-16000:], file=sys.stderr, flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("mode", nargs="?", choices=("cpu", "gpu"), default="cpu")
    parser.add_argument("--sony", type=Path)
    arguments = parser.parse_args()
    if not arguments.binary.is_file():
        parser.error("Missing desktop executable")
    with tempfile.TemporaryDirectory(prefix="jixel-brush-erase-") as folder:
        result = validate_brush_erase(arguments.binary, os.environ.copy(), Path(folder),
                                      arguments.report, arguments.mode, arguments.sony)
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
