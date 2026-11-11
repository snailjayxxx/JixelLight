"""Native committed-preview evidence. Schema checks do not establish acceptance."""
from __future__ import annotations

import math
import re

SONY_SHA256 = "aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"
SONY_PIXELS = 32947264
BUDGET = 3 * 1024**3
MODES = ("route", "delivery", "pending", "retired-render", "monitor", "sony")
ROUTE_CHECKS = {
    "CPU_canonical_reused_after_owner_retirement", "actual_threaded_GUI_RHI_worker_roles",
    "hidden_window_retired_old_RHI_generation", "native_diagnostic_32_samples_original_CPU_RGB40_alpha_exact",
    "native_source_uploaded_once", "no_CPU_colour_or_mask_upload", "normal_no_finish",
    "original_monitor_presentation", "physical_source_key_shared_across_delivery_versions",
    "redo_reentered_native_route", "two_native_compute_dispatches", "undo_preserved_original_intent",
    "virtual_copy_distinct_actual_delivery",
}
SONY_CHECKS = {
    "actual_current_target_source_revision", "actual_native_source_two_dispatches_no_CPU_upload",
    "complete_CPU_scope_rank_survives_native_preview", "distinct_GUI_RHI_CPU_worker_roles",
    "explicit_Look_off_supported_native_policy", "independent_CPU_reference_not_on_GUI_or_RHI",
    "native_preview_samples_original_RGB40_alpha_exact", "ordinary_native_finish_zero",
    *("full_original_CPU_histogram_exact_at_" + x for x in ("waveform", "parade", "vectorscope")),
    *("full_original_CPU_" + x + "_image_exact" for x in ("waveform", "parade", "vectorscope")),
}


def number(value, low=0, high=2**53, *, integer=False):
    assert type(value) in ((int,) if integer else (int, float))
    assert math.isfinite(value) and low <= value <= high


def digest(value):
    assert type(value) is str and re.fullmatch(r"[0-9a-f]{64}", value)


def flags(data, true=(), false=()):
    for key in true:
        assert data[key] is True, key
    for key in false:
        assert data[key] is False, key


def facts(data):
    for key in ("GUI_accepted", "GUI_rejected", "compute_submissions", "enqueue_intents", "last_enqueue_revision", "last_submitted_revision", "render_retired_blocked", "retired_owners_released"):
        number(data[key], 0, integer=True)


def commit(value, expected=None, *, private_example=False):
    if private_example and expected == value == "unknown":
        return  # Only a caller explicitly reviewing an old private example.
    assert type(value) is str and re.fullmatch(r"[0-9a-f]{7,40}", value)
    if expected is not None:
        assert re.fullmatch(r"[0-9a-f]{7,40}", expected) and expected.startswith(value)


def trace(data, *, current_delivery=True):
    assert data["producer"] == "native-brush-gpu-compute"
    flags(data, ["current_frame_native"], ["cpu_colour_uploaded", "cpu_mask_uploaded"])
    if current_delivery:
        flags(data, ["current_delivery_matches"])
    for key in ("colour_dispatches", "coverage_dispatches", "source_uploads", "histogram_dispatches"):
        number(data[key], 1, integer=True)
    wire_bytes = data["histogram_dispatches"] * 16400
    number(data["histogram_readback_bytes"], wire_bytes, wire_bytes, integer=True)
    number(data["normal_finish_calls"], 0, 0, integer=True)
    for key in ("parameter_revision", "prepare_generation", "rhi_generation", "owner_epoch"):
        number(data[key], 1, integer=True)
    number(data["current_requested_revision"], 1, integer=True)
    assert data["parameter_revision"] == data["current_requested_revision"]
    for key in ("catalog", "photo", "version", "source_cache_key"):
        assert type(data[key]) is str and data[key]
    digest(data["input_source_key"])
    assert "manual interpolation" in data["presentation"] and "premultiplication" in data["presentation"]
    roles = [data[k] for k in ("gui_thread", "rhi_thread", "source_worker_thread", "frame_worker_thread")]
    assert all(type(x) is str and x for x in roles)
    assert roles[0] != roles[1] and all(x not in roles[:2] for x in roles[2:])
    for key in ("source_capture_serial", "controller_source_captures", "controller_source_cache_hits"):
        if key in data:
            number(data[key], 0 if key.endswith("hits") else 1, integer=True)
    if "delivery_facts" in data:
        facts(data["delivery_facts"])


