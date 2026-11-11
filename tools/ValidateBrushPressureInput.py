"""Check bounded Qt input ownership; photo UI and hardware have separate gates."""

import argparse
import json
import math
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time


def expected_routes():
    def event(kind, x, y, pressure, active):
        return dict(kind=kind, x=x, y=y, pressure=pressure, active=active)

    def pen(kind, x, pressure, active=True):
        return event("tablet-" + kind, x, 70, pressure, active)

    cancel = event("tablet-cancel", 0, 0, 0, True)
    ordinary = [pen("press", 10, .2), pen("move", 60, .5),
                pen("move", 110, .8), pen("release", 110, 0, False)]
    fresh = [pen("press", 30, .3), pen("move", 100, .7),
             pen("release", 100, 0, False)]
    routes = {
        "tablet": ordinary,
        "mouse": [event("mouse-press", 50, 100, 1, True),
                  event("mouse-move", 100, 100, 1, True),
                  event("mouse-release", 100, 100, 1, False)],
        "clipped": [event("mouse-press", 250, 100, 1, True),
                    event("mouse-move", 255, 100, 1, True),
                    event("mouse-release", 255, 100, 1, False)],
        "disable": [pen("press", 10, .2), cancel],
        "deactivate": [pen("press", 10, .2), cancel],
        "mouse-owner": [event("mouse-press", 70, 100, 1, True),
                        event("mouse-release", 70, 100, 1, False)],
        # Qt's central button edge changes after the foreign device releases.
        # The owner's subsequent press edge remains consumed by its capture.
        "foreign": [pen("press", 10, .2), pen("move", 110, .8),
                    pen("release", 110, 0, False)],
        "no-pressure": [pen("press", 10, 1), pen("move", 60, 1),
                        pen("move", 110, 1), pen("release", 110, 1, False)],
        "mouse-during-pen": ordinary,
        "invalid": [pen("press", 10, .2), cancel],
        "recover": [pen("press", 10, .2), cancel, pen("press", 20, .3),
                    pen("move", 90, .7), pen("release", 90, 0, False)],
        "owner-destroy": [pen("press", 10, .2),
                          event("tablet-cancel", 0, 0, 0, False),
                          event("mouse-press", 100, 100, 1, True),
                          event("mouse-release", 100, 100, 1, False)],
        "outside-window": [pen("press", 10, .2),
                           event("tablet-move", 460, 270, .7, True),
                           event("tablet-release", 460, 270, 0, False)],
        "initial-zero": [pen("press", 10, 0), pen("move", 60, .5),
                         pen("release", 60, 0, False)],
        "barrel-only": [],
        "active-duplicate": [pen("press", 10, .2), pen("move", 70, .8),
                             pen("release", 70, 0, False)] + fresh,
        "barrel-held": [pen("press", 10, .2), pen("move", 110, .8),
                        pen("release", 110, 0, False)] + fresh,
    }
    for name in ("cancel-duplicate", "disable-duplicate",
                 "deactivate-duplicate", "cancel-barrel"):
        routes[name] = [pen("press", 10, .2), cancel] + fresh
    assert len(routes) == 21
    return routes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--fixture", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--result-file", type=Path, required=True)
    parser.add_argument("--expected-source-commit")
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    output = Path(tempfile.mkdtemp(prefix="run-", dir=args.output_dir))
    # This stable CTest pointer must never retain an older passing run on failure.
    args.result_file.write_text("BRUSH_PRESSURE_INPUT_PENDING " + json.dumps(
        dict(passed=False, output_directory=str(output))) + "\n", encoding="utf-8")
    started = time.monotonic()

    def invoke(command, environment, stdout=None):
        remaining = 120 - (time.monotonic() - started)
        if remaining <= 0:
            raise RuntimeError("Input verification exceeded its120s slice")
        result = subprocess.run(command, env=environment, stdout=stdout or subprocess.PIPE,
                                stderr=subprocess.PIPE, timeout=min(10, remaining))
        if result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
        return result

    env = os.environ.copy()
    # Tablet-to-mouse fallback belongs to the actual platform plugin. Offscreen
    # cannot stand in for Cocoa/Windows/XCB even when other core tests use it.
    env["QT_QPA_PLATFORM"] = ("cocoa" if sys.platform == "darwin" else
                              "windows" if sys.platform == "win32" else "xcb")
    env["QT_SCALE_FACTOR"] = "1"
    base = json.loads(invoke([str(args.probe), "--dpi-probe"], env).stdout)["dpr"]
    if not isinstance(base, (float, int)) or not math.isfinite(base) or base <= 0:
        raise RuntimeError("Invalid measured base DPR")
    rows = []
    for scale in (1, 2):
        env["QT_SCALE_FACTOR"] = format(scale / base, ".17g")
        for name, expected in expected_routes().items():
            report = output / f"{name}-{scale}x.json"
            result = invoke([str(args.probe), str(args.fixture), str(report), name], env)
            (output / f"{name}-{scale}x.log").write_bytes(result.stderr)
            observed = json.loads(report.read_text(encoding="utf-8"))
            assert observed["test_fixture"] and not observed["native_ui_acceptance"]
            assert observed["physical_tablet_verified"] is False
            assert observed["product_UI_acceptance"] is False
            assert observed["mouse_synthesis_enabled"]
            assert observed["Qt_platform"] == env["QT_QPA_PLATFORM"]
            assert observed["router_pressed_after"] is False
            assert abs(observed["actual_dpr"] - scale) < 1e-6
            assert observed["scenario"] == name
            if args.expected_source_commit:
                assert observed["source_commit"] == args.expected_source_commit
            records = observed["records"]
            assert len(records) == len(expected), (name, records, expected)
            for actual, reference in zip(records, expected):
                assert set(actual) == set(reference)
                assert actual["kind"] == reference["kind"]
                assert actual["active"] is reference["active"]
                for field in ("x", "y", "pressure"):
                    assert math.isfinite(actual[field])
                    assert abs(actual[field] - reference[field]) < 1e-6, (name, field, actual, reference)
            log = result.stderr.decode("utf-8", errors="replace")
            assert not any(marker in log for marker in
                           ("ReferenceError:", "TypeError:", "Binding loop detected",
                            "Cannot assign", "is not a type", "QQmlApplicationEngine failed to load"))
            rows.append(dict(case=name, dpr=observed["actual_dpr"],
                             source_commit=observed["source_commit"], records=records,
                             report=str(report), Qt_runtime=observed["Qt_runtime"],
                             Qt_platform=observed["Qt_platform"]))
    assert len(rows) == 42
    summary = dict(passed=True, source_commit=rows[0]["source_commit"], rows=42,
                   native_ui_acceptance=False, physical_tablet_verified=False,
                   product_UI_acceptance=False, logical_coordinates_exact=True,
                   primary_lift_only=True, initial_zero_retained=True,
                   pressureless_fallback_one=True, legacy_mouse_preserved=True,
                   foreign_owner_ignored=True, cancelled_owner_retained=True,
                   side_button_release_does_not_finish=True,
                   device_destruction_cancels=True, cases=rows,
                   elapsed_ms=round((time.monotonic() - started) * 1000, 3))
    assert summary["elapsed_ms"] < 120000
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    line = "BRUSH_PRESSURE_INPUT_EVIDENCE " + json.dumps(summary, separators=(",", ":"))
    args.result_file.write_text(line + "\n", encoding="utf-8")
    print(line, flush=True)


if __name__ == "__main__":
    main()
