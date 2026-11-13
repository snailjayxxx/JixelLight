"""Failure propagation and original-evidence preservation for the CI preflight."""
from pathlib import Path
import json
import os
import shutil
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch
sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from RunGpuPreflight import inspect_evidence, run_preflight

COMMIT = 'a' * 40


def tree(root):
    build = root / 'build with spaces'; build.mkdir()
    (build / 'CMakeCache.txt').write_text('owned fixture\n')
    (build / 'CTestTestfile.cmake').write_text('owned fixture\n')
    return build, root / 'preflight original'


def complete(build, report, *, code=0, qt='4 passed, 0 failed, 0 skipped, 0 blacklisted',
             native_qt='8 passed, 0 failed, 0 skipped, 0 blacklisted',
             names=('sony-real-files', 'gpu-correctness', 'native-brush-luminance-gpu')):
    (build / 'gpu-results.txt').write_text('owned GPU fixture\nTotals: ' + qt + ', 9ms\n')
    (build / 'native-brush-luminance-gpu-results.txt').write_text('owned native GPU fixture\nTotals: ' + native_qt + ', 9ms\n')
    sony = build / 'sony-real-reports'; (sony / 'dataset').mkdir(parents=True)
    (sony / 'summary.json').write_text('{"passed":true}\n')
    (sony / 'dataset/empirical.jlook.json').write_text('{"owned":"profile fixture"}\n')
    (sony / 'probe.log').write_bytes(b'original failed/successful probe\r\n')
    # Review photographs must never be copied into uploaded preflight evidence.
    (sony / 'private.ARW').write_bytes(b'owned photo fixture not for upload')
    (report / 'ctest.xml').write_text('<testsuite>' + ''.join('<testcase name="' + n + '" status="run" />' for n in names) + '</testsuite>')
    return subprocess.CompletedProcess([], code)


