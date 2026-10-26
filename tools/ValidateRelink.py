"""Exercise missing-source recovery with test-owned images and native Qt clicks."""
from __future__ import annotations
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def validate_relink(binary: Path, environment: dict[str, str], home: Path,
                    report: Path) -> dict:
    env = environment.copy()
    env.pop("JIXELLIGHT_REQUIRE_GPU", None)
    env.pop("QSG_RHI_BACKEND", None)
    env["JIXELLIGHT_FORCE_CPU"] = "1"
    if sys.platform.startswith("linux"):
        env["QT_QPA_PLATFORM"] = "offscreen"
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
    for key in ("HOME", "USERPROFILE", "XDG_CACHE_HOME", "XDG_CONFIG_HOME",
                "XDG_DATA_HOME", "APPDATA", "LOCALAPPDATA"):
        env[key] = str(home)
    report.parent.mkdir(parents=True, exist_ok=True)
    try:
        run = subprocess.run([str(binary), "--relink-smoke-report", str(report)],
                             cwd=home, env=env, capture_output=True, text=True,
                             errors="replace", check=False, timeout=30)
        code, log = run.returncode, run.stdout + "\n" + run.stderr
    except (OSError, subprocess.TimeoutExpired) as error:
        code, log = -1, str(error)
    report.with_suffix(".log").write_text(log, encoding="utf-8")
    try:
        data = json.loads(report.read_text(encoding="utf-8"))
    except (ValueError, OSError):
        data = {}
    markers = ("TypeError:", "ReferenceError:", "Unable to assign [undefined]",
               "QQmlApplicationEngine failed")
    errors = [line for line in log.splitlines() if any(m in line for m in markers)]
    required = ("missing_placeholder_passed", "cancel_preserves_catalog",
                "unknown_identity_acknowledgement", "captured_family_passed",
                "relink_complete", "reopen_preserves_versions",
                "selected_export_passed", "compact_dialog_passed", "source_unchanged")
    screenshots = all(Path(str(report) + suffix).is_file()
                      for suffix in (".missing.png", ".confirm.png", ".restored.png"))
    passed = (code == 0 and data.get("passed") is True and not errors and screenshots
              and all(data.get(key) is True for key in required)
              and data.get("file_picker_automated") is False)
    result = {"mode": "source-relink", "passed": passed, "returncode": code,
              "source_commit": data.get("source_commit"), "qml_errors": errors,
              "screenshots_saved": screenshots, "report": data}
    if not passed:
        print(log[-16000:], file=sys.stderr, flush=True)
    return result


def main() -> int:
    if len(sys.argv) != 3:
        raise SystemExit("Usage: ValidateRelink.py <desktop executable> <report path>")
    binary, report = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    if not binary.is_file():
        raise SystemExit(f"Missing desktop binary: {binary}")
    with tempfile.TemporaryDirectory(prefix="jixel-relink-") as folder:
        result = validate_relink(binary, os.environ.copy(), Path(folder), report)
    print(json.dumps(result, ensure_ascii=False, indent=2), flush=True)
    return 0 if result["passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
