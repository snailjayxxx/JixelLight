"""Reject stale, incomplete or false native dehaze evidence."""
from copy import deepcopy
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ValidateDehaze import DEVELOP_FIELDS, validate_report, validate_diagnostic, validate_dehaze


def report_fixture(mode='gpu', qpa='cocoa'):
    initial = {key: 0 for key in DEVELOP_FIELDS}
    for key in ('masterCurve', 'redCurve', 'greenCurve', 'blueCurve'): initial[key] = [0, .25, .5, .75, 1]
    for key in ('hslHue', 'hslSaturation', 'hslLuminance'): initial[key] = [0]*8
    initial['look'] = {'schema': 1, 'mode': 'off', 'code': '', 'strength': 1, 'parameters': {}}
    initial['geometry'] = {'schema': 1, 'x': 0, 'y': 0, 'width': 1, 'height': 1,
                           'quarterTurns': 0, 'flipHorizontal': False, 'flipVertical': False}
    initial.update(exposure=.2, grain={'schema': 1, 'amount': 17, 'size': 1}, denoise={'schema': 1, 'lightness': 25, 'chroma': 35})
    first = dict(initial, dehaze={'schema': 1, 'amount': 75})
    active = dict(initial, exposure=.3, dehaze={'schema': 1, 'amount': -65})
    return {
        'passed': True, 'error': '', 'phase': 16, 'language': 'en_US' if mode == 'cpu' else 'zh_CN',
        'actual_qpa': qpa, 'actual_Main_App_PhotoCanvas': True, 'controller_on_actual_GUI_thread': True,
        'source_unchanged': True, 'settings_isolated': True, 'physical_device_verified': False,
        'native_float_RAW_decoder': False, 'normal_Root_or_CI_accepted': False, 'product_development_complete': False,
        'GPU_full_readback_fingerprint_only_not_new_perpixel40proof': True, 'GPU_active': mode == 'gpu',
        'actual_backend': 'CPU parallel' if mode == 'cpu' else {'cocoa': 'GPU · Metal', 'windows': 'GPU · Direct3D 11', 'xcb': 'GPU · OpenGL'}[qpa],
        'elapsed_ms': 5000, 'deadline_ms': 60000, 'source_commit': '1234567',
        'initial_whole_Develop': initial, 'first_whole_Develop': first, 'active_whole_Develop': active,
        'reset_whole_Develop': dict(initial, exposure=.3), 'undo_steps_to_first': 2, 'dehaze_cache_hit_delta': 2 if mode=='gpu' else 1, 'dehaze_cache_required': True, 'dehaze_processing_policy': 'cached-gpu-compute' if mode=='gpu' else 'cached-cpu-reference',
        'dehaze_header_text': 'DEHAZE' if mode == 'cpu' else '去雾', 'dehaze_reset_text': 'Reset' if mode == 'cpu' else '重置',
        'staging': {'bit_exact': True, 'complete_pixels': 64777, 'actual_FP32_staging_sha256': 'a'*64, 'expected_FP32_staging_sha256': 'a'*64},
        'scope': {'pixels': 64777, 'bins': 1024, 'four_histograms_exact': True, 'clipping_exact': True,
                  **{key: [64777] + [0]*1023 for key in ('red', 'green', 'blue', 'luma')}},
        'scope_plots': [{'mode': key, 'current': True, 'full_resolution': True, 'pixels': '64777'} for key in ('waveform', 'parade', 'vectorscope')],
        'source_path': '/owned/source.png',
        'pointers': [{'target': key, 'QPA_mouse_route': True, 'source_key': '50', 'post_event_source_key': '50',
                      'render_revision': str(5+i), 'post_event_render_revision': str(6+i), 'GUI_thread_id': '73',
                      'file': '/owned/source.png', 'local_x': 10., 'local_y': 20., 'global_x': 30., 'global_y': 40.}
                     for i, key in enumerate(('positiveAmount', 'negativeAmount', 'dehazeReset'))],
        'export': {'RGBA16_exact': True, 'ICC_srgb': True, 'pixels': 64777},
        'diagnostic': {'request_revision': '30', 'source_key': '50'},
    }


def diagnostic_fixture(mode='gpu'):
    frame = {'available': True, 'pixels': 64777, 'width': 307, 'height': 211,
             'bytes': 1036432, 'nonfinite_rgba': [0, 0, 0, 0], 'pixel_sha256': 'b'*64}
    manifest = {'adjustments': report_fixture(mode)['active_whole_Develop']}
    stages = {'parameter_revision': 30, 'gpu_context': {'source_cache_key': '50'},
              'color_stages': {'available': True, 'backend': 'cpu-reference', 'spatial_dehaze': {
                  'backend': 'cpu-reference', 'effective_dark_radius': 0, 'effective_refine_radius': 0, 'airlight': [.8,.9,1], 'airlight_available': True, 'airlight_scope': 'whole corrected source before viewport/preview resize', 'input': frame,
                  'output': dict(frame, pixel_sha256='c'*64)}, 'spatial_denoise': {'backend': 'cpu-reference', 'output': {'pixel_sha256':'b'*64}}},
              'gpu_working_output': {'available': False} if mode == 'cpu' else {
                  'available': True, 'producer': 'gpu-compute', 'spatial_dehaze': 'gpu-compute', 'spatial_denoise': 'gpu-compute',
                  'pixels':64777, 'bytes':1036432, 'nonfinite_rgba':[0,0,0,0],
                  'numeric_source_policy': 'none', 'monitor_icc': False, 'parameter_revision': 30, 'source_cache_key': '50'}}
    return manifest, stages


