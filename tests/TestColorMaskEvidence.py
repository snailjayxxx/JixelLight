"""Reject incomplete, stale or falsely attributed range-mask evidence."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ValidateColorMask import validate_report, validate_diagnostic, validate_color_mask, validate_window_evidence
from TestDehazeEvidence import report_fixture as parent_report
from TestPointColorEvidence import diagnostic_fixture as parent_diagnostic


def report_fixture(mode='gpu', qpa='cocoa'):
    data = parent_report(mode, qpa)
    for key in list(data):
        if key.startswith('dehaze_'):
            del data[key]
    initial = data['initial_whole_Develop']
    initial['colorMask'] = dict(schema=1,space='oklab',enabled=True,inverted=False,exposure=0,center=[.6,.02,.01],hueRange=45,chromaRange=.12,lightnessRange=.4)
    data['first_whole_Develop'] = deepcopy(initial)
    data['first_whole_Develop']['colorMask']['exposure'] = 1.5
    data['active_whole_Develop'] = deepcopy(initial)
    data['active_whole_Develop']['exposure'] = .3
    data['active_whole_Develop']['colorMask'].update(exposure=-1.5, inverted=True)
    data['reset_whole_Develop'] = {k: v for k, v in data['active_whole_Develop'].items() if k != 'colorMask'}
    data.update(active_mask_changes_pixels=True,undo_steps_to_first=3, colorMask_cache_hit_delta=1,
                colorMask_cache_required=True,
                colorMask_processing_policy='cached-cpu-reference' if mode == 'cpu' else 'cached-gpu-compute',
                colorMask_header_text='COLOR RANGE MASK' if mode == 'cpu' else '颜色范围蒙版',
                colorMask_reset_text='Reset' if mode == 'cpu' else '重置')
    prototype = data['pointers'][0]
    targets = ['pickerButton','canvasPick','hueRange','chromaRange','lightnessRange','positiveExposure','negativeExposure','invertRange','colorMaskReset']
    data['pointers'] = [dict(prototype, target=key, render_revision=str(5+i),
                             post_event_render_revision=str(6+i)) for i, key in enumerate(targets)]
    data['picker']=dict(working_space='oklab',sample_before_user_WB_EV=True,exact_working_selection=True,reference_center=[.6,.02,.01],actual_center=[.6,.02,.01],pixel=[153,105],source_key='50')
    return data


def diagnostic_fixture(mode='gpu'):
    _, stages = parent_diagnostic(mode)
    data = report_fixture(mode)
    color = stages['color_stages']
    color.pop('point_color')
    color['color_mask']=dict(backend='cpu-reference',enabled=True,active=True,monitor_icc=False,output_boundary='Vignette',selection_oklab=[.6,.02,.01],settings=deepcopy(data['active_whole_Develop']['colorMask']))
    if mode == 'gpu':
        stages['gpu_working_output'].pop('point_color')
        stages['gpu_working_output'].update(producer='gpu-compute', color_mask='gpu-compute',
                                             spatial_denoise='gpu-compute', numeric_source_policy='none')
    return {'adjustments': deepcopy(data['active_whole_Develop'])}, stages


class ColorMaskEvidenceTests(unittest.TestCase):
    def test_six_backend_language_contexts(self):
        for mode in ('cpu', 'gpu'):
            for qpa in ('cocoa', 'windows', 'xcb'):
                self.assertTrue(validate_report(report_fixture(mode, qpa), mode, qpa, '1234567'))
                self.assertTrue(validate_diagnostic(report_fixture(mode, qpa), *diagnostic_fixture(mode), mode))

    def test_whole_state_ranges_and_pointer_revisions_are_required(self):
        edits = [(('phase',), 15), (('active_whole_Develop','colorMask','center'), [.6,.03,.01]),
                 (('active_whole_Develop', 'colorMask', 'inverted'), False),
                 (('active_whole_Develop', 'colorMask', 'exposure'), -4),
                 (('reset_whole_Develop', 'contrast'), 1), (('scope', 'red', 0), 64776),
                 (('pointers', 0, 'post_event_render_revision'), '4'),
                 (('pointers', 6, 'QPA_mouse_route'), False), (('product_development_complete',), True)]
        for path, value in edits:
            data = report_fixture(); cursor = data
            for key in path[:-1]: cursor = cursor[key]
            cursor[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError): validate_report(data, 'gpu', 'cocoa')

    def test_every_canonical_range_field_is_required(self):
        for key in report_fixture()['active_whole_Develop']['colorMask']:
            data = report_fixture(); del data['active_whole_Develop']['colorMask'][key]
            with self.subTest(key=key), self.assertRaises((ValueError, KeyError)): validate_report(data, 'gpu', 'cocoa')
        for value in (True,'1',float('nan'),0):
            data = report_fixture(); data['active_whole_Develop']['colorMask']['hueRange'] = value
            with self.subTest(value=value), self.assertRaises(ValueError): validate_report(data, 'gpu', 'cocoa')

    def test_both_cpu_and_gpu_require_real_cache_reuse(self):
        for mode in ('cpu', 'gpu'):
            data = report_fixture(mode); data['colorMask_cache_hit_delta'] = 0
            with self.assertRaises(ValueError): validate_report(data, mode, 'cocoa')
        for mode in ('cpu', 'gpu'):
            data = report_fixture(mode); data['colorMask_processing_policy'] = 'uncached-cpu-reference-upload'
            with self.assertRaises(ValueError): validate_report(data, mode, 'cocoa')
            data = report_fixture(mode); data['colorMask_cache_required'] = False
            with self.assertRaises(ValueError): validate_report(data, mode, 'cocoa')

    def test_false_upload_and_stale_mask_diagnostics_are_rejected(self):
        edits = [(('gpu_working_output', 'producer'), 'cpu-reference-upload'),
                 (('gpu_working_output', 'color_mask'), 'cpu-reference'),
                 (('gpu_working_output', 'spatial_denoise'), 'cpu-reference'),
                 (('gpu_working_output', 'numeric_source_policy'), 'color-range-mask-cpu-stage'),
                 (('gpu_working_output', 'source_cache_key'), '49'),
                 (('color_stages','color_mask','settings','hueRange'), 90),
                 (('color_stages', 'color_mask', 'monitor_icc'), True),
                 (('color_stages', 'entries', 2, 'pixels'), 1)]
        for path, value in edits:
            manifest, stages = diagnostic_fixture(); cursor = stages
            for key in path[:-1]: cursor = cursor[key]
            cursor[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError): validate_diagnostic(report_fixture(), manifest, stages, 'gpu')

    def test_captures_follow_invert_and_final_reset_in_nine_event_flow(self):
        data=report_fixture();data['DPR']=1.5
        admission=dict(accepted=True,phase=0,input_started=False,resize_count=0,
            initial_logical_size=[1180,780],first_observed_physical_size=[1770,1170],
            logical_width=1180,logical_height=780,physical_width=1770,physical_height=1170,
            DPR=1.5,presented_frames=3,source_key='50',render_revision='30',GUI_thread_id='73',at_ms=5)
        data['window_capture_admission']=admission
        data['phase_checkpoints']=[dict(phase=i,at_ms=100*i)for i in range(16)]
        data['stable_presented_warmups']=[dict(phase=i,presented_frames=3,signature=['50','30',1180,780,307,211])for i in range(16)]
        for pointer,at in zip(data['pointers'],[10,20,30,40,50,110,310,410,880]):pointer['at_ms']=at
        data['window_captures']=[]
        for role,phase in zip(['active','undo','redo','reset'],[4,7,8,9]):
            data['window_captures'].append(dict(role=role,phase=phase,
                path=data['source_path'].removesuffix('.source.png')+'.'+role+'.png',
                logical_width=1180,logical_height=780,physical_width=1770,physical_height=1170,
                DPR=1.5,image_bits_per_pixel=32,source_key='50',render_revision='30',GUI_thread_id='73',presented_frames=3,at_ms=100*phase+50))
        self.assertTrue(validate_window_evidence(data))
        for index,at in [(7,470),(8,840)]:
            bad=deepcopy(data);bad['pointers'][index]['at_ms']=at
            with self.subTest(index=index),self.assertRaises(ValueError):validate_window_evidence(bad)

    def test_occupied_evidence_namespace_is_preserved(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); binary = root / 'app'; binary.write_bytes(b'owned executable')
            report = root / 'check.json'; sibling = root / 'check.native.json'; sibling.write_bytes(b'original evidence')
            with patch('ValidateColorMask.subprocess.run') as run:
                with self.assertRaises(ValueError): validate_color_mask(binary, {}, root / 'home', report, 'cpu')
                run.assert_not_called()
            self.assertEqual(sibling.read_bytes(), b'original evidence'); self.assertFalse(report.exists())


if __name__ == '__main__':
    unittest.main()
