"""Controlled one-pixel QA contract; synthetic reports are not UI acceptance."""
from __future__ import annotations
import math,re,sys
from ValidateBrushStabilization import REQUIRED,FALSE_SCOPE,LABELS,number,dabs,same_arithmetic,mouse_reference
def validate_one_pixel_report(data: dict, mode: str, transparent: bool,
                                  erase: bool, qpa: str | None = None) -> None:
    assert data["private_one_pixel_input_route_probe"] is True
    assert data["default_distance_half_pixel"] is True and data["explicit_distance_one_pixel"] is True
    assert data["distance_disabled_while_held"] is True
    assert data["unique_jitter_axis"] == ("vertical" if transparent else "horizontal")
    assert data["unique_jitter_max_source_pixels"] == .9999
    assert type(data["raw_oracle_first_refused_packet"]) is int
    assert mode in ("cpu", "gpu") and data["passed"] is True and data["error"] == ""
    assert all(data[key] is True for key in REQUIRED)
    assert all(data[key] is False for key in FALSE_SCOPE)
    assert data["source_kind"] == "owned-rgba64-png"
    for key, value in (("source_width",512),("source_height",320),("source_pixels",512*320),
                       ("source_alpha_u16",42000 if transparent else 65535),
                       ("single_stroke_dab_limit",4096),("document_dab_limit",16384),
                       ("selected_final_dabs",2)):
        assert type(data[key]) is int and data[key] == value
    assert re.fullmatch(r"[0-9a-f]{64}", data["source_sha256"])
    assert re.fullmatch(r"[0-9a-f]{7,40}", data["source_commit"])
    assert data["injection"] == "Qt-QPA-controlled-pressure-stylus"
    assert data["window_focus_injection"] == "Qt-QPA-controlled-window-focus"
    assert type(data["elapsed_ms"]) is int and 0 < data["elapsed_ms"] < data["deadline_ms"] == 60000
    assert type(data["deadline_ms"]) is int
    assert number(data["dpr"]) and data["dpr"] > 0
    assert data["actual_qpa"] in ("offscreen","cocoa","windows","xcb")
    assert not qpa or data["actual_qpa"] == qpa
    assert data["gpu_active"] is (mode == "gpu")
    if mode == "gpu":
        backend = "Direct3D 11" if sys.platform == "win32" else "Metal" if sys.platform == "darwin" else "OpenGL"
        assert backend in data["backend"]
    else:
        assert data["backend"].startswith("CPU")
    assert data["mode"] == ("erase" if erase else "paint")
    assert data["language"] == ("zh_CN" if mode == "gpu" else "en_US")
    assert number(data["position_deadband_source_pixels"]) and data["position_deadband_source_pixels"] == 1.
    assert data["selected_input_rule"] == "one-image-pixel-anchor"
    style = data["selected_style"]
    assert type(style) is dict and all(number(v) for v in style.values())
    assert style == {"radius":.04,"hardness":1,"opacity":.7}
    selected = data["selected_dabs"]; dabs(selected)
    assert len(selected) == 2 and selected[0][:2] == selected[1][:2]
    assert selected[0][2] == .501 and selected[1][2] == 0
    assert type(data["off_mode_first_refused_packet"]) is int and 0 < data["off_mode_first_refused_packet"] == data["raw_oracle_first_refused_packet"] <= 9000
    start, end = data["mouse_start_source"], data["mouse_end_source"]
    assert all(type(p) is list and len(p) == 2 and all(number(v) and 0 <= v <= 1 for v in p) for p in (start,end))
    movement = math.hypot((end[0]-start[0])*512, (end[1]-start[1])*320)
    observed = data["mouse_move_source_pixels"]
    assert number(observed) and 0 < observed <= .5 and 0 < movement <= .5
    assert abs(movement-observed) <= 8 * max(math.ulp(movement), math.ulp(observed))
    actual, expected = data["mouse_actual_dabs"], data["mouse_expected_dabs"]
    dabs(actual); dabs(expected)
    assert len(actual) > 1 and actual == expected
    same_arithmetic(expected, mouse_reference(start, end))
    captures = data["captures"]
    assert type(captures) is list and len(captures) == len(LABELS)
    assert {row["label"] for row in captures} == LABELS
    for row in captures:
        assert type(row["width"]) is int and type(row["height"]) is int
        assert row["width"] == 1180 * data["dpr"] and row["height"] == 720 * data["dpr"]
        assert number(row["dpr"]) and row["dpr"] == data["dpr"]
        assert type(row["path"]) is str and row["path"]
        assert re.fullmatch(r"[0-9a-f]{64}", row["sha256"])
