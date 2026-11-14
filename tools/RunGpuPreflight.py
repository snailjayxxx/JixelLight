#!/usr/bin/env python3
"""Run the existing real-Sony/GPU gates early and retain their original evidence."""
import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import xml.etree.ElementTree as ET

TEST_NAMES = {'sony-real-files', 'gpu-correctness', 'native-brush-luminance-gpu'}
GPU_REPORTS = {'gpu-results.txt': 85, 'native-brush-luminance-gpu-results.txt': 19}
REPORT_SUFFIXES = {'.json', '.csv', '.txt', '.log', '.xml'}


def sha(path):
    h = hashlib.sha256()
    with path.open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def write_new(path, value):
    with path.open('x', encoding='utf-8') as f:
        json.dump(value, f, indent=2, ensure_ascii=False)
        f.write('\n')


def inspect_evidence(directory):
    root = ET.parse(directory / 'ctest.xml').getroot()
    cases = root.findall('testcase')
    if (root.tag != 'testsuite' or len(cases) != len(TEST_NAMES)
            or {c.get('name') for c in cases} != TEST_NAMES
            or any(c.get('status') != 'run' or c.find('failure') is not None
                   or c.find('error') is not None or c.find('skipped') is not None for c in cases)):
        raise ValueError('All three required CTest cases must run successfully without skips')
    qt_counts = {}
    for name, minimum in GPU_REPORTS.items():
        gpu = (directory / 'original' / name).read_text(encoding='utf-8', errors='replace')
        totals = re.findall(r'Totals: (\d+) passed, (\d+) failed, (\d+) skipped, (\d+) blacklisted', gpu)
        if len(totals) != 1 or int(totals[0][0]) < minimum or list(map(int, totals[0][1:])) != [0, 0, 0]:
            raise ValueError('Complete GPU suite has missing, failed or skipped Qt results: ' + name)
        qt_counts[name] = int(totals[0][0])
    summary = json.loads((directory / 'original/sony-real-reports/summary.json').read_text(encoding='utf-8'))
    if summary.get('passed') is not True:
        raise ValueError('Actual real-Sony fixture/profile validation did not pass')
    profile = directory / 'original/sony-real-reports/dataset/empirical.jlook.json'
    if not profile.is_file() or not profile.stat().st_size:
        raise ValueError('Fitted real-Sony profile is missing')
    return {'ctest_cases': sorted(TEST_NAMES), 'qt_passed': qt_counts['gpu-results.txt'],
            'native_brush_luminance_qt_passed': qt_counts['native-brush-luminance-gpu-results.txt'],
            'qt_failed_skipped_blacklisted': [0, 0, 0], 'sony_profile_sha256': sha(profile)}


def run_preflight(build, report, source_commit, *, config='', ctest='ctest', environment=None):
    build, report = Path(build).resolve(), Path(report).absolute()
    if not re.fullmatch(r'[0-9a-f]{40}', source_commit):
        raise ValueError('A full checkout commit is required')
    if not (build / 'CTestTestfile.cmake').is_file() or not (build / 'CMakeCache.txt').is_file():
        raise ValueError('Configured CTest build tree is required')
    # This extra invocation must never replace earlier full-run or failed evidence.
    for path in (report, *(build / name for name in GPU_REPORTS), build / 'sony-real-reports'):
        if path.exists() or path.is_symlink():
            raise ValueError('Preflight requires fresh evidence paths: ' + str(path))
    report.mkdir(parents=True, exist_ok=False)
    env = dict(os.environ if environment is None else environment)
    env['JIXELLIGHT_REQUIRE_GPU'] = '1'
    env['JIXELLIGHT_REQUIRE_SONY_REAL'] = '1'
    argv = [ctest, '--test-dir', str(build), '--output-on-failure', '--no-tests=error',
            '--output-junit', str(report / 'ctest.xml'), '-j1', '-R', '^(' + '|'.join(sorted(TEST_NAMES)) + ')$']
    if config:
        argv += ['-C', config]
    result = {'schema': 1, 'passed': False, 'source_commit': source_commit,
              'source_identity_scope': 'checkout; this preflight does not replace full product/package acceptance',
              'started_at': datetime.now(timezone.utc).isoformat(), 'argv': argv,
              'require_gpu': True, 'require_sony_real': True, 'returncode': None,
              'full_ctest_native_and_package_gates_still_required': True,
              'original_reports_preserved': True, 'captures': [], 'checks': {}, 'error': ''}
    write_new(report / 'started.json', result)
    started = time.monotonic()
    try:
        with (report / 'ctest.log').open('xb') as log:
            process = subprocess.run(argv, env=env, stdout=log, stderr=subprocess.STDOUT)
        result['returncode'] = process.returncode
    except Exception as error:
        result['error'] = str(error)
    finally:
        original = report / 'original'
        original.mkdir()
        candidates = [build / name for name in GPU_REPORTS]
        sony = build / 'sony-real-reports'
        if sony.is_dir():
            candidates += sorted(p for p in sony.rglob('*') if p.is_file() and p.suffix.lower() in REPORT_SUFFIXES)
        try:
            for path in candidates:
                if not path.exists():
                    continue
                if path.is_symlink():
                    raise ValueError('Evidence symlinks are not captured: ' + str(path))
                relative = path.relative_to(build)
                target = original / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                with path.open('rb') as source, target.open('xb') as destination:
                    shutil.copyfileobj(source, destination)
                if sha(path) != sha(target):
                    raise ValueError('Original evidence copy differs: ' + str(path))
                result['captures'].append({'path': relative.as_posix(), 'bytes': target.stat().st_size, 'sha256': sha(target)})
            if result['returncode'] != 0:
                raise ValueError('CTest GPU preflight failed: ' + str(result['returncode']))
            result['checks'] = inspect_evidence(report)
            result['passed'] = True
        except Exception as error:
            result['error'] = (result['error'] + '; ' if result['error'] else '') + str(error)
        result['elapsed_ms'] = round((time.monotonic() - started) * 1000)
        for name in ('ctest.log', 'ctest.xml'):
            path = report / name
            if path.is_file():
                result[name + '_sha256'] = sha(path)
        write_new(report / 'result.json', result)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--report', required=True, type=Path)
    parser.add_argument('--source-commit', required=True)
    parser.add_argument('--config', default='')
    args = parser.parse_args()
    try:
        result = run_preflight(args.build, args.report, args.source_commit, config=args.config)
    except (ValueError, OSError) as error:
        print(str(error), file=sys.stderr)
        return 2
    log = args.report / 'ctest.log'
    if log.is_file():
        sys.stdout.buffer.write(log.read_bytes())
        sys.stdout.buffer.flush()
    print(json.dumps({k: result[k] for k in ('passed', 'returncode', 'elapsed_ms', 'checks', 'error')}, ensure_ascii=False))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
