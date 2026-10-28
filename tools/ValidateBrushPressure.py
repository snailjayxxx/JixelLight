"""Controlled photo-canvas tablet input, with independent real-Sony export proof."""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import time

SONY_BYTES = 69_120_000
SONY_SHA256 = "aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"
CAPTURES = {"before", "cancel-paint-held", "cancel-paint-guarded", "paint-held",
            "painted", "reopened-paint", "cancel-erase-held", "cancel-erase-guarded",
            "erase-held", "erased", "final"}
OWNED_CAPTURES = CAPTURES | {"locale-preview-1", "locale-preview-2", "locale-preview-3",
                             "locale-full-4", "locale-full-5", "locale-full-6"}
REQUIRED = ("settings_isolated", "source_unchanged", "actual_photo_app_UI",
            "pressure_paint_reference", "pressure_erase_reference", "cancel_paint",
            "cancel_erase", "one_undo", "undo_redo", "project_reopen",
            "held_intent_history_revision_unchanged", "primary_lift_only",
            "cancelled_barrel_paint", "cancelled_barrel_erase")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_scope_locale(data: dict, mode: str) -> None:
    source_pixels = data["source_pixels"]
    assert data["scope_locale_checks"] == 6 and data["scope_locale_unchanged"]
    rows = data["scope_locale_records"]
    assert len(rows) == 6 and [(r["full"], r["language"]) for r in rows] == [
        (full, language) for full in (False, True) for language in ("en_US", "zh_CN", "en_US")]
    for row in rows:
        english = row["language"] == "en_US"
        expected = (("Full-resolution statistics" if english else "全分辨率统计") if row["full"] else
                    ("GPU preview statistics" if english else "GPU 预览统计") if mode == "gpu" else
                    ("Preview statistics" if english else "预览统计"))
        assert row["UI"] == row["controller"] == row["expected"] == expected
        assert row["stable"] and row["pixels"] == source_pixels
    assert all(row["revision"] == rows[start]["revision"] for start in (0, 3) for row in rows[start:start + 3])


def validate_brush_radius(data: dict) -> None:
    expected = {"activate-grow": .1, "shrink": .08, "modifiers-ignored": .08,
                "repeat-max": .5, "repeat-min": .002, "text-input-isolated": .08,
                "reentry-focus": .08, "idle-exit-inactive": .08,
                "mouse-held": .08, "next-stroke-radius": .1}
    assert data["radius_probe_done"] is True and data["radius_held_guards"] == 6
    rows = data["radius_key_records"]
    assert len(rows) == 10 and [row["case"] for row in rows] == list(expected)
    for row in rows:
        name, value = row["case"], row["radius"]
        assert row["passed"] is True and math.isfinite(value) and abs(value - expected[name]) < 1e-10
        if name == "reentry-focus":
            assert row["focus"] is True
        else:
            assert row["state_unchanged"] is True
        if name in tuple(expected)[:6] or name == "next-stroke-radius":
            assert math.isfinite(row["slider"]) and abs(row["slider"] - expected[name]) < 1e-10
        if name in tuple(expected)[:6]:
            assert row["focus"] is (name != "text-input-isolated")
    hints = data["radius_hint_records"]
    assert len(hints) == 3 and [row["language"] for row in hints] == ["en_US", "zh_CN", "en_US"]
    for row in hints:
        suffix = ("半径 " if row["language"] == "zh_CN" else "Radius ") + "10.0% · [ / ]"
        assert row["visible"] is True and row["expected_suffix"] == suffix and row["UI"].endswith(suffix)
        assert math.isfinite(row["width"]) and math.isfinite(row["canvas_width"])
        assert 0 < row["width"] <= row["canvas_width"] - 15


