#!/usr/bin/env python3
"""Exercise real shader dependency rebuilds without compiling the application."""
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
import uuid


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cmake', required=True)
    parser.add_argument('--qsb', required=True)
    parser.add_argument('--generator', required=True)
    parser.add_argument('--make-program', default='')
    parser.add_argument('--report-directory', required=True, type=Path)
    parser.add_argument('--results-file', required=True, type=Path)
    args = parser.parse_args()
    args.report_directory.mkdir(parents=True, exist_ok=True)
    attempt = args.report_directory/('attempt-'+uuid.uuid4().hex)
    attempt.mkdir()
    repository = Path(__file__).resolve().parents[1]
    result = {'passed': False, 'at': datetime.now(timezone.utc).isoformat(),
              'attempt': str(attempt), 'generator': args.generator,
              'commands': [], 'checks': {}, 'original_reports_preserved': True,
              'application_or_GUI_exercised': False}

    def command(label, argv, expected=0):
        start = time.monotonic()
        completed = subprocess.run([str(v) for v in argv], stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, encoding='utf-8', errors='replace')
        log = attempt/(label+'.log')
        log.write_text(completed.stdout, encoding='utf-8')
        result['commands'].append({'label': label, 'argv': [str(v) for v in argv],
            'returncode': completed.returncode, 'expected_returncode': expected,
            'elapsed_ms': round((time.monotonic()-start)*1000),
            'log': str(log), 'log_sha256': digest(log)})
        if completed.returncode != expected:
            raise RuntimeError(f'{label}: exit {completed.returncode}; see {log}')
        return completed.stdout

    try:
        version = command('qsb-version', [args.qsb, '--version'])
        match = re.search(r'\b(\d+\.\d+\.\d+)\b', version)
        if not match:
            raise RuntimeError('qsb did not report its version')
        result['qsb_version'] = match[1]
        source = attempt/'source 空格'
        build = attempt/'build with spaces'
        (source/'shaders').mkdir(parents=True)
        for name in ['cmake/QsbDepfile.cmake', 'tools/FixQsbDepfile.py']:
            target = source/name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(repository/name, target)
            if digest(target) != digest(repository/name):
                raise RuntimeError('Scratch helper differs from the production source')
        result['copied_helper_sha256'] = {name: digest(source/name) for name in
            ['cmake/QsbDepfile.cmake', 'tools/FixQsbDepfile.py']}
        include = source/'shaders/include #$ tone.glsl'
        include.write_text('float fixtureValue() { return 0.125; }\n', encoding='utf-8')
        shader = source/'shaders/fixture.comp'
        shader.write_text('''#version 440
#extension GL_GOOGLE_include_directive : require
#include "include #$ tone.glsl"
layout(local_size_x = 1) in;
layout(std430, binding = 0) buffer Output { float value; } destination;
void main() { destination.value = fixtureValue(); }
''', encoding='utf-8')
        cmake = '''cmake_minimum_required(VERSION 3.24)
project(QsbDependencyRegression NONE)
set(Qt6_VERSION "@VERSION@")
set(Python3_EXECUTABLE [=[@PYTHON@]=])
set(qsb [=[@QSB@]=])
set(output "${CMAKE_BINARY_DIR}/.qsb/fixture.comp.qsb")
file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/.qsb")
add_custom_command(OUTPUT "${output}"
    COMMAND "${qsb}" --glsl "430,310es" --hlsl 50 --msl 12
        -o "${output}" --depfile "${output}.d" "${CMAKE_SOURCE_DIR}/shaders/fixture.comp"
    DEPENDS "${CMAKE_SOURCE_DIR}/shaders/fixture.comp"
    DEPFILE "${output}.d" VERBATIM COMMENT "Bake depfile regression fixture")
include(cmake/QsbDepfile.cmake)
jixellight_escape_qsb_depfiles(shaders/fixture.comp)
add_custom_target(shader_only DEPENDS "${output}")
'''.replace('@VERSION@', match[1]).replace('@PYTHON@', Path(sys.executable).as_posix()).replace('@QSB@', Path(args.qsb).as_posix())
        (source/'CMakeLists.txt').write_text(cmake, encoding='utf-8')
        configure = [args.cmake, '-S', source, '-B', build, '-G', args.generator]
        if args.make_program:
            configure.append('-DCMAKE_MAKE_PROGRAM='+args.make_program)
        command('configure', configure)
        build_command = [args.cmake, '--build', build, '--target', 'shader_only',
                         '--parallel', '1', '--config', 'Release']
        bundle = build/'.qsb/fixture.comp.qsb'
        command('first-build', build_command)
        initial = (digest(bundle), bundle.stat().st_mtime_ns)
        command('unchanged-build', build_command)
        if (digest(bundle), bundle.stat().st_mtime_ns) != initial:
            raise RuntimeError('Unchanged shader regenerated on the second build')
        result['checks']['unchanged_build_is_noop'] = True
        # Makefile generators compare whole seconds on some filesystems.
        # Wait before each scratch-input mutation, never touch repository files.
        time.sleep(1.05)
        shader.write_text(shader.read_text(encoding='utf-8')+'// source changed\n', encoding='utf-8')
        command('source-changed', build_command)
        after_source = (digest(bundle), bundle.stat().st_mtime_ns)
        if after_source[0] != initial[0] or after_source[1] == initial[1]:
            raise RuntimeError('Main shader dependency was missed or shader bytes changed for a comment')
        command('source-changed-noop', build_command)
        if (digest(bundle), bundle.stat().st_mtime_ns) != after_source:
            raise RuntimeError('Main source update did not return to a no-op build')
        result['checks']['main_source_change_rebuilds_then_noop'] = True
        time.sleep(1.05)
        include.write_text('float fixtureValue() { return 0.375; }\n', encoding='utf-8')
        command('include-changed', build_command)
        after_include = (digest(bundle), bundle.stat().st_mtime_ns)
        if after_include[0] == after_source[0] or after_include[1] == after_source[1]:
            raise RuntimeError('Direct include dependency with space/hash/dollar was missed')
        command('include-changed-noop', build_command)
        if (digest(bundle), bundle.stat().st_mtime_ns) != after_include:
            raise RuntimeError('Include update did not return to a no-op build')
        result['checks']['direct_include_change_rebuilds_then_noop'] = True
        result['helper_workaround_active'] = match[1] == '6.8.3'
        if result['helper_workaround_active']:
            time.sleep(1.05)
            os.utime(source/'tools/FixQsbDepfile.py', None)
            command('helper-changed', build_command)
            after_helper = (digest(bundle), bundle.stat().st_mtime_ns)
            if after_helper[0] != after_include[0] or after_helper[1] == after_include[1]:
                raise RuntimeError('Postprocessor dependency was missed or changed shader bytes')
            command('helper-changed-noop', build_command)
            if (digest(bundle), bundle.stat().st_mtime_ns) != after_helper:
                raise RuntimeError('Postprocessor update did not return to a no-op build')
            result['checks']['helper_change_rebuilds_same_bytes_then_noop'] = True
        target = '/build dir/result.qsb'
        original_source = '/source 空格/fixture.comp'
        continuation = ' \\\n  '
        raw = (target+':'+continuation+original_source).encode('utf-8')
        rejected = {
            'empty': b'', 'foreign-target': b'/else.qsb: \\\n  '+original_source.encode('utf-8'),
            'foreign-source': (target+':'+continuation+'/else.comp').encode('utf-8'),
            'wrong-separator': (target+': '+original_source).encode('utf-8'),
            'unexpected-newline': raw+b'\n/other.comp',
            'empty-dependency': raw+continuation.encode(),
            'relative-dependency': raw+(continuation+'relative.glsl').encode(),
            'nul-path': raw+(continuation+'/bad\0name.glsl').encode(),
            'invalid-utf8': raw+continuation.encode()+b'/bad\xff.glsl',
            'unsupported-posix-backslash': raw+(continuation+'/bad\\name.glsl').encode(),
            'already-escaped': (target.replace(' ', '\\ ')+':'+continuation+original_source.replace(' ', '\\ ')+'\n').encode('utf-8'),
            'suffix-on-source': raw+b'suffix',
        }
        for name, data in rejected.items():
            depfile = attempt/(name+'.d')
            depfile.write_bytes(data)
            command('reject-'+name, [sys.executable, repository/'tools/FixQsbDepfile.py',
                    depfile, target, original_source], expected=1)
            if depfile.read_bytes() != data or list(attempt.glob('.qsb-depfile-*')):
                raise RuntimeError(f'{name}: failed normalization changed original bytes or left a temporary file')
        result['negative_controls'] = len(rejected)
        result['checks']['unknown_input_preserves_original_bytes'] = True
        for name, expected in result['copied_helper_sha256'].items():
            if digest(repository/name) != expected:
                raise RuntimeError('Production helper source changed during the test')
        result['checks']['production_helpers_unchanged'] = True
        # Keep the actual final shader bundle in the uploaded evidence tree.
        # The build output lives in a hidden .qsb directory, which artifact
        # uploaders can omit even when the parent directory is included.
        evidence_bundle = attempt/'fixture-final.qsb'
        shutil.copyfile(bundle, evidence_bundle)
        expected_bundle_sha = after_helper[0] if result['helper_workaround_active'] else after_include[0]
        if evidence_bundle.stat().st_size <= 0 or digest(evidence_bundle) != expected_bundle_sha:
            raise RuntimeError('Final shader evidence does not match the built bundle')
        result['final_bundle'] = {'file': evidence_bundle.name,
                                  'bytes': evidence_bundle.stat().st_size,
                                  'sha256': expected_bundle_sha}
        result['passed'] = True
    except Exception as error:
        result['error'] = f'{type(error).__name__}: {error}'
    report = attempt/'report.json'
    report.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    args.results_file.parent.mkdir(parents=True, exist_ok=True)
    with args.results_file.open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(result, ensure_ascii=False)+'\n')
    print(json.dumps({'passed': result['passed'], 'report': str(report), 'checks': result['checks']}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
