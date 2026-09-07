"""Run the deployed desktop application without development Qt lookup paths."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile


def qml_runtime_errors(text: str) -> list[str]:
    """Catch binding failures that do not necessarily change the GUI exit code."""
    markers = ('TypeError:', 'ReferenceError:', 'Unable to assign [undefined]')
    return [line for line in text.splitlines() if any(mark in line for mark in markers)]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('raw', type=Path)
    parser.add_argument('--sony-probe', type=Path)
    parser.add_argument('--reports', type=Path, default=Path('package-validation'))
    args = parser.parse_args()
    executable, raw = args.executable.resolve(), args.raw.resolve()
    if not executable.is_file() or not raw.is_file():
        parser.error('Deployed executable and RAW fixture must exist')
    reports = args.reports.resolve()
    reports.mkdir(parents=True, exist_ok=True)
    environment = os.environ.copy()
    sdk = environment.get('QT_ROOT_DIR', '').replace('\\', '/').lower().rstrip('/')
    for key in ('QT_PLUGIN_PATH', 'QT_QPA_PLATFORM_PLUGIN_PATH', 'QML2_IMPORT_PATH',
                'QML_IMPORT_PATH', 'QT_ROOT_DIR', 'QT_QPA_PLATFORM',
                'DYLD_LIBRARY_PATH', 'DYLD_FRAMEWORK_PATH',
                'JIXELLIGHT_REQUIRE_GPU', 'JIXELLIGHT_FORCE_CPU',
                'QSG_RHI_BACKEND', 'QSG_RHI_PREFER_SOFTWARE_RENDERER'):
        environment.pop(key, None)
    entries = environment.get('PATH', '').split(os.pathsep)
    environment['PATH'] = os.pathsep.join(entry for entry in entries
        if not sdk or not (entry.replace('\\', '/').lower().rstrip('/') == sdk
        or entry.replace('\\', '/').lower().startswith(sdk + '/')))
    results = []
    with tempfile.TemporaryDirectory(prefix='jixellight-package-') as temp:
        for mode in ('gpu', 'cpu'):
            home = Path(temp) / mode
            home.mkdir()
            env = environment.copy()
            for key in ('HOME', 'USERPROFILE', 'XDG_CACHE_HOME', 'XDG_CONFIG_HOME',
                        'XDG_DATA_HOME', 'APPDATA', 'LOCALAPPDATA'):
                env[key] = str(home)
            if mode == 'gpu':
                env['JIXELLIGHT_REQUIRE_GPU'] = '1'
                env['QSG_RHI_BACKEND'] = 'd3d11' if sys.platform == 'win32' else 'metal'
                if sys.platform == 'win32':
                    env['QSG_RHI_PREFER_SOFTWARE_RENDERER'] = '1'
            else:
                env['JIXELLIGHT_FORCE_CPU'] = '1'
            report = reports / (mode + '.json')
            screenshot = reports / (mode + '.png')
            command = [str(executable), '--smoke-report', str(report),
                       '--screenshot', str(screenshot), str(raw)]
            log = reports / (mode + '.log')
            with log.open('w', encoding='utf-8') as stream:
                try:
                    run = subprocess.run(command, cwd=home, env=env, stdout=stream,
                        stderr=subprocess.STDOUT, check=False, timeout=100)
                    code = run.returncode
                except subprocess.TimeoutExpired:
                    code = -1
            try:
                data = json.loads(report.read_text(encoding='utf-8')) if report.exists() else {}
            except (OSError, ValueError):
                data = {}
            ok = (code == 0 and data.get('smoke_passed') is True
                  and data.get('preview_ready') is True and data.get('scope_pixels', 0) > 0
                  and data.get('gpu_active') is (mode == 'gpu')
                  and data.get('screenshot_saved') is True and screenshot.is_file())
            ok = (ok and data.get('look_validation_required') is True
                  and data.get('look', {}).get('code') == 'FL'
                  and data.get('look', {}).get('active') is True
                  and bool(data.get('reference', {}).get('previewPixelSha256')))
            binding_errors = qml_runtime_errors(log.read_text(encoding='utf-8', errors='replace'))
            ok = ok and not binding_errors
            if mode == 'gpu':
                expected = 'Direct3D 11' if sys.platform == 'win32' else 'Metal'
                ok = ok and expected in data.get('backend', '')
            results.append({'mode': mode, 'passed': bool(ok), 'returncode': code,
                            'backend': data.get('backend'), 'source_commit': data.get('source_commit'),
                            'build_version': data.get('build_version'),
                            'qml_runtime_error_count': len(binding_errors),
                            'look_code': data.get('look', {}).get('code'),
                            'look_active': data.get('look', {}).get('active'),
                            'reference_kind': data.get('reference', {}).get('kind'),
                            'reference_pixel_sha256': data.get('reference', {}).get('previewPixelSha256')})
            if not ok:
                print(log.read_text(encoding='utf-8', errors='replace')[-20000:], flush=True)
                print(json.dumps(data, indent=2), flush=True)
    sony_check = None
    if args.sony_probe:
        probe = args.sony_probe.resolve()
        fixtures = Path(environment.get('JIXELLIGHT_SONY_FIXTURES', '')).resolve()
        env = environment.copy()
        with tempfile.TemporaryDirectory(prefix='jixellight-sony-package-') as temporary:
            for key in ('HOME', 'USERPROFILE', 'XDG_CACHE_HOME', 'XDG_CONFIG_HOME',
                        'XDG_DATA_HOME', 'APPDATA', 'LOCALAPPDATA'):
                env[key] = temporary
            sony_reports = reports / 'sony-native'
            sony_reports.mkdir(exist_ok=True)
            with (sony_reports / 'probe.log').open('w', encoding='utf-8') as stream:
                try:
                    run = subprocess.run([str(probe), str(fixtures/'sony_a7_iv_07.arw'),
                        str(fixtures/'sony_a7_iv_07.jpg'), str(sony_reports)],
                        env=env, cwd=temporary, stdout=stream, stderr=subprocess.STDOUT, timeout=180)
                    code = run.returncode
                except (OSError, subprocess.TimeoutExpired):
                    code = -1
            data = {}
            try:
                data = json.loads((sony_reports/'report.json').read_text(encoding='utf-8'))
            except (OSError, ValueError):
                pass
            for photo in sony_reports.glob('*.png'):
                photo.unlink()  # Do not publish the review photographer's images.
            ok = (code == 0 and data.get('pairMetadataMatches') is True
                  and data.get('automaticReference', {}).get('kind') == 'paired-jpeg'
                  and data.get('decoderModel') == 'ILCE-7M4'
                  and data.get('fitAccepted') is True
                  and data.get('fit', {}).get('heldoutRmseAfter', 1) < .05)
            sony_check = {'mode': 'sony-native-cli', 'passed': bool(ok), 'returncode': code,
                          'probe_sha256': hashlib.sha256(probe.read_bytes()).hexdigest(),
                          'source_commit': data.get('commit'), 'sdk_paths_removed': True,
                          'raw_sha256': data.get('rawSha256'), 'jpeg_sha256': data.get('jpegSha256')}
            results.append(sony_check)
    manifest = {'executable': executable.name,
                'executable_sha256': hashlib.sha256(executable.read_bytes()).hexdigest(),
                'raw_fixture_sha256': hashlib.sha256(raw.read_bytes()).hexdigest(),
                'sdk_paths_removed': True, 'checks': results,
                'passed': all(result['passed'] for result in results)}
    (reports / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    print(json.dumps(manifest, indent=2), flush=True)
    return 0 if manifest['passed'] else 1

if __name__ == '__main__':
    sys.exit(main())
