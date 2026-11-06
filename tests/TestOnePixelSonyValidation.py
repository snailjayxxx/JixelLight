"""Synthetic1px metadata and dispatch only; original23 controls are not rerun."""
from copy import deepcopy
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import ValidateBrushPressure as base
from ValidateOnePixelSonyValidation import validate_one_pixel_sony_pen

def sample():return dict(selected_pen_stabilization_smoke=True,selected_default_off=True,selected_starts=4,selected_extra_pressure_packets=36000,explicit_selection_clicks=1,selected_checkbox_held_disabled=True,selected_jitter_span_source_pixels=.9999,selected_rule_changes_raw_intent=True,raw_unfiltered_jitter_fixed=False,stabilizer_specific_Sony_verified=False,selected_pen_movement_pixels=1,selected_distance_default_half=True,explicit_one_pixel_distance_selected=True)
class OnePixelSonyValidation(unittest.TestCase):
 def reject(self,d):
  with self.assertRaises((AssertionError,KeyError,TypeError)):validate_one_pixel_sony_pen(d,selected=True)
 def test_new_positive(self):validate_one_pixel_sony_pen(sample(),selected=True)
 def test_strength_types_and_half(self):
  for v in [True,'1',.5,1.001,float('nan')]:d=sample();d['selected_pen_movement_pixels']=v;self.reject(d)
 def test_default_and_selection(self):
  for k in ['selected_default_off','selected_distance_default_half','explicit_one_pixel_distance_selected','selected_checkbox_held_disabled']:
   for v in [False,1]:d=sample();d[k]=v;self.reject(d)
 def test_mode_crossover(self):
  with self.assertRaises(AssertionError):validate_one_pixel_sony_pen(sample(),selected=False)
  with self.assertRaises(AssertionError):base.validate_sony_pen_stabilization(sample(),selected=True)
 def test_original_packet_and_start_counts(self):
  for k,v in [('selected_starts',3),('selected_starts',True),('selected_extra_pressure_packets',35999),('selected_extra_pressure_packets',36001),('selected_extra_pressure_packets',36000.0)]:d=sample();d[k]=v;self.reject(d)
 def test_click_and_span(self):
  for k,v in [('explicit_selection_clicks',0),('explicit_selection_clicks',True),('explicit_selection_clicks',5),('selected_jitter_span_source_pixels',.1)]:d=sample();d[k]=v;self.reject(d)
 def test_false_claims(self):
  for k,v in [('raw_unfiltered_jitter_fixed',True),('stabilizer_specific_Sony_verified',True),('selected_rule_changes_raw_intent',False)]:d=sample();d[k]=v;self.reject(d)
 def test_missing_fields(self):
  for k in ['selected_pen_movement_pixels','selected_distance_default_half','explicit_one_pixel_distance_selected']:d=sample();del d[k];self.reject(d)
 def test_requires_real_source_and_selected_mode(self):
  with patch.object(base.subprocess,'run') as run:
   for options in [dict(one_pixel=True),dict(stabilized_sony=True,one_pixel=True),dict(one_pixel=1)]:
    with self.assertRaises(ValueError):base.validate_brush_pressure(Path('unused'),{},Path('unused'),Path('unused-report'),'cpu',**options)
   run.assert_not_called()
 def test_new_dispatch_only_no_RAW_execution(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);exe=p/'app';exe.touch();verifier=p/'verifier';verifier.touch();raw=p/'fake.arw'
   with raw.open('wb') as f:f.truncate(base.SONY_BYTES)
   with patch.object(base,'sha256',return_value=base.SONY_SHA256),patch.object(base.subprocess,'run',return_value=SimpleNamespace(returncode=0)) as run:r=base.validate_brush_pressure(exe,{},p/'home',p/'case.json','cpu',raw,verifier,stabilized_sony=True,one_pixel=True)
   self.assertFalse(r['passed']);self.assertIn('--sony-one-pixel-brush-pressure-canvas-smoke-report',run.call_args.args[0]);self.assertEqual(r['mode'],'sony-one-pixel-brush-pressure-cpu');shutil.rmtree(r['private_output_directory'])
if __name__=='__main__':unittest.main()