def captures(data, count):
    rows = data["captures"]
    assert type(rows) is list and len(rows) == count
    assert len({r["path"] for r in rows}) == count
    for row in rows:
        assert type(row["path"]) is str and row["path"]
        number(row["width"], 1, 16384, integer=True)
        number(row["height"], 1, 16384, integer=True)
        if "sha256" in row:
            digest(row["sha256"])


def diagnostic(data):
    flags(data, ["available"])
    assert data["producer"] == "native-brush-gpu-compute"
    for k in ("width", "height", "pixels", "bytes", "parameter_revision"):
        number(data[k], 1, integer=True)
    assert data["pixels"] == data["width"] * data["height"]
    assert data["bytes"] == data["pixels"] * 16
    digest(data["pixel_sha256"])
    flags(data, false=["monitor_icc"])
    assert data["nonfinite_rgba"] == [0] * 4
    for n in data["nonfinite_rgba"]:
        number(n, 0, 0, integer=True)
    samples = data["diagnostic_samples_rgba16"]
    assert type(samples) is list and len(samples) == 32
    for s in samples:
        number(s["x"], 0, data["width"] - 1, integer=True)
        number(s["y"], 0, data["height"] - 1, integer=True)
        assert type(s["rgba16"]) is list and len(s["rgba16"]) == 4
        for v in s["rgba16"]:
            number(v, 0, 65535, integer=True)


def route(data):
    flags(data, ["passed", "source_unchanged"], ["physical_tablet", "real_RAW", "full_frame_CPU_comparison"])
    assert set(data["checks"]) == ROUTE_CHECKS
    flags(data["checks"], ROUTE_CHECKS)
    number(data["phase"], 8, 8, integer=True)
    trace(data["last_trace"])
    rows = data["observations"]
    native = [r for r in rows if r.get("producer") == "native-brush-gpu-compute" and "catalog" in r]
    assert len(native) == 4
    for row in native:
        trace(row)
    a, _, copy, restored = native
    assert a["photo"] == copy["photo"] and a["version"] != copy["version"] and a["owner_epoch"] != copy["owner_epoch"]
    assert a["input_source_key"] == copy["input_source_key"] == restored["input_source_key"]
    assert restored["rhi_generation"] > copy["rhi_generation"]
    numeric = [r for r in rows if "diagnostic_samples" in r]
    assert len(numeric) == 1
    number(numeric[0]["diagnostic_samples"], 32, 32, integer=True)
    number(numeric[0]["max_RGB16_error"], 0, 40, integer=True)
    number(numeric[0]["max_alpha16_error"], 0, 0, integer=True)
    assert numeric[0]["CPU_error"] == ""
    captures(data, 4)


def complete_cpu_fallback(row):
    geometry = row["request_geometry"]
    flags(geometry, ["prepared_request", "whole_frame_region"])
    for key in ("source_width", "source_height", "frame_width", "frame_height", "region_width", "region_height"):
        number(geometry[key], 1, 16384, integer=True)
    for key in ("region_x", "region_y"):
        number(geometry[key], 0, 0, integer=True)
    width, height = geometry["frame_width"], geometry["frame_height"]
    assert geometry["source_width"] == geometry["region_width"] == width
    assert geometry["source_height"] == geometry["region_height"] == height
    pixels = width * height
    number(pixels, 1, 32 * 1024**2, integer=True)
    number(row["pixels_compared"], pixels, pixels, integer=True)
    number(geometry["parameter_revision"], 1, integer=True)
    assert type(geometry["source_cache_key"]) is str and geometry["source_cache_key"]
    actual, expected = row["actual_diagnostic"], row["expected_CPU"]
    assert actual["available"] is True and expected["available"] is True
    assert actual["producer"] == "cpu-reference-upload"
    for frame in (actual, expected):
        for key, value in (("width", width), ("height", height), ("pixels", pixels), ("bytes", pixels * 16)):
            number(frame[key], value, value, integer=True)
        assert frame["nonfinite_rgba"] == [0] * 4
        for value in frame["nonfinite_rgba"]:
            number(value, 0, 0, integer=True)
    number(actual["parameter_revision"], geometry["parameter_revision"], geometry["parameter_revision"], integer=True)
    assert actual["source_cache_key"] == geometry["source_cache_key"]
    assert actual["pixel_sha256"] == expected["pixel_sha256"]
    digest(actual["pixel_sha256"])


