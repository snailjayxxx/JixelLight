"""One-pixel Sony metadata, never a substitute for RAW/export verification."""
def validate_one_pixel_sony_pen(data: dict, *, selected: bool) -> None:
    """Require explicit1px selection and the original Sony observations."""
    assert selected is True
    assert data["selected_pen_stabilization_smoke"] is selected
    assert data["raw_unfiltered_jitter_fixed"] is False
    assert data["stabilizer_specific_Sony_verified"] is False
    for name in ("selected_starts", "selected_extra_pressure_packets", "explicit_selection_clicks"):
        assert type(data[name]) is int
    if not selected:
        assert data["selected_starts"] == data["selected_extra_pressure_packets"] == data["explicit_selection_clicks"] == 0
        assert data["selected_jitter_span_source_pixels"] == 0
        assert data["selected_rule_changes_raw_intent"] is False
        return
    assert type(data["selected_pen_movement_pixels"]) in (int,float) and data["selected_pen_movement_pixels"] == 1
    assert data["selected_distance_default_half"] is True
    assert data["explicit_one_pixel_distance_selected"] is True
    assert data["selected_default_off"] is True
    assert data["selected_starts"] == 4 and data["selected_extra_pressure_packets"] == 36000
    assert 1 <= data["explicit_selection_clicks"] <= 4
    assert data["selected_checkbox_held_disabled"] is True
    assert data["selected_jitter_span_source_pixels"] == .9999
    assert data["selected_rule_changes_raw_intent"] is True
