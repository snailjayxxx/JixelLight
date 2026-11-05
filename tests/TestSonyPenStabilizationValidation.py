"""Metadata controls; no RAW process, GPU or private study substitutes."""
from copy import deepcopy
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from ValidateBrushPressure import validate_sony_pen_stabilization, validate_brush_pressure


class SonyPenStabilizationValidation(unittest.TestCase):
    def setUp(self):
        self.data = dict(selected_pen_stabilization_smoke=True, selected_default_off=True,
            selected_starts=4, selected_extra_pressure_packets=36000, explicit_selection_clicks=2,
            selected_checkbox_held_disabled=True, selected_jitter_span_source_pixels=.1,
            selected_rule_changes_raw_intent=True, raw_unfiltered_jitter_fixed=False,
            stabilizer_specific_Sony_verified=False)

    def reject(self, key, value):
        d=deepcopy(self.data);d[key]=value
        with self.assertRaises((AssertionError, KeyError, TypeError)):
            validate_sony_pen_stabilization(d, selected=True)

    def test_selected_observation(self):
        validate_sony_pen_stabilization(self.data, selected=True)

    def test_legacy_observation(self):
        d=deepcopy(self.data);d.update(selected_pen_stabilization_smoke=False,
            selected_starts=0, selected_extra_pressure_packets=0, explicit_selection_clicks=0,
            selected_jitter_span_source_pixels=0, selected_rule_changes_raw_intent=False)
        validate_sony_pen_stabilization(d, selected=False)

    def test_missing_selection(self):
        d=deepcopy(self.data);del d['selected_pen_stabilization_smoke']
        with self.assertRaises(KeyError): validate_sony_pen_stabilization(d, selected=True)

    def test_not_selected(self): self.reject('selected_pen_stabilization_smoke',False)
    def test_preselected(self): self.reject('selected_default_off',False)
    def test_missing_start(self): self.reject('selected_starts',3)
    def test_wrong_start_type(self): self.reject('selected_starts',True)
    def test_missing_packet(self): self.reject('selected_extra_pressure_packets',35999)
    def test_extra_packet(self): self.reject('selected_extra_pressure_packets',36001)
    def test_wrong_packet_type(self): self.reject('selected_extra_pressure_packets',36000.0)
    def test_no_click(self): self.reject('explicit_selection_clicks',0)
    def test_wrong_click_type(self): self.reject('explicit_selection_clicks',True)
    def test_extra_click(self): self.reject('explicit_selection_clicks',5)
    def test_enabled_while_held(self): self.reject('selected_checkbox_held_disabled',False)
    def test_wrong_span(self): self.reject('selected_jitter_span_source_pixels',.5)
    def test_raw_intent_claim(self): self.reject('selected_rule_changes_raw_intent',False)
    def test_raw_jitter_claim(self): self.reject('raw_unfiltered_jitter_fixed',True)
    def test_native_acceptance_claim(self): self.reject('stabilizer_specific_Sony_verified',True)

    def test_selected_cannot_count_as_legacy(self):
        with self.assertRaises(AssertionError): validate_sony_pen_stabilization(self.data, selected=False)

    def test_selected_requires_real_source(self):
        with self.assertRaises(ValueError):
            validate_brush_pressure(Path('unused'),{},Path('unused-home'),Path('unused-report'),'cpu',stabilized_sony=True)


if __name__ == '__main__': unittest.main()
