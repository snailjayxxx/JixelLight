"""Native QML pointer-event smoke with a locally generated synthetic image.

This never uses private photos or the strict 60s real-RAW GUI smoke budget.
"""
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def main():
    if len(sys.argv) != 3:
        raise SystemExit("Usage: ValidateMaskCanvas.py <desktop executable> <report path>")
    binary = Path(sys.argv[1]).resolve()
    report = Path(sys.argv[2]).resolve()
    if not binary.is_file():
        raise SystemExit(f"Missing desktop binary: {binary}")
    env = os.environ.copy()
    env.pop("JIXELLIGHT_REQUIRE_GPU", None)
    env.pop("QSG_RHI_BACKEND", None)
    env["JIXELLIGHT_FORCE_CPU"] = "1"
    if sys.platform.startswith("linux"):
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    with tempfile.TemporaryDirectory(prefix="jixel-mask-gesture-") as folder:
        for key in ("HOME", "USERPROFILE", "XDG_CACHE_HOME", "XDG_CONFIG_HOME",
                    "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
            env[key] = folder
        try:
            process = subprocess.run(
                [str(binary), "--mask-canvas-smoke-report", str(report)],
                env=env, cwd=folder, text=True, capture_output=True,
                errors="replace", check=False, timeout=45)
            code = process.returncode
            log = process.stdout + "\n" + process.stderr
        except subprocess.TimeoutExpired as error:
            code = -1
            log = str(error)
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        data = {}
    markers = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]",
               "QQmlApplicationEngine failed")
    qml_errors = [row for row in log.splitlines() if any(marker in row for marker in markers)]
    conditions = ("radial_drag_passed", "linear_drag_passed",
                  "radial_feather_drag_passed", "linear_feather_drag_passed",
                  "undo_redo_passed", "zoom_roi_passed",
                  "source_unchanged", "qml_overlay_found")
    passed = (code == 0 and data.get("passed") is True
              and all(data.get(key) is True for key in conditions)
              and 0 < data.get("zoom_frame_width", 1) < .98
              and not qml_errors)
    print(json.dumps({"passed": passed, "returncode": code, "qml_errors": qml_errors,
                      "report": data}, ensure_ascii=False, indent=2), flush=True)
    if not passed:
        print(log[-16000:], file=sys.stderr, flush=True)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