class GpuPreflightTests(unittest.TestCase):
    def test_success_requires_all_three_gates_and_keeps_originals_before_full_run_overwrites(self):
        with tempfile.TemporaryDirectory() as temp:
            build, report = tree(Path(temp))
            def invoke(argv, **kwargs):
                self.assertEqual(kwargs['env']['JIXELLIGHT_REQUIRE_GPU'], '1')
                self.assertEqual(kwargs['env']['JIXELLIGHT_REQUIRE_SONY_REAL'], '1')
                self.assertIn('--no-tests=error', argv); self.assertIn('Release', argv)
                kwargs['stdout'].write(b'complete original CTest log\n')
                return complete(build, report)
            with patch('RunGpuPreflight.subprocess.run', side_effect=invoke):
                result = run_preflight(build, report, COMMIT, config='Release', environment={})
            self.assertTrue(result['passed']); self.assertTrue(result['full_ctest_native_and_package_gates_still_required'])
            self.assertEqual(result['checks']['qt_passed'], 4)
            self.assertEqual(result['checks']['native_brush_luminance_qt_passed'], 8)
            self.assertEqual(result['checks']['ctest_cases'], ['gpu-correctness', 'native-brush-luminance-gpu', 'sony-real-files'])
            self.assertFalse((report / 'original/sony-real-reports/private.ARW').exists())
            self.assertEqual((report / 'original/sony-real-reports/probe.log').read_bytes(), b'original failed/successful probe\r\n')
            saved = (report / 'original/gpu-results.txt').read_bytes()
            saved_native = (report / 'original/native-brush-luminance-gpu-results.txt').read_bytes()
            (build / 'gpu-results.txt').write_text('later full suite')
            (build / 'native-brush-luminance-gpu-results.txt').write_text('later native suite')
            (build / 'sony-real-reports/dataset/empirical.jlook.json').write_text('later profile')
            self.assertEqual((report / 'original/gpu-results.txt').read_bytes(), saved)
            self.assertEqual((report / 'original/native-brush-luminance-gpu-results.txt').read_bytes(), saved_native)
            self.assertEqual(inspect_evidence(report), result['checks'])
            self.assertFalse(json.loads((report / 'started.json').read_text())['passed'])

    def test_native_failure_is_retained_and_cannot_become_success(self):
        with tempfile.TemporaryDirectory() as temp:
            build, report = tree(Path(temp))
            with patch('RunGpuPreflight.subprocess.run', side_effect=lambda *a, **k: complete(build, report, code=8)):
                result = run_preflight(build, report, COMMIT)
            self.assertFalse(result['passed']); self.assertEqual(result['returncode'], 8)
            self.assertIn('failed: 8', result['error'])
            self.assertTrue((report / 'original/gpu-results.txt').is_file())
            self.assertTrue((report / 'original/native-brush-luminance-gpu-results.txt').is_file())
            self.assertFalse(json.loads((report / 'result.json').read_text())['passed'])

    def test_zero_exit_cannot_hide_skipped_missing_or_failed_evidence(self):
        for fault in ('qt-skip', 'qt-fail', 'missing-test', 'missing-native-test', 'ctest-skip',
                      'native-qt-skip', 'native-qt-fail', 'native-incomplete', 'native-missing',
                      'native-duplicate-totals', 'sony-fail', 'missing-profile'):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as temp:
                build, report = tree(Path(temp))
                def invoke(*args, **kwargs):
                    names = ('gpu-correctness',) if fault == 'missing-test' else ('sony-real-files', 'gpu-correctness') if fault == 'missing-native-test' else ('sony-real-files', 'gpu-correctness', 'native-brush-luminance-gpu')
                    value = complete(build, report, qt='3 passed, 0 failed, 1 skipped, 0 blacklisted' if fault == 'qt-skip' else '3 passed, 1 failed, 0 skipped, 0 blacklisted' if fault == 'qt-fail' else '4 passed, 0 failed, 0 skipped, 0 blacklisted', names=names)
                    native = build / 'native-brush-luminance-gpu-results.txt'
                    if fault == 'native-qt-skip': native.write_text('Totals: 7 passed, 0 failed, 1 skipped, 0 blacklisted\n')
                    if fault == 'native-qt-fail': native.write_text('Totals: 7 passed, 1 failed, 0 skipped, 0 blacklisted\n')
                    if fault == 'native-incomplete': native.write_text('Totals: 7 passed, 0 failed, 0 skipped, 0 blacklisted\n')
                    if fault == 'native-missing': native.unlink()
                    if fault == 'native-duplicate-totals': native.write_text(native.read_text() * 2)
                    if fault == 'ctest-skip':
                        p = report / 'ctest.xml'; p.write_text(p.read_text().replace('status="run"', 'status="notrun"'))
                    if fault == 'sony-fail': (build / 'sony-real-reports/summary.json').write_text('{"passed":false}')
                    if fault == 'missing-profile': (build / 'sony-real-reports/dataset/empirical.jlook.json').unlink()
                    return value
                with patch('RunGpuPreflight.subprocess.run', side_effect=invoke):
                    result = run_preflight(build, report, COMMIT)
                self.assertEqual(result['returncode'], 0); self.assertFalse(result['passed']); self.assertTrue(result['error'])

    def test_occupied_paths_are_refused_without_running_or_overwriting(self):
        for kind in ('report', 'gpu', 'native', 'sony'):
            with self.subTest(kind=kind), tempfile.TemporaryDirectory() as temp:
                build, report = tree(Path(temp)); occupied = {'report': report, 'gpu': build / 'gpu-results.txt', 'native': build / 'native-brush-luminance-gpu-results.txt', 'sony': build / 'sony-real-reports'}[kind]
                occupied.write_bytes(b'original prior attempt')
                with patch('RunGpuPreflight.subprocess.run') as invoke, self.assertRaises(ValueError):
                    run_preflight(build, report, COMMIT)
                invoke.assert_not_called(); self.assertEqual(occupied.read_bytes(), b'original prior attempt')

    def test_launch_error_keeps_failed_attempt_and_original_log(self):
        with tempfile.TemporaryDirectory() as temp:
            build, report = tree(Path(temp))
            with patch('RunGpuPreflight.subprocess.run', side_effect=OSError('owned launch failure')):
                result = run_preflight(build, report, COMMIT)
            self.assertFalse(result['passed']); self.assertIsNone(result['returncode']); self.assertIn('owned launch failure', result['error'])
            self.assertTrue((report / 'ctest.log').is_file()); self.assertTrue((report / 'result.json').is_file())

    def test_real_ctest_with_spaces_produces_supported_junit_and_preserved_reports(self):
        cmake, ctest = shutil.which('cmake'), shutil.which('ctest')
        self.assertTrue(cmake and ctest, 'CMake/CTest are required for the CI wrapper integration fixture')
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp); source = root / 'source with spaces'; source.mkdir(); build = root / 'build with spaces'; report = root / 'original reports'
            (source / 'fixture.py').write_text('''from pathlib import Path
import json,os,sys
assert os.environ['JIXELLIGHT_REQUIRE_GPU']=='1' and os.environ['JIXELLIGHT_REQUIRE_SONY_REAL']=='1'
build=Path(sys.argv[2])
if sys.argv[1]=='sony':
 p=build/'sony-real-reports';(p/'dataset').mkdir(parents=True)
 (p/'summary.json').write_text(json.dumps({'passed':True}))
 (p/'dataset/empirical.jlook.json').write_text('{"owned":"synthetic CTest integration fixture"}')
elif sys.argv[1]=='gpu':(build/'gpu-results.txt').write_text('Totals: 4 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms\\n')
else:(build/'native-brush-luminance-gpu-results.txt').write_text('Totals: 8 passed, 0 failed, 0 skipped, 0 blacklisted, 1ms\\n')
''')
            script = (source / 'fixture.py').as_posix(); python = Path(sys.executable).as_posix()
            (source / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.24)\nproject(OwnedPreflightFixture NONE)\nenable_testing()\n' + ''.join('add_test(NAME ' + name + ' COMMAND [=[' + python + ']=] [=[' + script + ']=] ' + mode + ' "${CMAKE_BINARY_DIR}")\n' for name, mode in [('sony-real-files', 'sony'), ('gpu-correctness', 'gpu'), ('native-brush-luminance-gpu', 'native')]) + 'set_tests_properties(gpu-correctness native-brush-luminance-gpu PROPERTIES DEPENDS sony-real-files)\n')
            configured = subprocess.run([cmake, '-S', str(source), '-B', str(build)], capture_output=True, text=True)
            self.assertEqual(configured.returncode, 0, configured.stdout + configured.stderr)
            result = run_preflight(build, report, COMMIT, config='Release', ctest=ctest)
            self.assertTrue(result['passed'], result); self.assertEqual(result['checks']['ctest_cases'], ['gpu-correctness', 'native-brush-luminance-gpu', 'sony-real-files'])


if __name__ == '__main__': unittest.main()
