"""New1px schema/dispatch controls; no real application or package acceptance."""
import copy
from pathlib import Path
import shutil
import sys
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
import ValidateBrushStabilization as base
from ValidateOnePixelBrush import validate_one_pixel_report
from TestBrushStabilizationValidation import valid_report

def sample():
 d=valid_report();d.update(private_one_pixel_input_route_probe=True,default_distance_half_pixel=True,explicit_distance_one_pixel=True,distance_disabled_while_held=True,unique_jitter_axis='horizontal',unique_jitter_max_source_pixels=.9999,position_deadband_source_pixels=1,selected_input_rule='one-image-pixel-anchor',raw_oracle_first_refused_packet=2358,off_mode_first_refused_packet=2358);return d
class OnePixelBrushValidation(unittest.TestCase):
 def reject(self,d):
  with self.assertRaises((AssertionError,KeyError,ValueError,TypeError)):validate_one_pixel_report(d,'cpu',False,False,'offscreen')
 def test_new_positive(self):validate_one_pixel_report(sample(),'cpu',False,False,'offscreen')
 def test_explicit_default_and_held(self):
  for k in ['private_one_pixel_input_route_probe','default_distance_half_pixel','explicit_distance_one_pixel','distance_disabled_while_held']:
   for value in [False,1,None]:
    d=sample();d[k]=value;self.reject(d)
 def test_strength_is_numeric_exact_one(self):
  for value in [.5,1.00001,True,'1',float('nan')]:d=sample();d['position_deadband_source_pixels']=value;self.reject(d)
 def test_unique_span_axis_and_rule(self):
  for k,v in [('unique_jitter_max_source_pixels',.5),('unique_jitter_axis','vertical'),('selected_input_rule','half-image-pixel-anchor')]:d=sample();d[k]=v;self.reject(d)
 def test_raw_oracle_exact_integer_refusal(self):
  for k,v in [('raw_oracle_first_refused_packet',2358.0),('raw_oracle_first_refused_packet',True),('raw_oracle_first_refused_packet',0),('off_mode_first_refused_packet',2359)]:d=sample();d[k]=v;self.reject(d)
 def test_old_half_and_new_one_cannot_cross(self):
  self.reject(valid_report())
  with self.assertRaises(AssertionError):base.validate_stabilization_report(sample(),'cpu',False,False,'offscreen')
 def test_original_deadline_and_capacities(self):
  for k,v in [('elapsed_ms',60000),('single_stroke_dab_limit',4097),('document_dab_limit',16385),('selected_final_dabs',3)]:d=sample();d[k]=v;self.reject(d)
 def test_pressure_and_false_claims(self):
  for k in base.FALSE_SCOPE:d=sample();d[k]=True;self.reject(d)
  d=sample();d['selected_dabs'][0][2]=.5;self.reject(d)
 def test_qpa_and_raw_mouse(self):
  d=sample();d['actual_qpa']='';self.reject(d)
  d=sample();d['mouse_actual_dabs'][0][2]=.5;self.reject(d)
 def test_bad_option_never_launches(self):
  with patch.object(base.subprocess,'run') as run:
   for v in [1,'yes',None]:
    with self.assertRaises(ValueError):base.validate_brush_stabilization(Path('unused'),{},Path('unused'),Path('unused-report'),'cpu',one_pixel=v)
   run.assert_not_called()
 def test_new_flag_dispatch_synthetic_failure_preserved(self):
  with tempfile.TemporaryDirectory() as tmp:
   p=Path(tmp);exe=p/'app';exe.touch()
   with patch.object(base.subprocess,'run',return_value=SimpleNamespace(returncode=0)) as run:r=base.validate_brush_stabilization(exe,{},p/'home',p/'case.json','cpu',one_pixel=True)
   self.assertFalse(r['passed']);self.assertIn('--one-pixel-brush-stabilization-smoke-report',run.call_args.args[0]);self.assertEqual(r['mode'],'one-pixel-brush-stabilization-paint-cpu');shutil.rmtree(r['private_output_directory'])
if __name__=='__main__':unittest.main()