def delivery(data, mode):
    selected = ["cache", "revision", "photo", "project", "hard", "detail", "lut"] if mode == "delivery" else [mode]
    flags(data, ["passed", "two_owned_sources_unchanged"], ["all_F0_F6_complete", "nonidentity_ICC", "physical_device_loss_or_OOM", "product_or_QA_owner_wait", "real_RAW", "synthesized_producer_callback"])
    number(data["owned_source_count"], 2, 2, integer=True)
    assert data["selected_cases"] == selected and data["active_case"] == ""
    rows = data["completed_cases"]
    assert [r["name"] for r in rows] == selected
    cpu_hashes = []
    for r in rows:
        flags(r, ["passed"])
        name = r["name"]
        if name in ("hard", "detail", "lut"):
            flags(r, ["entire_small_frame_float4_bit_exact"])
            assert r["producer"] == "cpu-reference-upload"
            complete_cpu_fallback(r)
            cpu_hashes.append(r["actual_diagnostic"]["pixel_sha256"])
            continue
        if name == "revision":
            flags(r, ["actual_Qt_MetaCall_interleave"])
            number(r["obsolete_revision"], 1, integer=True)
            number(r["latest_revision"], r["obsolete_revision"] + 1, integer=True)
            number(r["CPU_samples"], 32, 32, integer=True)
            number(r["max_RGB16_error"], 0, 40, integer=True)
            number(r["max_alpha16_error"], 0, 0, integer=True)
            facts(r["obsolete_facts"])
            number(r["GUI_rejections_before"], 0, integer=True)
            assert r["obsolete_facts"]["GUI_rejected"] > r["GUI_rejections_before"]
            diagnostic(r["actual_diagnostic"])
            assert r["actual_diagnostic"]["parameter_revision"] == r["latest_revision"]
            continue
        a, b = r["before"], r["after"]
        trace(a); trace(b)
        flags(r, ["old_GPU_owner_released"])
        if name == "cache":
            flags(r, ["direct_capture_serial_retained", "no_new_canonical_capture_and_cache_hit"])
            assert b["source_capture_serial"] == a["source_capture_serial"] > 0
            assert b["controller_source_captures"] == a["controller_source_captures"]
            assert b["controller_source_cache_hits"] > a["controller_source_cache_hits"]
            assert b["rhi_generation"] > a["rhi_generation"]
        elif name in ("photo", "project"):
            flags(r, ["actual_Qt_MetaCall_interleave", "obsolete_context_inactive"])
            old = r["obsolete_facts"]
            facts(old)
            number(r["GUI_accepted_before"], 0, integer=True)
            number(r["GUI_rejected_before"], 0, integer=True)
            assert old["GUI_accepted"] == r["GUI_accepted_before"] and old["GUI_rejected"] > r["GUI_rejected_before"]
            if name == "photo":
                assert a["photo"] != b["photo"] and a["input_source_key"] != b["input_source_key"]
            else:
                assert a["catalog"] != b["catalog"]
                for k in ("photo", "version", "input_source_key", "source_capture_serial"):
                    assert a[k] == b[k]
        else:
            old, signal = r["old_facts"], r["at_signal"]
            facts(old); facts(signal["product_facts_at_signal"])
            flags(r, ["obsolete_context_inactive"])
            assert old["retired_owners_released"] > 0 and signal["callback_thread"] == a["rhi_thread"]
            if name == "pending":
                flags(r, ["actual_GUI_photo_selection", "actual_afterFrameEnd_before_publication"], ["physical_GPU_busy_or_device_loss_claim"])
                assert signal["window_signal"] == "afterFrameEnd"
                number(signal["recorded_revision"], a["parameter_revision"]+1, integer=True)
                flags(signal, ["compute_recorded_before_publication"])
                at_signal_facts = signal["product_facts_at_signal"]
                assert at_signal_facts["last_submitted_revision"] == signal["recorded_revision"] != at_signal_facts["last_enqueue_revision"]
                assert old["GUI_accepted"] == r["GUI_accepted_before"]
                assert a["photo"] != b["photo"] and a["input_source_key"] != b["input_source_key"]
            else:
                flags(r, ["actual_afterSynchronizing_before_item_render", "actual_renderer_retired_guard_executed", "controlled_atomic_transport_injection"], ["actual_GUI_race_or_physical_loss_claim"])
                assert signal["window_signal"] == "afterSynchronizing"
                flags(signal, ["context_was_active", "controlled_atomic_context_retirement"])
                assert old["render_retired_blocked"] > 0
                assert a["input_source_key"] == b["input_source_key"] and a["source_capture_serial"] == b["source_capture_serial"]
    if mode == "delivery":
        assert len(set(cpu_hashes)) == 3
    captures(data, len(selected))


