"""Synthetic contract/dispatch tests, never native GPU or GUI acceptance."""
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import NativeBrushEvidence as gate
import ValidateNativeBrush as runner

EXAMPLES = json.loads((Path(__file__).parent / "fixtures/native-brush-contract-examples.json").read_text())
assert EXAMPLES["synthetic_contract_examples_not_execution_evidence"] is True


def example(mode):
    return copy.deepcopy(EXAMPLES["modes"][mode])


def png(width=2, height=2):
    def chunk(kind, data):
        return struct.pack(">I", len(data))+kind+data+struct.pack(">I", zlib.crc32(kind+data) & 0xffffffff)
    return b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))+chunk(b"IDAT", zlib.compress((b"\0"+bytes([20, 80, 120, 255])*width)*height))+chunk(b"IEND", b"")


class NativeBrushContractTests(unittest.TestCase):
    def validate(self, data, mode):
        gate.validate_report(data, mode, expected_commit="d00dcaf0123456789", expected_qpa="cocoa", expected_backend="Metal")

    def test_fallback_covers_frozen_whole_request_at_each_preview_size(self):
        for width, height in ((704, 512), (522, 380), (640, 416)):
            d = example("delivery")
            for row in d["completed_cases"]:
                if row["name"] not in ("hard", "detail", "lut"):
                    continue
                geometry = row["request_geometry"]
                for key in ("source_width", "frame_width", "region_width"):
                    geometry[key] = width
                for key in ("source_height", "frame_height", "region_height"):
                    geometry[key] = height
                row["pixels_compared"] = width * height
                for frame in (row["actual_diagnostic"], row["expected_CPU"]):
                    frame.update(width=width, height=height, pixels=width * height, bytes=width * height * 16)
            with self.subTest(width=width, height=height):
                self.validate(d, "delivery")

    def test_fallback_refuses_missing_or_partial_request_geometry(self):
        for field, value in (("whole_frame_region", False), ("prepared_request", False), ("region_x", 1), ("region_y", 1), ("region_width", 703), ("region_height", 511), ("source_width", 705), ("frame_height", 511)):
            d = example("delivery"); row = next(x for x in d["completed_cases"] if x["name"] == "hard")
            row["request_geometry"][field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.validate(d, "delivery")
        d = example("delivery"); next(x for x in d["completed_cases"] if x["name"] == "hard").pop("request_geometry")
        with self.assertRaises(KeyError):
            self.validate(d, "delivery")

    def test_fallback_refuses_equal_area_wrong_shape_or_stale_request(self):
        for field, value in (("width", 512), ("height", 704), ("parameter_revision", 1), ("source_cache_key", "different-source")):
            d = example("delivery"); row = next(x for x in d["completed_cases"] if x["name"] == "hard")
            row["actual_diagnostic"][field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.validate(d, "delivery")
        d = example("delivery"); row = next(x for x in d["completed_cases"] if x["name"] == "hard")
        for frame in (row["actual_diagnostic"], row["expected_CPU"]):
            frame.update(width=512, height=704)
        with self.assertRaises(AssertionError):
            self.validate(d, "delivery")

    def test_fallback_geometry_and_fingerprint_counts_are_typed(self):
        for field in ("frame_width", "frame_height", "region_x", "region_y", "parameter_revision"):
            d = example("delivery"); row = next(x for x in d["completed_cases"] if x["name"] == "hard")
            row["request_geometry"][field] = False
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.validate(d, "delivery")
        for field, value in (("width", True), ("height", "512"), ("pixels", 360448.0), ("bytes", 5767168.0), ("nonfinite_rgba", [False] * 4)):
            d = example("delivery"); row = next(x for x in d["completed_cases"] if x["name"] == "hard")
            row["expected_CPU"][field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.validate(d, "delivery")

    def test_native_upload_matches_independent_frozen_input_and_identity(self):
        mutations = (("pixel_sha256", "d" * 64), ("source_cache_key", "other-source"),
                     ("input_source_key", "f" * 64), ("parameter_revision", 99),
                     ("used_for_compute", False), ("monitor_icc", True),
                     ("alpha", "premultiplied"), ("encoding", "unknown"), ("bytes", 1))
        for field, value in mutations:
            d = example("route"); frame = next(r for r in d["observations"] if "diagnostic_samples_rgba16" in r)
            frame["source_staging"][field] = value
            with self.subTest(field=field), self.assertRaises((AssertionError, KeyError)):
                self.validate(d, "route")
        d = example("route"); next(r for r in d["observations"] if "source_upload_reference" in r)["source_upload_reference"]["pixel_sha256"] = "d" * 64
        with self.assertRaises(AssertionError): self.validate(d, "route")
        d = example("delivery"); row = next(r for r in d["completed_cases"] if r["name"] == "revision")
        row["expected_source_upload"]["pixel_sha256"] = "d" * 64
        with self.assertRaises(AssertionError): self.validate(d, "delivery")

    def test_native_upload_requires_aggregate_accounting_and_honest_unavailability(self):
        for field, value in (("readback_budget_bytes", 64 * 1024**2 + 1), ("readback_requested_bytes", 1), ("readback_received_bytes", 1)):
            d = example("sony")["diagnostic"]; d[field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError): gate.diagnostic(d)
        for reason in ("budget", "resource-budget", "source-retired"):
            d = example("sony")["diagnostic"]; source = d["source_staging"]
            source.update(available=False, reason=reason, error="owned declared refusal", required_bytes=d["bytes"])
            source.pop("pixel_sha256"); d["readback_requested_bytes"] = d["readback_received_bytes"] = d["bytes"]
            if reason == "budget": d["readback_budget_bytes"] = d["bytes"]
            gate.diagnostic(d)
            source["pixel_sha256"] = "c" * 64
            with self.assertRaises(AssertionError): gate.diagnostic(d)
        d = example("sony")["diagnostic"]; d["source_staging"].update(available=False, reason="budget", error="dishonest budget", required_bytes=d["bytes"])
        d["source_staging"].pop("pixel_sha256"); d["readback_requested_bytes"] = d["readback_received_bytes"] = d["bytes"]
        with self.assertRaises(AssertionError): gate.diagnostic(d)

    def test_native_upload_numbers_and_counts_refuse_booleans(self):
        for field, value in (("width", True), ("height", "294"), ("bytes", False), ("parameter_revision", True),
                             ("base_exposure_stops", False), ("raw_source", 0), ("available", 1),
                             ("nonfinite_rgba", [False] * 4), ("ranges_rgba", [[False, 1]] * 4)):
            d = example("sony")["diagnostic"]; d["source_staging"][field] = value
            with self.subTest(field=field), self.assertRaises((AssertionError, TypeError)):
                gate.diagnostic(d)
        d = example("sony")["diagnostic"]; d["readback_requested_bytes"] = True
        with self.assertRaises(AssertionError): gate.diagnostic(d)

    def test_six_synthetic_examples(self):
        for mode in gate.MODES:
            with self.subTest(mode=mode):
                self.validate(example(mode), mode)

    def test_commit_must_bind_actual_build(self):
        d = example("monitor")
        for value in ("unknown", "d00dcaa", True, "", "d00dcaf-not-a-commit"):
            d["source_commit"] = value
            with self.subTest(value=value), self.assertRaises((AssertionError, TypeError)):
                self.validate(d, "monitor")

    def test_unknown_is_only_an_explicit_private_example(self):
        gate.commit("unknown", "unknown", private_example=True)
        with self.assertRaises(AssertionError):
            gate.commit("unknown", "unknown")

    def test_boolean_cannot_masquerade_as_number(self):
        for integer in (True, False):
            for v in (True, False, float("nan"), float("inf"), "1", None):
                with self.subTest(v=v, integer=integer), self.assertRaises(AssertionError):
                    gate.number(v, integer=integer)
        for mode in gate.MODES:
            d = example(mode)
            d["actual_dpr" if mode == "route" else "actual_DPR"] = True
            with self.subTest(mode=mode), self.assertRaises(AssertionError):
                self.validate(d, mode)

    def test_original_60_second_gui_boundary(self):
        for mode in gate.MODES:
            d = example(mode)
            d["gui_elapsed_ms" if mode == "sony" else "elapsed_ms"] = 60000
            with self.subTest(mode=mode), self.assertRaises(AssertionError):
                self.validate(d, mode)

    def test_wrong_backend_or_direct_runtime_qpa_rejected(self):
        for mode in gate.MODES:
            d = example(mode)
            d["actual_qpa"] = "offscreen"
            with self.subTest(mode=mode), self.assertRaises(AssertionError):
                self.validate(d, mode)
            d = example(mode)
            d["backend" if mode == "route" else "actual_backend"] = "CPU reference"
            with self.assertRaises(AssertionError):
                self.validate(d, mode)

    def test_no_CPU_upload_substitution_or_relaxed_alpha(self):
        d = example("route")
        for key in ("cpu_colour_uploaded", "cpu_mask_uploaded"):
            q = copy.deepcopy(d); q["last_trace"][key] = True
            with self.subTest(key=key), self.assertRaises(AssertionError):
                self.validate(q, "route")
        q = example("delivery")
        r = next(x for x in q["completed_cases"] if x["name"] == "revision")
        r["max_alpha16_error"] = 1
        with self.assertRaises(AssertionError):
            self.validate(q, "delivery")

    def test_new_route_requires_actual_delivery_match_field(self):
        d = example("route"); d["last_trace"].pop("current_delivery_matches")
        with self.assertRaises(KeyError):
            self.validate(d, "route")

    def test_complete_CPU_fallback_hash_and_size_are_mandatory(self):
        for name in ("hard", "detail", "lut"):
            for field, value in (("pixel_sha256", "f"*64), ("pixels", 360447), ("bytes", 1)):
                d = example("delivery"); row = next(x for x in d["completed_cases"] if x["name"] == name)
                row["actual_diagnostic"][field] = value
                with self.subTest(name=name, field=field), self.assertRaises(AssertionError):
                    self.validate(d, "delivery")

    def test_direct_cache_counter_is_not_only_an_equal_key(self):
        d = example("delivery"); r = d["completed_cases"][0]
        r["after"]["controller_source_cache_hits"] = r["before"]["controller_source_cache_hits"]
        with self.assertRaises(AssertionError):
            self.validate(d, "delivery")

    def test_pending_temporal_guard_cannot_use_old_publication(self):
        d = example("pending"); r = d["completed_cases"][0]
        r["at_signal"]["product_facts_at_signal"]["last_enqueue_revision"] = r["at_signal"]["recorded_revision"]
        with self.assertRaises(AssertionError):
            self.validate(d, "pending")

    def test_atomic_injection_is_not_hardware_loss(self):
        d = example("retired-render"); d["completed_cases"][0]["actual_GUI_race_or_physical_loss_claim"] = True
        with self.assertRaises(AssertionError):
            self.validate(d, "retired-render")

    def test_monitor_not_physical_calibration_or_recompute(self):
        for field, value in (("physical_nonidentity_monitor_calibration", True), ("OS_monitor_profile_or_preferences_changed", True)):
            d = example("monitor"); d[field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.validate(d, "monitor")
        d = example("monitor"); d["final_trace"]["delivery_facts"]["compute_submissions"] = 2
        with self.assertRaises(AssertionError):
            self.validate(d, "monitor")

    def test_Sony_requires_external_completion_not_forged_native_pass(self):
        for field, value in (("passed", True), ("source_unchanged", True), ("external_pixel_verification_pending", False), ("source_pixels", 1), ("operation_peak_reserved_bytes", gate.BUDGET+1)):
            d = example("sony"); d[field] = value
            with self.subTest(field=field), self.assertRaises(AssertionError):
                self.validate(d, "sony")

    def test_Sony_requires_full_three_original_CPU_plots(self):
        d = example("sony"); d["three_plot_proofs"][2]["image_exact"] = False
        with self.assertRaises(AssertionError):
            self.validate(d, "sony")
        d = example("sony"); d["three_plot_proofs"][0]["scope_rank"] = 0
        with self.assertRaises(AssertionError):
            self.validate(d, "sony")

    def test_backend_dispatch_preserves_sdk_stripping_and_original_environment(self):
        original = {"PATH": "owned-runtime", "JIXELLIGHT_FORCE_CPU": "1", "QT_QUICK_BACKEND": "software", "QT_SCALE_FACTOR": "9"}
        for platform, qpa, backend in (("win32", "windows", "d3d11"), ("darwin", "cocoa", "metal"), ("linux", "xcb", "opengl")):
            env, actual, _ = runner.backend_environment(original, Path("owned-home"), "monitor", platform)
            self.assertEqual(actual, qpa); self.assertEqual(env["QSG_RHI_BACKEND"], backend)
            self.assertEqual(env["PATH"], original["PATH"]); self.assertNotIn("QT_ROOT_DIR", env)
            self.assertNotIn("JIXELLIGHT_FORCE_CPU", env); self.assertNotIn("QT_SCALE_FACTOR", env)
        self.assertEqual(original["QT_SCALE_FACTOR"], "9")
        self.assertEqual(runner.backend_environment(original, Path("owned-home"), "sony", "darwin")[0]["QT_SCALE_FACTOR"], ".5")

    def test_command_modes_do_not_dispatch_legacy_QA(self):
        for mode in gate.MODES:
            args = runner.command(Path("app"), Path("report"), mode, Path("owned.arw") if mode == "sony" else None)
            self.assertIn("--native-brush-", args[1]); self.assertNotIn("--smoke-report", args)
            if mode in ("delivery", "pending", "retired-render"):
                self.assertEqual(args[-1], "all" if mode == "delivery" else mode)

    def test_PNG_CRC_truncation_or_wrong_shape_refused(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/"owned.png"; p.write_bytes(png())
            self.assertTrue(runner.png_evidence(p, {"width": 2, "height": 2})["CRC_all_chunks"])
            for b in (png()[:-1], png()[:-20]+b"corruption"+png()[-10:]):
                p.write_bytes(b)
                with self.assertRaises((AssertionError, struct.error, zlib.error)):
                    runner.png_evidence(p, {"width": 2, "height": 2})

    def test_output_alias_guards_include_hardlinks_and_symlinks(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)/"owned-input"; p.write_text("owned marker")
            hard, symbolic = p.with_name("hard"), p.with_name("symbolic")
            os.link(p, hard); symbolic.symlink_to(p)
            for candidate in (p, hard, symbolic):
                with self.subTest(candidate=candidate.name), self.assertRaises(ValueError):
                    runner.reject_alias(candidate, [p])
            self.assertEqual(p.read_text(), "owned marker")

    def mock_transport(self, mode, *, verifier_ok=True, qml_error=False):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); binary, raw, verifier = root/"app", root/"owned.arw", root/"verifier"
            binary.write_text("owned executable"); verifier.write_text("owned verifier")
            if mode == "sony":
                with raw.open("wb") as f: f.truncate(69120000)  # Sparse owned marker, never a real photograph.
            output = root/"artifacts/result.json"; created = {}; original_digest = runner.digest
            def digest(path):
                return gate.SONY_SHA256 if Path(path).resolve() == raw.resolve() else original_digest(path)
            def process(args, **kwargs):
                if Path(args[0]).resolve() == verifier.resolve():
                    created["verifier_called"] = True
                    proof = {"passed": verifier_ok, "exact_pixels_and_ICC": True, "plan_hash_matches_actual_UI_export": True,
                             "source_commit": "d00dcaf", "plan_data_sha256": "a"*64, "raw_sha256": gate.SONY_SHA256,
                             "source_pixels": gate.SONY_PIXELS, "decoded_pixels": gate.SONY_PIXELS,
                             "operation_budget_bytes": gate.BUDGET, "operation_peak_reserved_bytes": 1024, "operation_end_charged_bytes": 0}
                    return type("Run", (), {"returncode": 0 if verifier_ok else 1, "stdout": json.dumps(proof), "stderr": ""})()
                native = Path(args[2]); native.parent.mkdir(parents=True, exist_ok=True); created["native"] = native
                d = example(mode)
                for i, r in enumerate(d["captures"]):
                    p = native.parent/(f"owned-{i}-controls.png" if mode == "sony" else f"owned-{i}.png")
                    p.write_bytes(png()); r.update(path=str(p), width=2, height=2)
                    if "sha256" in r:r["sha256"] = original_digest(p)
                native.write_text(json.dumps(d))
                if mode == "sony":
                    Path(str(native)+".frozen.json").write_text(json.dumps({"source_commit": "d00dcaf", "source_sha256": gate.SONY_SHA256, "plan_data_sha256": "a"*64}))
                    created["photo"] = Path(str(native)+".export.png"); created["photo"].write_bytes(png())
                return type("Run", (), {"returncode": 0, "stdout": "", "stderr": "TypeError: owned injected error" if qml_error else ""})()
            with patch.object(runner.sys, "platform", "darwin"), patch.object(runner.subprocess, "run", side_effect=process), patch.object(runner, "digest", side_effect=digest), patch.object(runner.time, "monotonic", side_effect=[0, 70]), patch.object(runner.tempfile, "mkdtemp", side_effect=lambda **_: str(root/"private-outside-artifacts")):
                result = runner.validate_native_brush(binary, {}, root/"home", output, mode,
                    raw=raw if mode == "sony" else None, verifier=verifier if mode == "sony" else None, expected_commit="d00dcaf")
            self.assertEqual(json.loads(Path(str(output)+".native.json").read_text()), json.loads(created["native"].read_text()))
            if mode == "sony":
                self.assertTrue(created.get("verifier_called"), result)
                self.assertEqual(created["photo"].exists(), not verifier_ok or qml_error)
                self.assertFalse(any(p.name.endswith(".export.png") for p in output.parent.rglob("*")))
                self.assertFalse(result["report"]["passed"] if result["passed"] else False)
            return result

    def test_owned_transport_mock_preserves_raw_report_and_capture_proofs(self):
        for mode in gate.MODES[:-1]:
            with self.subTest(mode=mode):
                result = self.mock_transport(mode)
                self.assertTrue(result["passed"], result)
                self.assertEqual(len(result["captures"]), runner.CAPTURES[mode])

    def test_Sony_transport_mock_requires_exact_verifier_then_cleans_private_photo(self):
        result = self.mock_transport("sony")
        self.assertTrue(result["passed"], result)
        self.assertTrue(result["independent_exact_export_verified"])
        self.assertFalse(result["report"]["source_unchanged"])

    def test_Sony_transport_mock_preserves_failed_photo(self):
        self.assertFalse(self.mock_transport("sony", verifier_ok=False)["passed"])

    def test_QML_failure_stays_failed_even_after_exact_export(self):
        result = self.mock_transport("sony", qml_error=True)
        self.assertFalse(result["passed"])
        self.assertIn("QML runtime errors", result["error"])

    def test_existing_prefix_is_preserved_before_process_launch(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); binary = root/"app"; binary.write_text("owned binary")
            output = root/"report.json"; prior = Path(str(output)+".native.json"); prior.write_text("original failure")
            with patch.object(runner.subprocess, "run") as process:
                with self.assertRaises(ValueError):
                    runner.validate_native_brush(binary, {}, root/"home", output, "monitor")
                process.assert_not_called()
            self.assertEqual(prior.read_text(), "original failure")

    def test_histogram_wire_is_exact_for_actual_cumulative_job_count(self):
        d = example("monitor")["initial_trace"]
        d["histogram_dispatches"] = 2; d["histogram_readback_bytes"] = 32800
        gate.trace(d)
        for value in (32799, 32801, True):
            d["histogram_readback_bytes"] = value
            with self.subTest(value=value), self.assertRaises(AssertionError):
                gate.trace(d)

    def test_transport_timeout_preserves_partial_log(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory); binary = root/"app"; binary.write_text("owned binary")
            output = root/"report.json"
            error = runner.subprocess.TimeoutExpired(["owned-app"], 80, output=b"owned partial output", stderr=b"owned partial error")
            with patch.object(runner.subprocess, "run", side_effect=error):
                result = runner.validate_native_brush(binary, {}, root/"home", output, "route")
            self.assertFalse(result["passed"])
            log = Path(str(output)+".log").read_text()
            self.assertIn("owned partial output", log); self.assertIn("owned partial error", log)

    def test_runtime_counter_values_reject_boolean_numbers(self):
        d = example("monitor")["initial_trace"]; gate.trace(d)
        for key in ("source_capture_serial", "controller_source_captures", "controller_source_cache_hits"):
            bad = copy.deepcopy(d); bad[key] = True
            with self.subTest(key=key), self.assertRaises(AssertionError):
                gate.trace(bad)
        for key in d["delivery_facts"]:
            bad = copy.deepcopy(d); bad["delivery_facts"][key] = False
            with self.subTest(key=key), self.assertRaises(AssertionError):
                gate.trace(bad)


    def test_retired_transport_requires_after_sync_and_real_guard(self):
        d = example("retired-render"); self.validate(d, "retired-render")
        bad = copy.deepcopy(d); bad["completed_cases"][0]["at_signal"]["window_signal"] = "beforeRendering"
        with self.assertRaises(AssertionError):self.validate(bad, "retired-render")
        bad = copy.deepcopy(d); bad["completed_cases"][0]["old_facts"]["render_retired_blocked"] = 0
        with self.assertRaises(AssertionError):self.validate(bad, "retired-render")
        bad = copy.deepcopy(d); bad["completed_cases"][0].pop("actual_afterSynchronizing_before_item_render")
        with self.assertRaises(KeyError):self.validate(bad, "retired-render")


if __name__ == "__main__":
    unittest.main()
