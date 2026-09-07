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


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument('executable', type=Path)
    parser.add_argument('raw', type=Path)
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
            if mode == 'gpu':
                expected = 'Direct3D 11' if sys.platform == 'win32' else 'Metal'
                ok = ok and expected in data.get('backend', '')
            results.append({'mode': mode, 'passed': bool(ok), 'returncode': code,
                            'backend': data.get('backend'), 'source_commit': data.get('source_commit')})
            if not ok:
                print(log.read_text(encoding='utf-8', errors='replace')[-20000:], flush=True)
                print(json.dumps(data, indent=2), flush=True)
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