def monitor(data):
    flags(data, ["passed", "owned_source_unchanged", "actual_controller_QtQuick_native_frame", "controlled_owned_monitor_selection", "initial_trace_monitor_key_is_initial_publication_snapshot"], ["OS_monitor_profile_or_preferences_changed", "physical_nonidentity_monitor_calibration", "full_RAW_or_all_backend_or_package_acceptance", "Root_modified"])
    number(data["phase"], 4, 4, integer=True)
    number(data["GUI_deadline_ms"], 60000, 60000, integer=True)
    number(data["budget_limit_bytes"], BUDGET, BUDGET, integer=True)
    number(data["peak_operation_reserved_bytes"], 1, BUDGET, integer=True)
    digest(data["source_sha256"])
    a, b = data["initial_trace"], data["final_trace"]
    trace(a); trace(b)
    for k in ("catalog", "photo", "version", "source_cache_key", "input_source_key", "parameter_revision", "rhi_generation"):
        assert a[k] == b[k]
    assert a["delivery_facts"]["compute_submissions"] == b["delivery_facts"]["compute_submissions"] == 1
    assert data["actual_afterFrameEnd_RHI_thread"] == a["rhi_thread"] != a["gui_thread"]
    assert data["reference_worker"] not in (a["gui_thread"], a["rhi_thread"])
    rows = data["identity_P3_identity"]
    assert len(rows) == 3
    for j, r in enumerate(rows):
        assert r["profile"] == ("DisplayP3" if j == 1 else "identity")
        assert re.fullmatch(r"owned-icc:[0-9a-f]{64}" if j == 1 else "owned-native-identity", r["profile_key"])
        flags(r, ["source_revision_state_and_compute_count_unchanged"])
        number(r["frames_since_selection"], 1, integer=True)
        number(r["max_RGBA8_error"], 0, 1, integer=True)
        assert len(r["sample_points"]) == 3
        for q, uv in zip(r["sample_points"], ((.8, .75), (.8, .2), (.12, .8))):
            for k, value in zip(("u", "v"), uv):
                number(q[k], value, value)
            actual, expected = q["actual_RGBA8"], q["expected_RGBA8"]
            assert len(actual) == len(expected) == 4
            for value in actual + expected:
                number(value, 0, 255, integer=True)
            number(q["max_error"], 0, 1, integer=True)
            assert q["max_error"] == max(abs(x-y) for x, y in zip(actual, expected))
        assert r["max_RGBA8_error"] == max(q["max_error"] for q in r["sample_points"])
    assert rows[0]["sample_points"] == rows[2]["sample_points"] != rows[1]["sample_points"]
    captures(data, 3)


