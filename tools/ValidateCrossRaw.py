"""Read-only cross-format RAW/compute/PNG conformance; no camera calibration claim."""
import argparse
import hashlib
import json
import os
import re
import struct
from pathlib import Path
import shutil
import subprocess
import tempfile
from FetchCrossRawFixtures import read_manifest, file_sha
from ValidateCli import png16_pixels


def require(value, message):
    if not value:
        raise ValueError(message)


def validate_report(data, spec, expected_commit=None):
    require(data['schema'] == 1 and data['passed'] is True and data['error'] == '', 'native result')
    require(data['engine'] == 'jixellight-linear-v5-base2-look4' and data['GPU_required'] is True
            and data['backend'] != 'unavailable', 'real compute/engine')
    require(data['camera_color_calibration_accepted'] is False and data['native_float_RAW_decoder'] is False
            and data['photographs_published'] is False, 'bounded evidence claims')
    require(re.fullmatch(r'[0-9a-f]{7,40}', data['source_commit']) and
            (expected_commit is None or expected_commit.startswith(data['source_commit'])), 'exact tested source commit')
    require(len(data['cases']) == len(spec['items']) == 3, 'all three camera formats')
    for case, pinned in zip(data['cases'], spec['items']):
        require(case['id'] == pinned['id'] and case['source_sha256'] == pinned['sha256'], 'source identity/order')
        require(pinned['model'].lower() in case['model'].lower() and case['decoded_format'] == 'RGBA64'
                and case['linear_working_space'] == 'Linear ProPhoto RGB', 'actual camera/decoder format')
        require(case['source_alpha_exact'] is True and case['source_unchanged'] is True
                and case['width']*case['height'] > 1_000_000, 'full opaque read-only RAW')
        require(re.fullmatch(r'[0-9a-f]{64}', case['decoded_pixels_sha256']), 'complete decoded pixels identity')
        audit = case['sensor_audit']
        require(audit['available'] is True and audit['cfa_kind'] == pinned['cfa']
                and audit['rgb_demosaiced'] is False and audit['black_subtracted'] is False, 'unpacked CFA evidence')
        expected_label = 'X-Trans (LibRaw quality 3)' if pinned['cfa'] == 'xtrans_6x6' else 'AHD'
        require(case['demosaic'] == expected_label, 'CFA-specific demosaic diagnostic')
        require((case['sample_width'], case['sample_height']) == (128, 96)
                and case['linear_sample'] == pinned['id'] + '.linear.rgba16', 'bounded exact sample')
        require(len(case['renders']) == 2, 'zero/+0.75EV coverage')
        for render, ev, suffix in zip(case['renders'], (0, .75), ('.zero', '.ev075')):
            require(all(re.fullmatch(r'[0-9a-f]{64}', render[key]) for key in
                        ('CPU_pixels_sha256', 'GPU_pixels_sha256', 'ICC_sha256')), 'render/profile identities')
            require(type(render['exposure']) in (int, float) and render['exposure'] == ev
                    and type(render['raw_base_exposure_stops']) in (int, float) and render['raw_base_exposure_stops'] == 0,
                    'explicit user/base exposure')
            require(render['actual_compute'] is True and type(render['maximum_GPU_error_U16']) is int
                    and 0 <= render['maximum_GPU_error_U16'] <= 40, 'unchanged compute tolerance')
            require(render['CPU_reference'] == pinned['id'] + suffix + '.cpu.rgba16'
                    and render['GPU_readback'] == pinned['id'] + suffix + '.gpu.rgba16'
                    and render['PNG16'] == pinned['id'] + suffix + '.png', 'owned output names')
        require(case['renders'][0]['CPU_pixels_sha256'] != case['renders'][1]['CPU_pixels_sha256'], 'exposure changes actual pixels')


