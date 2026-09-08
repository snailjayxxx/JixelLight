"""Native ARW/JPEG compatibility, held-out fitting and regression gates.
No original/reference images are retained in the report artifacts.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys


def sha(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def check(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def run(exe: Path, arguments: list[str], log: Path, expect_success: bool = True) -> None:
    with log.open('w', encoding='utf-8') as f:
        result = subprocess.run([str(exe), *arguments], stdout=f, stderr=subprocess.STDOUT, timeout=180)
    check((result.returncode == 0) == expect_success,
          f'Unexpected probe exit {result.returncode}; see {log.name}')


def validate(exe: Path, manifest: Path, output: Path) -> int:
    root = os.environ.get('JIXELLIGHT_SONY_FIXTURES', '')
    if not root:
        if os.environ.get('JIXELLIGHT_REQUIRE_SONY_REAL'):
            raise RuntimeError('Real Sony fixtures are required, not configured')
        print('SKIP: real Sony files not configured. This is NOT a real-file validation pass.')
        return 77
    root = Path(root).resolve()
    spec = json.loads(manifest.read_text(encoding='utf-8'))
    expected = spec['expected']
    output.mkdir(parents=True, exist_ok=True)
    summary = {'schema': 1, 'passed': False, 'limitations': spec['limitations'], 'scenes': []}
    try:
        for item in spec['items']:
            check(sha(root / item['file']) == item['sha256'], f'Fixture hash mismatch: {item["file"]}')
        for pair in spec['pairs']:
            scene = output / pair['id']
            scene.mkdir(exist_ok=True)
            run(exe, [str(root / pair['raw']), str(root / pair['jpeg']), str(scene)], scene / 'probe.log')
            report = json.loads((scene / 'report.json').read_text(encoding='utf-8'))
            for kind in ('rawMetadata', 'jpegMetadata'):
                m = report[kind]
                check(m['model'] == expected['model'] and m['recordedModel'] == expected['recordedModel'], f'{kind}: model identity')
                look = m['sonyLook']
                check(look['code'] == expected['code'] and look['autoEligible'], f'{kind}: look identity')
                check(look['sonyModelId'] == expected['sonyModelId'], f'{kind}: numeric model')
                check(look['parameters'] == expected['parameters'], f'{kind}: eight controls disagree with ExifTool oracle')
            check(report['pairMetadataMatches'], 'Paired metadata rejected')
            check(report['automaticReference']['kind'] == 'paired-jpeg', 'Actual source/cache metadata lost automatic JPEG pairing')
            fit = report['fit']
            check(report['fitAccepted'], report['fitError'])
            check(fit['geometryBasis'] == 'camera-metadata-default-crop' and fit['sourceCrop'] == expected['cameraDefaultCrop'], 'Default crop not preserved/applied to matching')
            check(report['width'] == 7028 and report['height'] == 4688, 'Full developed RAW was unexpectedly cropped')
            check(report['referencePixelCount'] == report['reference']['width'] * report['reference']['height'], 'Reference histogram counts')
            check(fit['heldoutRmseAfter'] < .05 and fit['heldoutRmseAfter'] < fit['heldoutRmseBefore'] * .98, 'Single-image held-out error gate')
            check(report['asShot']['parameters'] == expected['parameters'], 'As-shot values lost')
            summary['scenes'].append({'id': pair['id'], 'metrics': fit, 'model': report['decoderModel']})
            # Do not redistribute copyrighted review photographs in test artifacts.
            for picture in scene.glob('*.png'):
                picture.unlink()
        dataset = output / 'dataset'
        dataset.mkdir(exist_ok=True)
        (dataset / 'empirical.jlook.json').unlink(missing_ok=True)
        dataset_spec = {'pairs': [{**p, 'raw': str(root / p['raw']), 'jpeg': str(root / p['jpeg'])} for p in spec['pairs']]}
        (output / 'dataset-input.json').write_text(json.dumps(dataset_spec), encoding='utf-8')
        run(exe, ['--dataset', str(output / 'dataset-input.json'), str(dataset)], dataset / 'probe.log')
        result = json.loads((dataset / 'dataset-report.json').read_text(encoding='utf-8'))
        check(result['accepted'] and result['metrics']['independentValidationPassed'], 'Independent scene gate failed')
        check(result['metrics']['cameraCalibration'] is False, 'Empirical fit misrepresented as universal calibration')
        validation = [s for s in result['metrics']['scenes'] if s['role'] == 'independent-validation']
        check(len(validation) == 1 and validation[0]['id'] == spec['pairs'][2]['id'], 'Hold-out scene provenance')
        check(validation[0]['heldoutRmseAfter'] <= .05, 'Independent scene error exceeds predeclared limit')
        summary['dataset'] = result
        # A same-file training/validation duplicate must fail before fitting.
        invalid = {'pairs': [dataset_spec['pairs'][0], dataset_spec['pairs'][1],
                             {**dataset_spec['pairs'][0], 'id': 'duplicate', 'role': 'validation'}]}
        (output / 'duplicate-input.json').write_text(json.dumps(invalid), encoding='utf-8')
        bad_dir = output / 'duplicate-rejected'
        run(exe, ['--dataset', str(output / 'duplicate-input.json'), str(bad_dir)], output / 'duplicate.log', False)
        check(not (bad_dir / 'empirical.jlook.json').exists(), 'Rejected dataset generated profile')
        for item in spec['items']:
            check(sha(root / item['file']) == item['sha256'], 'An original fixture was modified')
        summary['passed'] = True
        print(json.dumps(summary, ensure_ascii=False, indent=2), flush=True)
    finally:
        for picture in output.rglob('*.png'):
            picture.unlink()
        (output / 'summary.json').write_text(json.dumps(summary, ensure_ascii=False, indent=2), encoding='utf-8')
    return 0


if __name__ == '__main__':
    if len(sys.argv) != 4:
        raise SystemExit('Usage: ValidateSonyFixtures.py native-probe manifest.json output-directory')
    raise SystemExit(validate(Path(sys.argv[1]).resolve(), Path(sys.argv[2]), Path(sys.argv[3])))