def validate_brush_pressure(binary: Path, environment: dict[str, str], home: Path,
                           report: Path, mode: str, raw: Path | None = None,
                           verifier: Path | None = None, *, transparent: bool = False) -> dict:
    if mode not in ("cpu", "gpu") or (transparent and raw is not None):
        raise ValueError("Invalid pressure-validation mode/source")
    binary, report = binary.resolve(), report.resolve()
    if report.exists() or list(report.parent.glob(report.stem + ".*")):
        raise ValueError("Use a fresh report path; preserve previous evidence")
    if raw is not None:
        raw = raw.resolve()
        if raw == report or raw.stat().st_size != SONY_BYTES or sha256(raw) != SONY_SHA256:
            raise ValueError("Exactly the pinned public Sony ARW is required")
        if verifier is None or not verifier.resolve().is_file():
            raise ValueError("Sony pressure validation requires its deployed export verifier")
        verifier = verifier.resolve()
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

    # Full Sony exports never enter the public report/package-artifact tree.
    # Failed outputs stay in this separate directory for local diagnosis.
    work = Path(tempfile.mkdtemp(prefix="jixellight-pressure-像素 验证-🖌-"))
    native_path = work / (report.stem + ".native.json")
    native_log = work / (report.stem + ".native.log")
    reference_path = work / (report.stem + ".reference.json")
    reference_log = work / (report.stem + ".reference.log")
    flag = ("sony-brush-pressure-canvas-smoke-report" if raw else
            "transparent-brush-pressure-canvas-smoke-report" if transparent else
            "brush-pressure-canvas-smoke-report")
    command = [str(binary), "--" + flag, str(native_path)] + ([str(raw)] if raw else [])
    prefix = "sony-" if raw else "transparent-" if transparent else ""
    result = dict(mode=prefix + "brush-pressure-" + mode, passed=False,
                  physical_tablet_verified=False, native_gpu_brush=False,
                  native_float_raw_decoder=False, photographs_published=False,
                  report={}, verification={}, checks={}, private_output_directory=str(work))
    started = time.monotonic()
    try:
        with native_log.open("xb") as stream:
            run = subprocess.run(command, cwd=home, env=env, stdout=stream,
                                 stderr=subprocess.STDOUT, timeout=190 if raw else 65)
        result["returncode"] = run.returncode
        data = json.loads(native_path.read_text(encoding="utf-8"))
        result["report"] = data
        assert run.returncode == 0 and not data["error"]
        assert data["test_fixture"] and data["physical_tablet_verified"] is False
        assert data["product_UI_acceptance"] is False
        assert data["source_commit"] and data["source_commit"] != "unknown"
        assert data["initial_pressure"] == .2 and data["mouse_synthesis_enabled"]
        assert data["injection"] == "Qt-QPA-controlled-pressure-stylus"
        assert data["gpu_active"] is (mode == "gpu")
        if mode == "gpu":
            expected_backend = "Direct3D 11" if sys.platform == "win32" else "Metal" if sys.platform == "darwin" else "OpenGL"
            assert expected_backend in data["backend"]
        assert not data["native_gpu_brush"] and not data["native_float_raw_decoder"]
        assert all(data[key] is True for key in REQUIRED)
        assert data["barrel_transitions_held"] == 8
        assert data["live_paint_delta"] >= 3 and data["live_erase_delta"] >= 3
        assert 0 <= data["live_to_commit_paint_delta"] <= 2
        assert 0 <= data["live_to_commit_erase_delta"] <= 2
        source_pixels = 32_947_264 if raw else 512 * 320
        assert data["source_pixels"] == source_pixels
        if raw:
            assert data["passed"] is False and data["gui_and_export_write_passed"]
            assert data["external_pixel_verification_pending"] and not data["export_exact_pixels_and_ICC"]
            assert data["source_bytes"] == SONY_BYTES and data["source_sha256"] == SONY_SHA256
            assert data["source_kind"] == "public-sony-arw" and data["source_is_raw"]
            assert not data["synthetic_fixture"] and not data["source_override"]
            assert not data["photographs_published"]
            assert data["scope_pixels"] == data["scope_plot_pixels"] == source_pixels
            assert data["full_scopes"] and data["ownership_mode"] and data["budget_admitted"]
            assert data["gui_deadline_ms"] == 60000 and 0 < data["gui_elapsed_ms"] < 60000
            assert data["export_deadline_ms"] == 120000
            assert data["operation_budget_bytes"] == 3 * 1024**3
            assert 0 < data["operation_peak_reserved_bytes"] <= 3 * 1024**3
        else:
            assert data["window_focus_injection"] == "Qt-QPA-controlled-window-focus"
            assert data["window_focus_ready"] is True and data["physical_OS_activation_verified"] is False
            assert type(data["initial_window_active"]) is bool
            assert type(data["focus_bootstrap_count"]) is int and data["focus_bootstrap_count"] >= 0
            assert data["passed"] and data["export_exact_pixels_and_ICC"]
            assert data["deadline_ms"] == 45000 and 0 < data["elapsed_ms"] < 45000
            assert data["source_kind"] == "owned-rgba64-png"
            assert data["source_alpha_u16"] == (42000 if transparent else 65535)
            validate_scope_locale(data, mode)
            validate_brush_radius(data)
        captures = CAPTURES if raw else OWNED_CAPTURES
        assert len(data["captures"]) == len(captures) and {c["label"] for c in data["captures"]} == captures
        copied = {}
        for capture in data["captures"]:
            label = capture["label"]
            suffix = "-controls.png" if raw else "-owned.png"
            path = Path(str(native_path) + "." + label + suffix)
            assert Path(capture["path"]) == path and capture["dpr"] == data["dpr"]
            with path.open("rb") as stream:
                header = stream.read(24)
            assert header[:8] == b"\x89PNG\r\n\x1a\n" and header[12:16] == b"IHDR"
            width, height = struct.unpack(">II", header[16:24])
            expected = ((capture["controls_width"], capture["controls_height"]) if raw else
                        (capture["width"], capture["height"]))
            assert (width, height) == expected and sha256(path) == capture["sha256"]
            if raw:
                assert 0 < width < capture["width"] and 0 < height <= capture["height"]
            destination = report.parent / path.name
            shutil.copy2(path, destination)
            assert sha256(destination) == capture["sha256"]
            copied[label] = str(destination)
        markers = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]",
                   "QQmlApplicationEngine failed", "Binding loop detected")
        errors = [line for line in native_log.read_text(encoding="utf-8", errors="replace").splitlines()
                  if any(marker in line for marker in markers)]
        assert not errors
        result.update(source_commit=data["source_commit"], backend=data["backend"],
                      qml_errors=errors, capture_artifacts=copied,
                      controls_only_captures=bool(raw), captures_verified=True)
        if raw:
            remaining = 120 - (time.monotonic() - started - data["gui_elapsed_ms"] / 1000)
            assert remaining > 0, "Export/reference deadline exhausted"
            output = Path(str(native_path) + ".export.png")
            frozen = Path(str(native_path) + ".frozen.json")
            with reference_path.open("xb") as stdout, reference_log.open("xb") as stderr:
                check = subprocess.run([str(verifier), str(raw), str(frozen), str(output)],
                                       cwd=home, env=env, stdout=stdout, stderr=stderr, timeout=remaining)
            result["verification_returncode"] = check.returncode
            witness = json.loads(reference_path.read_text(encoding="utf-8"))
            result["verification"] = witness
            assert check.returncode == 0 and witness["passed"] and witness["test_fixture"]
            assert witness["source_commit"] == data["source_commit"]
            assert witness["exact_pixels_and_ICC"] and witness["plan_hash_matches_actual_UI_export"]
            assert witness["source_pixels"] == witness["decoded_pixels"] == source_pixels
            assert witness["raw_sha256"] == sha256(raw) == SONY_SHA256
            assert witness["operation_budget_bytes"] == 3 * 1024**3
            assert 0 < witness["operation_peak_reserved_bytes"] <= 3 * 1024**3
            assert witness["operation_end_charged_bytes"] == 0
            assert not witness["physical_tablet_verified"] and not witness["product_UI_acceptance"]
            assert not witness["native_gpu_brush"] and not witness["native_float_raw_decoder"]
            result.update(export_bytes=output.stat().st_size, export_sha256=sha256(output),
                          frozen_input_sha256=sha256(frozen), native_report_preserved_as_partial=True)
            combined = (time.monotonic() - started) * 1000 - data["gui_elapsed_ms"]
            assert combined < 120000, "Combined export/reference deadline exceeded"
            result["combined_export_reference_wall_ms"] = round(combined, 3)
        result["checks"] = dict(native_pointer_and_pressure=True, primary_lift_ownership=True,
                                one_undo_and_reopen=True, held_and_committed_rgb=True,
                                exact_export_pixels_and_ICC=True, captures_verified=True,
                                source_unchanged=True, settings_isolated=True)
        result["passed"] = True
    except (OSError, ValueError, TypeError, IndexError, KeyError, AssertionError, subprocess.TimeoutExpired) as error:
        result["error"] = str(error) or "Pressure validation predicate failed"
    finally:
        # Preserve original report bytes, including a Sony native passed=false.
        for path in (native_path, native_log, reference_path, reference_log):
            if path.is_file():
                shutil.copy2(path, report.parent / path.name)
        frozen = Path(str(native_path) + ".frozen.json")
        if frozen.is_file():
            shutil.copy2(frozen, report.parent / frozen.name)
        result["private_output_preserved"] = not result["passed"]
        if result["passed"]:
            shutil.rmtree(work)
        report.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("binary", type=Path)
    parser.add_argument("report", type=Path)
    parser.add_argument("mode", choices=("cpu", "gpu"))
    parser.add_argument("--sony", type=Path)
    parser.add_argument("--verifier", type=Path)
    parser.add_argument("--transparent", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="jixellight-pressure-home-") as folder:
        result = validate_brush_pressure(args.binary, os.environ.copy(), Path(folder),
                                        args.report, args.mode, args.sony, args.verifier,
                                        transparent=args.transparent)
    print(json.dumps(result, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
