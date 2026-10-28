"""Actual Qt pointer brush smoke; test-owned synthetic source, separate RAW gate."""
from __future__ import annotations
import json
import math
import os
from pathlib import Path
import subprocess
import struct
import sys
import tempfile


def _pan_checks(data: dict, report: Path) -> dict[str, bool]:
    checks = dict.fromkeys(("protocol", "guards", "basic_mapping", "view_mapping",
                           "pointer_ownership", "hint_policy", "bilingual_hints",
                           "focus_transition", "captures"), False)
    def number(value):
        assert type(value) in (int, float) and math.isfinite(value)
        return value
    def point(value):
        assert type(value) is list and len(value) == 2
        return [number(v) for v in value]
    def same(actual, expected):
        assert all(abs(a-b) < 1e-9 for a, b in zip(point(actual), point(expected)))
    try:
        assert type(data["pan_protocol_version"]) is int and data["pan_protocol_version"] == 1
        assert all(data[key] is True for key in ("pan_checked", "pan_readonly", "pan_mapping", "pan_guards_checked", "pan_geometry_checked"))
        assert data["pan_started_brush"] is False and data["physical_OS_activation_verified"] is False
        assert data["window_focus_injection"] == "Qt-QPA-controlled-window-focus"
        checks["protocol"] = True
        guards = data["pan_guard_records"]
        expected = {"modifiers-ignored", "orphan-repeat-ignored", "repeat-release-retains-navigation",
                    "radius-held-during-navigation", "escape-keeps-brush-mode", "textfield-owns-space-focus-loss",
                    "mouse-held-and-cancelled-until-lift", "pen-held-and-cancelled-primary-lift",
                    "pen-during-pan-ignored-until-lift-fresh-press", "window-deactivation-cancels-held-pan",
                    "mode-exit-cancels-space", "crop-entry-cancels-space", "fit-ignores-space",
                    "source-switch-cancels-held-pan-original-preserved"}
        assert len(guards) == 14 and {r["case"] for r in guards} == expected
        assert all(r["passed"] is True and r["document_history_unchanged"] is True for r in guards)
        checks["guards"] = True
        basic = data["pan_record"]; dpr = number(basic["dpr"]); assert dpr > 0
        assert (basic["source_width"], basic["source_height"], basic["zoom"]) == (2203, 1207, 2)
        assert basic["space_released_before_mouse_lift"] is True and basic["document_history_unchanged"] is True and basic["view_mapping_exact"] is True
        same(basic["actual_center"], [.5-48*dpr/(2203*2), .5+24*dpr/(1207*2)])
        checks["basic_mapping"] = True
        views = data["pan_view_records"]
        assert len(views) == 3 and [r["case"] for r in views] == ["100-percent", "rotated-cropped", "plain-rotated-cropped"]
        assert [r["output_size"] for r in views] == [[2203,1207],[846,1653],[846,1653]]
        assert [r["zoom"] for r in views] == [1,2,2]
        for row, delta in zip(views, ((12,-8),(26,-16),(18,-10))):
            assert row["document_history_unchanged"] is True and row["mapping_exact"] is True and number(row["dpr"]) == dpr
            start = point(row["center_before"]); frame = row["frame_before"]
            assert type(frame) is list and len(frame) == 4 and all(0 <= number(v) <= 1 for v in frame) and frame[2] > 0 and frame[3] > 0
            assert frame[0]+frame[2] <= 1+1e-9 and frame[1]+frame[3] <= 1+1e-9
            size = row["output_size"]; zoom = number(row["zoom"])
            expected_center = [start[i] - delta[i]*dpr/(size[i]*zoom) if frame[i+2] < 1 else start[i] for i in (0,1)]
            same(row["actual_center"], expected_center); same(row["expected_center"], expected_center)
        checks["view_mapping"] = True
        assert all(r["pointer_owned_closed_hand"] is True for r in views)
        checks["pointer_ownership"] = True
        assert [r["mask_tool"] for r in views] == ["brush", "brush", ""]
        assert [r["hint_visible_bounded"] for r in views] == [True, True, False]
        assert all(r["hint_policy_correct"] is True for r in views)
        checks["hint_policy"] = True
        hints = data["pan_hint_records"]
        assert len(hints) == 2 and [r["language"] for r in hints] == ["en_US", "zh_CN"]
        for row, prefix in zip(hints, ("Space + drag to pan", "空格＋拖动平移")):
            assert row["visible_bounded"] is True and isinstance(row["text"], str) and row["text"].startswith(prefix)
            assert 0 < number(row["width"]) <= number(row["canvas_width"]) - 15
        checks["bilingual_hints"] = True
        focus = data["pan_window_check"]
        assert all(focus[k] is True for k in ("active_before", "active_after_reactivation", "stable"))
        assert focus["active_after_deactivation"] is False and focus["navigation_after_deactivation"] is False
        checks["focus_transition"] = True
        logical = point(data["logical_window_size"]); assert logical[0] == 1180 and 720 <= logical[1] <= 800
        physical = tuple(int(math.floor(v*dpr+.5)) for v in logical)
        minimum_height = int(math.floor(720*dpr+.5)); capture_size = None
        suffixes = (".pan.png", ".pan-100-percent.png", ".pan-rotated-cropped.png", ".pan-plain-rotated-cropped.png", ".pan-hint-en_US.png", ".pan-hint-zh_CN.png")
        for suffix in suffixes:
            with Path(str(report)+suffix).open("rb") as stream: header = stream.read(24)
            assert len(header) == 24 and header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR"
            size = struct.unpack(">II", header[16:24])
            # Native raster capture can be shorter than nominal QWindow
            # geometry. Keep original pixels and verify DPR/width, product
            # minimum height and consistent bounds across all six captures.
            assert size[0] == physical[0] and minimum_height <= size[1] <= physical[1]
            if capture_size is None: capture_size = size
            assert size == capture_size
        checks["captures"] = True
    except (AssertionError, KeyError, TypeError, ValueError, OverflowError, OSError, struct.error):
        pass
    return checks


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
    pan_checks = _pan_checks(data, report)
    passed = (code == 0 and data.get("passed") is True and not errors and screenshots and all(pan_checks.values())
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
              "screenshots_saved": screenshots, "pan_checks": pan_checks, "report": data}
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
