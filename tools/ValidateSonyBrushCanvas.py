"""Real public Sony native pointer GUI, with private photograph captures.

Published evidence contains numerical probes/hashes and controls-only crops.
The review photograph is never copied into a package or a public artifact.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile

SONY_SHA256 = "aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"
SOURCE_PIXELS = 32947264


def digest(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def validate_sony_brush(binary: Path, raw: Path, environment: dict[str, str],
                        home: Path, report: Path, mode: str,
                        private_directory: Path | None = None) -> dict:
    report.parent.mkdir(parents=True, exist_ok=True)
    # Separate persistent private captures from the disposable settings home.
    private = (private_directory or Path(tempfile.mkdtemp(prefix="jixel-sony-brush-private-"))).resolve()
    private.mkdir(parents=True, exist_ok=True)
    private_report = private / "native.json"
    if raw.resolve() == report.resolve() or raw.resolve() == private_report:
        raise ValueError("The report must not replace the original")
    if private.is_relative_to(report.parent.resolve()):
        raise ValueError("Photograph captures must have a separate private directory")
    if not raw.is_file() or raw.stat().st_size != 69120000 or digest(raw) != SONY_SHA256:
        result = {"mode": "sony-brush-" + mode, "passed": False,
                  "error": "Required pinned public Sony07 ARW is absent or its SHA256 differs"}
        report.write_text(json.dumps(result, indent=2), encoding="utf-8")
        return result
    env = environment.copy()
    for key in ("HOME", "USERPROFILE", "XDG_CACHE_HOME", "XDG_CONFIG_HOME",
                "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
        env[key] = str(home)
    for key in ("JIXELLIGHT_FORCE_CPU", "JIXELLIGHT_REQUIRE_GPU", "QSG_RHI_BACKEND",
                "QSG_RHI_PREFER_SOFTWARE_RENDERER"):
        env.pop(key, None)
    if mode == "gpu":
        env["JIXELLIGHT_REQUIRE_GPU"] = "1"
        env["QSG_RHI_BACKEND"] = "d3d11" if sys.platform == "win32" else "metal" if sys.platform == "darwin" else "opengl"
        if sys.platform == "win32":
            env["QSG_RHI_PREFER_SOFTWARE_RENDERER"] = "1"
        elif sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "xcb"
            env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    else:
        env["JIXELLIGHT_FORCE_CPU"] = "1"
        if sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "offscreen"
    try:
        run = subprocess.run([str(binary), "--sony-brush-canvas-smoke-report", str(private_report), str(raw)],
                             cwd=home, env=env, capture_output=True, text=True,
                             errors="replace", check=False, timeout=80)
        code, log = run.returncode, run.stdout + "\n" + run.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        code, log = -1, str(error)
    report.with_suffix(".log").write_text(log, encoding="utf-8")
    try:
        data = json.loads(private_report.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        data = {}
    # Preserve reports even on failure. Copy only the controls panel; full
    # window PNGs containing the photograph remain outside artifact folders.
    captures = data.get("captures", {})
    capture_checks = {}
    for label, record in captures.items():
        if label not in ("before", "painted", "undo", "reopen", "final", "selected-stroke", "failure"):
            continue
        photo = Path(str(private_report) + "." + label + ".png")
        controls = Path(str(private_report) + "." + label + "-controls.png")
        good = (photo.is_file() and controls.is_file()
                and digest(photo) == record.get("sha256")
                and digest(controls) == record.get("controls_sha256"))
        capture_checks[label] = good
        if controls.is_file():
            shutil.copyfile(controls, Path(str(report) + "." + label + "-controls.png"))
    required = ("settings_isolated", "source_is_raw", "source_unchanged", "live_uncommitted", "escape_cancel", "one_undo",
                "display_changed", "undo_redo", "project_reopen", "loaded_layer_cancel", "scope_controls",
                "full_scopes", "brush_pointer_ready", "budget_admitted", "selected_stroke_delete")
    markers = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]",
               "QQmlApplicationEngine failed", "Binding loop detected")
    qml_errors = [line for line in log.splitlines() if any(m in line for m in markers)]
    expected_backend = "Direct3D 11" if sys.platform == "win32" else "Metal" if sys.platform == "darwin" else "OpenGL Compute"
    passed = (code == 0 and data.get("passed") is True and not qml_errors
              and all(data.get(key) is True for key in required)
              and all(capture_checks.get(label) is True for label in ("before", "painted", "undo", "reopen", "final", "selected-stroke"))
              and data.get("synthetic_fixture") is False and data.get("source_override") is False
              and data.get("source_sha256") == SONY_SHA256 and digest(raw) == SONY_SHA256
              and data.get("source_bytes") == 69120000
              and (data.get("source_width"), data.get("source_height")) == (7028, 4688)
              and data.get("source_pixels") == data.get("scope_pixels") == data.get("scope_plot_pixels") == SOURCE_PIXELS
              and data.get("deadline_ms") == 60000 and data.get("elapsed_ms", 60000) < 60000
              and data.get("display_rgb_delta", 0) >= 3
              and data.get("undo_rgb_delta", 999) <= 2 and data.get("redo_rgb_delta", 999) <= 2
              and data.get("scope_display_ink_samples", 0) >= 64
              and data.get("selected_guide_new_ink_samples", 0) >= 16
              and data.get("operation_budget_bytes") == 3 * 1024 ** 3
              and 0 < data.get("operation_peak_reserved_bytes", 0) <= 3 * 1024 ** 3
              and data.get("gpu_active") is (mode == "gpu")
              and (mode == "cpu" or expected_backend in data.get("backend", ""))
              and data.get("native_gpu_brush") is False and data.get("native_float_raw_decoder") is False)
    report.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")
    result = {"mode": "sony-brush-" + mode, "passed": passed, "returncode": code,
              "source_commit": data.get("source_commit"), "qml_errors": qml_errors,
              "controls_only_captures": capture_checks, "photographs_published": False,
              "private_evidence_directory": str(private), "report": data}
    if not passed:
        print(log[-12000:], file=sys.stderr, flush=True)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("raw", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("mode", choices=("cpu", "gpu"))
    parser.add_argument("--private-directory", type=Path)
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="jixel-real-brush-home-") as folder:
        result = validate_sony_brush(args.binary.resolve(), args.raw.resolve(), os.environ.copy(),
                                     Path(folder), args.report.resolve(), args.mode, args.private_directory)
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