class DehazeEvidenceTests(unittest.TestCase):
    def test_all_native_backend_and_language_contexts(self):
        for qpa in ('cocoa', 'windows', 'xcb'):
            for mode in ('cpu', 'gpu'):
                with self.subTest(qpa=qpa, mode=mode):
                    self.assertTrue(validate_report(report_fixture(mode, qpa), mode, qpa, '1234567'))
                    self.assertTrue(validate_diagnostic(report_fixture(mode, qpa), *diagnostic_fixture(mode), mode))

    def test_reject_incomplete_workflow_and_changed_unrelated_edits(self):
        edits = [(('phase',), 15), (('dehaze_header_text',), 'DENOISE'), (('source_commit',), 'unknown'), (('deadline_ms',), 60000.0),
                 (('active_whole_Develop', 'contrast'), 20), (('reset_whole_Develop', 'exposure'), .2),
                 (('active_whole_Develop', 'dehaze', 'amount'), 0), (('dehaze_cache_hit_delta',), -1),
                 (('dehaze_cache_required',), False), (('dehaze_processing_policy',), 'gpu-compute'),
                 (('export', 'RGBA16_exact'), False), (('scope', 'red', 0), 64776),
                 (('pointers', 1, 'post_event_render_revision'), '1'), (('pointers', 2, 'QPA_mouse_route'), False),
                 (('native_float_RAW_decoder',), True), (('product_development_complete',), True)]
        for path, value in edits:
            data = report_fixture(); cursor = data
            for key in path[:-1]: cursor = cursor[key]
            cursor[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError):
                validate_report(data, 'gpu', 'cocoa', '1234567')

    def test_cpu_requires_real_cache_reuse(self):
        for mode in ('cpu','gpu'):
            data=report_fixture(mode);data['dehaze_cache_hit_delta']=0
            with self.assertRaises(ValueError):validate_report(data,mode,'cocoa')

    def test_reject_false_compute_claims_and_stale_frames(self):
        edits = [(('gpu_working_output', 'producer'), 'cpu-reference-upload'),
                 (('gpu_working_output', 'spatial_dehaze'), 'cpu-reference'),
                 (('gpu_working_output', 'numeric_source_policy'), 'dehaze-near-zero-alpha'),
                 (('gpu_working_output', 'monitor_icc'), True), (('gpu_working_output', 'source_cache_key'), '49'),
                 (('gpu_working_output', 'parameter_revision'), 29), (('parameter_revision',), 29),
                 (('color_stages', 'spatial_dehaze', 'airlight'), [0,1,1]),
                 (('color_stages', 'spatial_dehaze', 'airlight'), [float('nan'),1,1]),
                 (('color_stages', 'spatial_dehaze', 'airlight_scope'), 'viewport-only'),
                 (('color_stages', 'spatial_dehaze', 'output', 'pixels'), 1),
                 (('color_stages', 'spatial_dehaze', 'output', 'nonfinite_rgba'), [1, 0, 0, 0]),
                 (('color_stages', 'spatial_dehaze', 'output', 'pixel_sha256'), 'b'*64)]
        for path, value in edits:
            manifest, stages = diagnostic_fixture(); cursor = stages
            for key in path[:-1]: cursor = cursor[key]
            cursor[path[-1]] = value
            with self.subTest(path=path), self.assertRaises(ValueError):
                validate_diagnostic(report_fixture(), manifest, stages, 'gpu')
        manifest, stages = diagnostic_fixture(); manifest['adjustments']['exposure'] = .2
        with self.assertRaises(ValueError): validate_diagnostic(report_fixture(), manifest, stages, 'gpu')

    def test_windows_recorded_path_identity_without_relaxing_source_ownership(self):
        data = report_fixture('cpu', 'windows'); data['source_path'] = r'D:\owned\source.png'
        for pointer in data['pointers']: pointer['file'] = 'D:/owned/source.png'
        self.assertTrue(validate_report(data, 'cpu', 'windows'))
        for path in ('E:/owned/source.png', 'D:/other/source.png', 'D:owned/source.png'):
            bad = deepcopy(data); bad['pointers'][0]['file'] = path
            with self.subTest(path=path), self.assertRaises(ValueError): validate_report(bad, 'cpu', 'windows')

    def test_occupied_namespace_never_launches_or_changes_existing_evidence(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); binary = root/'app'; binary.write_bytes(b'owned-binary')
            report = root/'check.json'; sibling = root/'check.native.json'; sibling.write_bytes(b'original evidence')
            with patch('ValidateDehaze.subprocess.run') as run:
                with self.assertRaises(ValueError): validate_dehaze(binary, {}, root/'home', report, 'cpu')
                run.assert_not_called()
            self.assertEqual(sibling.read_bytes(), b'original evidence'); self.assertFalse(report.exists())

if __name__ == '__main__': unittest.main()