def verify_pixels(data, root):
    proofs = []
    for case in data['cases']:
        require((root / case['linear_sample']).stat().st_size == 128*96*8, 'complete linear sample')
        for render in case['renders']:
            width, height, color, pixels, icc = png16_pixels(root / render['PNG16'])
            expected = (root / render['CPU_reference']).read_bytes()
            gpu = (root / render['GPU_readback']).read_bytes()
            require(len(gpu) == len(expected) == 128*96*8, 'complete CPU/GPU samples')
            maximum = 0
            for a, b in zip(struct.iter_unpack('<4H', gpu), struct.iter_unpack('<4H', expected)):
                require(a[3] == b[3], 'independent exact GPU alpha')
                maximum = max(maximum, *(abs(a[i]-b[i]) for i in range(3)))
            require(maximum == render['maximum_GPU_error_U16'] and maximum <= 40
                    and hashlib.sha256(gpu).hexdigest() == render['GPU_pixels_sha256'], 'independent every-pixel GPU tolerance')
            # PNG stores U16 channels big endian; the native reference files
            # explicitly store little endian words on every supported host.
            network = bytearray(len(expected))
            network[0::2], network[1::2] = expected[1::2], expected[0::2]
            require((width, height, color) == (128, 96, 6) and pixels == network, 'independent complete PNG16 rows')
            require(hashlib.sha256(expected).hexdigest() == render['CPU_pixels_sha256']
                    and hashlib.sha256(icc).hexdigest() == render['ICC_sha256'], 'PNG pixels/profile identities')
            require(all(pixels[index:index+2] == b'\xff\xff' for index in range(6, len(pixels), 8)), 'every exported alpha')
            proofs.append(dict(id=case['id'], exposure=render['exposure'], rows=height,
                               PNG_big_endian_pixels_sha256=hashlib.sha256(pixels).hexdigest(),
                               CPU_little_endian_pixels_sha256=hashlib.sha256(expected).hexdigest(), ICC_sha256=hashlib.sha256(icc).hexdigest()))
    return proofs


def run(probe, manifest, fixtures, report, expected_commit=None):
    report = Path(report).resolve()
    report.parent.mkdir(parents=True, exist_ok=True)
    require(not any(report.parent.glob(report.stem + '.*')) and not any(report.parent.glob(report.name + '.*'))
            and not report.exists() and not report.is_symlink(), 'occupied evidence namespace; unchanged')
    # Reserve before launching the native process.
    with report.with_name(report.name + '.reservation').open('x') as stream:
        stream.write('cross-raw-evidence-v1\n')
    if str(fixtures) == '@env':
        require(bool(os.environ.get('JIXELLIGHT_CROSS_RAW_FIXTURES')), 'JIXELLIGHT_CROSS_RAW_FIXTURES is required')
        fixtures = os.environ['JIXELLIGHT_CROSS_RAW_FIXTURES']
    spec = read_manifest(manifest)
    probe, fixtures = Path(probe).resolve(), Path(fixtures).resolve()
    before = dict(binary=file_sha(probe), manifest=file_sha(Path(manifest)),
                  sources={p['file']: file_sha(fixtures / p['file']) for p in spec['items']})
    require(before['sources'] == {p['file']: p['sha256'] for p in spec['items']}, 'pinned original RAW bytes')
    private = Path(tempfile.mkdtemp(prefix='jixellight-cross-raw-'))
    output = private / 'native'
    process = subprocess.run([str(probe), str(Path(manifest).resolve()), str(fixtures), str(output)],
                             capture_output=True, text=True, timeout=180)
    for label, content in [('stdout', process.stdout), ('stderr', process.stderr)]:
        with report.with_name(report.stem + '.native.' + label + '.txt').open('x') as stream:
            stream.write(content)
    native_file = output / 'report.json'
    if native_file.exists():
        with report.with_name(report.stem + '.native.json').open('xb') as stream:
            stream.write(native_file.read_bytes())
    try:
        require(process.returncode == 0, 'native RAW conformance failed; private output retained: ' + str(private))
        data = json.loads(native_file.read_text())
        require(data['manifest_sha256'] == before['manifest'], 'frozen native fixture manifest')
        validate_report(data, spec, expected_commit)
        proofs = verify_pixels(data, output)
        require(file_sha(probe) == before['binary'] and file_sha(Path(manifest)) == before['manifest'], 'binary/manifest unchanged')
        require(all(file_sha(fixtures / p['file']) == before['sources'][p['file']] for p in spec['items']), 'all original RAWs unchanged')
    except Exception as error:
        result = dict(passed=False, error=str(error), private_output=str(private), native_returncode=process.returncode)
        with report.open('x') as stream:
            json.dump(result, stream, indent=2)
        raise
    result = dict(passed=True, source_commit=data['source_commit'], backend=data['backend'], report=data,
                  independent_PNG_proofs=proofs, decoded_PNG_rows=sum(p['rows'] for p in proofs),
                  binary_sha256=before['binary'], source_hashes=before['sources'], source_unchanged=True,
                  camera_color_calibration_accepted=False, native_float_RAW_decoder=False, photographs_published=False)
    with report.open('x') as stream:
        json.dump(result, stream, indent=2)
    shutil.rmtree(private)
    print(json.dumps(dict(passed=True, source_commit=data['source_commit'], cameras=3, PNG_rows=result['decoded_PNG_rows'])))
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('probe')
    parser.add_argument('manifest')
    parser.add_argument('fixtures')
    parser.add_argument('report')
    parser.add_argument('--expected-commit')
    args = parser.parse_args()
    run(args.probe, args.manifest, args.fixtures, args.report, args.expected_commit)
