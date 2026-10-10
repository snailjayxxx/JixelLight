"""Verify the real GUI diagnostic path and its frozen, pre-monitor ZIP snapshot."""
from __future__ import annotations
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import zipfile


def validate_diagnostic(executable: Path, environment: dict, cwd: Path,
                        reports: Path, mode: str) -> dict:
    reports.mkdir(parents=True, exist_ok=True)
    report = reports / f'diagnostic-{mode}.json'
    log = reports / f'diagnostic-{mode}.log'
    report.unlink(missing_ok=True)
    result = {'mode': f'{mode}-diagnostic', 'passed': False}
    try:
        with log.open('w', encoding='utf-8') as stream:
            run = subprocess.run([str(executable.resolve()), '--diagnostic-smoke-report', str(report.resolve())],
                cwd=cwd, env=environment, stdout=stream, stderr=subprocess.STDOUT, timeout=25, check=False)
        data = json.loads(report.read_text(encoding='utf-8'))
        result.update(returncode=run.returncode, source_commit=data.get('source_commit'), elapsed_ms=data.get('elapsed_ms'))
        assert run.returncode == 0 and data['passed'] is True and data['elapsed_ms'] < 15000, data
        errors = [line for line in log.read_text(encoding='utf-8', errors='replace').splitlines()
                  if any(marker in line for marker in ('TypeError:', 'ReferenceError:', 'Unable to assign [undefined]'))]
        result['qml_runtime_error_count'] = len(errors)
        assert not errors, errors
        bundle = Path(data['bundle'])
        shutil.copyfile(bundle, reports / f'diagnostic-{mode}.zip')
        with zipfile.ZipFile(bundle) as archive:
            assert archive.testzip() is None
            stages = json.loads(archive.read('stage_outputs.json'))
            manifest = json.loads(archive.read('manifest.json'))
            performance = json.loads(archive.read('performance.json'))
            assert archive.read('current_preview.png').startswith(b'\x89PNG\r\n\x1a\n')
        assert manifest['stage_outputs'] == stages and manifest['adjustments'] == data['adjustments']
        assert manifest['git_commit'] == data['source_commit'] and stages['engine'] == data['engine']
        assert stages['parameter_revision'] == data['parameter_revision']
        assert performance['values']['controller_state']['requested_revision'] == data['parameter_revision']
        assert stages['cpu_srgb_output']['available'] is True
        gpu = stages['gpu_working_output']
        assert int(gpu['request_id']) > 0
        if mode == 'gpu':
            assert data['gpu_active'] is True and gpu['available'] is True, gpu
            assert gpu['parameter_revision'] == data['parameter_revision']
            assert gpu['source_cache_key'] == data['source_cache_key']
            assert (gpu['width'], gpu['height']) == (data['width'], data['height'])
            assert gpu['bytes'] == gpu['width'] * gpu['height'] * 16 <= 64 * 1024 * 1024
            assert re.fullmatch(r'[0-9a-f]{64}', gpu['pixel_sha256'])
            assert gpu['nonfinite_rgba'] == [0, 0, 0, 0] and gpu['ranges_rgba'][3] == [1, 1]
            assert gpu['monitor_icc'] is False and gpu['output_space'] == 'srgb'
            assert gpu['producer'] in ('gpu-compute', 'cpu-reference-upload')
            reference = stages['gpu_context']['cpu_reference']
            assert reference['available'] is True
            if gpu['producer'] == 'cpu-reference-upload':
                assert gpu['pixel_sha256'] == reference['pixel_sha256']
            result.update(producer=gpu['producer'], pixel_sha256=gpu['pixel_sha256'], backend=gpu['backend'])
        else:
            assert data['gpu_active'] is False and gpu['available'] is False and gpu['error']
            assert 'pixel_sha256' not in gpu
        result['passed'] = True
        # The copy in reports is the retained evidence; remove only this owned ZIP.
        if bundle.name.startswith('JixelLight_Diagnostic_'):
            bundle.unlink()
    except (OSError, ValueError, KeyError, AssertionError, subprocess.TimeoutExpired, zipfile.BadZipFile) as error:
        result['error'] = str(error)
    (reports / f'diagnostic-{mode}-check.json').write_text(json.dumps(result, indent=2), encoding='utf-8')
    return result


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('--mode', choices=('gpu', 'cpu'), required=True)
    parser.add_argument('--reports', type=Path, default=Path('diagnostic-validation'))
    args = parser.parse_args()
    environment = os.environ.copy()
    environment.pop('JIXELLIGHT_FORCE_CPU' if args.mode == 'gpu' else 'JIXELLIGHT_REQUIRE_GPU', None)
    environment['JIXELLIGHT_REQUIRE_GPU' if args.mode == 'gpu' else 'JIXELLIGHT_FORCE_CPU'] = '1'
    result = validate_diagnostic(args.executable, environment, Path.cwd(), args.reports.resolve(), args.mode)
    print(json.dumps(result, indent=2), flush=True)
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