def sony(data):
    flags(data, ["gui_and_export_write_passed", "external_pixel_verification_pending", "external_source_verification_pending", "source_is_raw", "settings_isolated"], ["passed", "source_unchanged", "full_native_frame_CPU_comparison", "physical_tablet_verified", "full_unfiltered_raw_jitter_fixed", "native_float_RAW_decoder", "Root_native_GPU_accepted", "all_backend_or_package_accepted", "nonidentity_monitor_ICC_verified"])
    assert set(data["checks"]) == SONY_CHECKS
    flags(data["checks"], SONY_CHECKS)
    assert data["source_sha256"] == SONY_SHA256 and data["initial_import_Look_mode"] == "as-shot" and data["explicit_supported_Look_mode"] == "off"
    number(data["source_bytes"], 69120000, 69120000, integer=True)
    for k in ("source_pixels", "original_CPU_scope_pixels", "original_CPU_plot_pixels"):
        number(data[k], SONY_PIXELS, SONY_PIXELS, integer=True)
    number(data["gui_deadline_ms"], 60000, 60000, integer=True)
    number(data["gui_elapsed_ms"], 1, 59999, integer=True)
    number(data["export_deadline_ms"], 120000, 120000, integer=True)
    number(data["export_elapsed_ms"], 1, 119999, integer=True)
    number(data["operation_budget_bytes"], BUDGET, BUDGET, integer=True)
    number(data["operation_peak_reserved_bytes"], 1, BUDGET, integer=True)
    number(data["phase"], 6, 6, integer=True)
    assert data["histogram_bins_per_channel"] == [1024]*4
    for n in data["histogram_bins_per_channel"]:
        number(n, 1024, 1024, integer=True)
    trace(data["native_trace"]); diagnostic(data["diagnostic"])
    d, t = data["diagnostic"], data["native_trace"]
    for k in ("parameter_revision", "source_cache_key", "input_source_key"):
        assert d[k] == t[k]
    number(data["native_preview_sample_RGB16_max_error"], 0, 40, integer=True)
    number(data["native_preview_sample_alpha16_max_error"], 0, 0, integer=True)
    assert data["independent_original_CPU_reference_thread"] not in (t["gui_thread"], t["rhi_thread"])
    plots = data["three_plot_proofs"]
    assert [r["mode"] for r in plots] == ["waveform", "parade", "vectorscope"]
    for r in plots:
        flags(r, ["histogram_exact", "image_exact"])
        number(r["pixels"], SONY_PIXELS, SONY_PIXELS, integer=True)
        number(r["scope_rank"], 1, 1, integer=True)
        assert r["revision"] == t["parameter_revision"]
    captures(data, 3)
    assert [r["label"] for r in data["captures"]] == ["waveform", "parade", "vectorscope"]


def validate_report(data, mode, *, expected_commit=None, expected_backend=None, expected_qpa=None, private_example=False):
    assert mode in MODES and type(data) is dict and data["error"] == ""
    commit(data["source_commit"], expected_commit, private_example=private_example)
    backend = data["backend"] if mode == "route" else data["actual_backend"]
    qpa = data["actual_qpa"]
    assert qpa in ("windows", "cocoa", "xcb") and type(backend) is str
    if expected_qpa is not None:
        assert qpa == expected_qpa
    if expected_backend is not None:
        assert expected_backend in backend
    number(data["actual_dpr"] if mode == "route" else data["actual_DPR"], .125, 8)
    if mode != "sony":
        number(data["elapsed_ms"], 1, 59999, integer=True)
    if mode == "route":
        route(data)
    elif mode in ("delivery", "pending", "retired-render"):
        delivery(data, mode)
    elif mode == "monitor":
        monitor(data)
    else:
        sony(data)
