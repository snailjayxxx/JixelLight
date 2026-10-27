"""Actual Qt pointer brush smoke; test-owned synthetic source, separate RAW gate."""
from __future__ import annotations
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def validate_brush(binary: Path, environment: dict[str, str], home: Path,
                   report: Path, mode: str) -> dict:
    env = environment.copy()
    for key in ("HOME", "USERPROFILE", "XDG_CACHE_HOME", "XDG_CONFIG_HOME",
                "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
        env[key] = str(home)
    for key in ("JIXELLIGHT_REQUIRE_GPU", "JIXELLIGHT_FORCE_CPU", "QSG_RHI_BACKEND",
                "QSG_RHI_PREFER_SOFTWARE_RENDERER"):
        env.pop(key, None)
    if mode == "gpu":
        env["JIXELLIGHT_REQUIRE_GPU"] = "1"
        env["QSG_RHI_BACKEND"] = "d3d11" if sys.platform == "win32" else "metal" if sys.platform == "darwin" else "opengl"
        if sys.platform == "win32":
            env["QSG_RHI_PREFER_SOFTWARE_RENDERER"] = "1"
        elif sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "xcb"  # Caller supplies DISPLAY, normally xvfb-run.
            env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    else:
        env["JIXELLIGHT_FORCE_CPU"] = "1"
        if sys.platform.startswith("linux"):
            env["QT_QPA_PLATFORM"] = "offscreen"
    report.parent.mkdir(parents=True, exist_ok=True)
    try:
        run = subprocess.run([str(binary), "--brush-canvas-smoke-report", str(report)],
                             cwd=home, env=env, capture_output=True, text=True,
                             errors="replace", check=False, timeout=65)
        code, log = run.returncode, run.stdout + "\n" + run.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        code, log = -1, str(error)
    report.with_suffix(".log").write_text(log, encoding="utf-8")
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        data = {}
    markers = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]",
               "QQmlApplicationEngine failed", "Binding loop detected")
    errors = [line for line in log.splitlines() if any(m in line for m in markers)]
    required = ("live_uncommitted", "escape_cancel", "one_undo", "display_changed",
                "undo_redo", "project_reopen", "zoom_roi", "full_scopes", "source_unchanged", "settings_visible",
                "layer_selection", "layer_ev_drag_one_undo", "layer_toggle", "remove_last_stroke",
                "empty_layer_pruned_undo", "delete_layer_undo", "english_controls_visible", "scope_display", "photo_display_ready", "brush_pointer_ready", "disabled_layer_guard", "selected_stroke_delete", "brush_controls_reveal", "capacity_guard")
    screenshots = all(Path(str(report) + suffix).is_file()
                      for suffix in (".before.png", ".painted.png", ".reopen.png", ".zoom.png", ".layers.png", ".disabled.png", ".selected-stroke.png", ".reveal.png", ".capacity.png"))
    passed = (code == 0 and data.get("passed") is True and not errors and screenshots
              and all(data.get(key) is True for key in required)
              and data.get("synthetic_fixture") is True and data.get("scope_pixels") == 2659021
              and data.get("gpu_active") is (mode == "gpu") and data.get("elapsed_ms", 45000) < 45000
              and data.get("scope_display_ink_samples", 0) >= 64
              and data.get("selected_guide_new_ink_samples", 0) >= 16 and data.get("selected_delete_rgb_delta", 0) >= 3
              and data.get("manual_scroll_respected") is True
              and data.get("manual_locale_scroll_preserved") is True and data.get("language_reveal_requested") is True
              and data.get("capacity_target_specific") is True and data.get("capacity_recovery") is True
              and len(data.get("capacity_cases", [])) == 7 and all(c.get("input_ignored") is True and c.get("hint_visible") is True for c in data.get("capacity_cases", []))
              and len(data.get("capacity_ready_cases", [])) == 4 and all(c.get("drawing_started") is True and c.get("state_preserved") is True for c in data.get("capacity_ready_cases", []))
              and data.get("native_gpu_brush") is False and data.get("native_float_raw_decoder") is False)
    result = {"mode": "brush-" + mode, "passed": passed, "returncode": code,
              "source_commit": data.get("source_commit"), "qml_errors": errors,
              "screenshots_saved": screenshots, "report": data}
    if not passed:
        print(log[-16000:], file=sys.stderr, flush=True)
    return result


def main() -> int:
    if len(sys.argv) not in (3, 4) or (len(sys.argv) == 4 and sys.argv[3] not in ("cpu", "gpu")):
        raise SystemExit("Usage: ValidateBrushCanvas.py <desktop executable> <report path> [cpu|gpu]")
    binary, report = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    if not binary.is_file():
        raise SystemExit(f"Missing desktop binary: {binary}")
    with tempfile.TemporaryDirectory(prefix="jixel-brush-pointer-") as folder:
        result = validate_brush(binary, os.environ.copy(), Path(folder), report,
                                sys.argv[3] if len(sys.argv) == 4 else "cpu")
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
