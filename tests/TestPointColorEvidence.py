"""Reject incomplete working-color picker, history, scope and export evidence."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools'))
from ValidatePointColor import validate_report,validate_diagnostic,validate_pointColor
from TestDehazeEvidence import report_fixture as parent_fixture

def report_fixture(mode='gpu',qpa='cocoa'):
    data=parent_fixture(mode,qpa)
    selection=dict(schema=1,space='oklab',enabled=True,center=[.65,-.03,-.02],hue=0,saturation=0,luminance=0,hueRange=45,chromaRange=.12,lightnessRange=.4)
    initial=data['initial_whole_Develop'];initial['pointColor']=selection
    data['first_whole_Develop']=deepcopy(initial);data['first_whole_Develop']['pointColor']['hue']=75
    data['active_whole_Develop']=deepcopy(initial);data['active_whole_Develop']['exposure']=.3;data['active_whole_Develop']['pointColor']['hue']=-65
    data['reset_whole_Develop']={k:v for k,v in data['active_whole_Develop'].items() if k!='pointColor'}
    for key in list(data):
        if key.startswith('dehaze_'):del data[key]
    data.update(pointColor_cache_hit_delta=1 if mode=='cpu' else 0,pointColor_cache_required=mode=='cpu',pointColor_processing_policy='cached-cpu-reference' if mode=='cpu' else 'uncached-cpu-reference-upload',pointColor_header_text='POINT COLOR' if mode=='cpu' else '点颜色',pointColor_reset_text='Reset' if mode=='cpu' else '重置')
    prototype=data['pointers'][0];data['pointers']=[dict(prototype,target=key,render_revision=str(5+i),post_event_render_revision=str(6+i))for i,key in enumerate(['pickerButton','canvasPick','positiveHue','negativeHue','pointColorReset'])]
    data['picker']=dict(working_space='oklab',sample_before_point_color=True,exact_working_selection=True,reference_center=selection['center'],actual_center=selection['center'],pixel=[153,105],source_key='50')
    return data

def diagnostic_fixture(mode='gpu'):
    data=report_fixture(mode);manifest={'adjustments':deepcopy(data['active_whole_Develop'])}
    color=dict(available=True,backend='cpu-reference',point_color=dict(backend='cpu-reference',enabled=True,active=True,monitor_icc=False,selection_oklab=[.65,-.03,-.02],output_boundary='PerceptualLook'),spatial_denoise={'backend':'cpu-reference'},entries=[dict(id=key,pixels=64777,non_finite_values=0)for key in ['input_linear','wb_exposure','vignette','highlight_recovery','tone_neutral','perceptual_look','output_linear','output_transfer','look_lut','grain']])
    gpu={'available':False}if mode=='cpu'else dict(available=True,producer='cpu-reference-upload',point_color='cpu-reference',spatial_denoise='cpu-reference',numeric_source_policy='point-color-cpu-stage',monitor_icc=False,pixels=64777,bytes=1036432,nonfinite_rgba=[0,0,0,0],parameter_revision=30,source_cache_key='50')
    return manifest,dict(parameter_revision=30,gpu_context={'source_cache_key':'50'},color_stages=color,gpu_working_output=gpu)

class PointColorEvidenceTests(unittest.TestCase):
    def test_six_backend_and_language_contexts(self):
        for mode in ['cpu','gpu']:
            for qpa in ['cocoa','windows','xcb']:
                self.assertTrue(validate_report(report_fixture(mode,qpa),mode,qpa,'1234567'))
                self.assertTrue(validate_diagnostic(report_fixture(mode,qpa),*diagnostic_fixture(mode),mode))
    def test_complete_selection_history_and_scope_are_required(self):
        edits=[(('phase',),15),(('deadline_ms',),60000.),(('active_whole_Develop','pointColor','space'),'monitor-rgb'),(('active_whole_Develop','pointColor','center'),[.65,-.04,-.02]),(('active_whole_Develop','pointColor','hue'),-181),(('active_whole_Develop','exposure'),.5),(('reset_whole_Develop','contrast'),3),(('scope','red',0),64776),(('pointers',2,'post_event_render_revision'),'1'),(('pointers',1,'QPA_mouse_route'),False),(('product_development_complete',),True)]
        for path,value in edits:
            data=report_fixture();cursor=data
            for key in path[:-1]:cursor=cursor[key]
            cursor[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(ValueError):validate_report(data,'gpu','cocoa')
    def test_picker_cannot_use_stale_source_or_display_coordinates(self):
        for key,value in [('source_key','49'),('working_space','display-p3'),('sample_before_point_color',False),('actual_center',[.6,0,0]),('exact_working_selection',False),('pixel',[154,105])]:
            data=report_fixture();data['picker'][key]=value
            with self.subTest(key=key),self.assertRaises(ValueError):validate_report(data,'gpu','cocoa')
    def test_cached_cpu_and_uncached_upload_policies_are_distinct(self):
        for mode in ['cpu','gpu']:
            data=report_fixture(mode);data['pointColor_cache_hit_delta']=0 if mode=='cpu'else 1
            with self.assertRaises(ValueError):validate_report(data,mode,'cocoa')
            data=report_fixture(mode);data['pointColor_processing_policy']='gpu-compute'
            with self.assertRaises(ValueError):validate_report(data,mode,'cocoa')
    def test_false_compute_and_stale_diagnostics_are_rejected(self):
        edits=[(('gpu_working_output','producer'),'gpu-compute'),(('gpu_working_output','point_color'),'gpu-compute'),(('gpu_working_output','numeric_source_policy'),'none'),(('gpu_working_output','source_cache_key'),'49'),(('gpu_working_output','parameter_revision'),29),(('color_stages','point_color','selection_oklab'),[.6,0,0]),(('color_stages','point_color','monitor_icc'),True),(('color_stages','entries',5,'pixels'),1),(('color_stages','entries',5,'non_finite_values'),1)]
        for path,value in edits:
            manifest,stages=diagnostic_fixture();cursor=stages
            for key in path[:-1]:cursor=cursor[key]
            cursor[path[-1]]=value
            with self.subTest(path=path),self.assertRaises(ValueError):validate_diagnostic(report_fixture(),manifest,stages,'gpu')
    def test_occupied_namespace_is_preserved(self):
        with tempfile.TemporaryDirectory()as temp:
            root=Path(temp);binary=root/'app';binary.write_bytes(b'owned binary');report=root/'check.json';sibling=root/'check.native.json';sibling.write_bytes(b'original evidence')
            with patch('ValidatePointColor.subprocess.run')as run:
                with self.assertRaises(ValueError):validate_pointColor(binary,{},root/'home',report,'cpu')
                run.assert_not_called()
            self.assertEqual(sibling.read_bytes(),b'original evidence');self.assertFalse(report.exists())
if __name__=='__main__':unittest.main()
