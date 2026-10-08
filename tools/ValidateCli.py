"""Exercise offline exports, shared commands and destination protection without external fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


def main():
    cli = str(Path(sys.argv[1]).resolve())
    with tempfile.TemporaryDirectory(prefix='jixellight-cli-') as directory:
        root = Path(directory)
        source = root / 'original.png'
        source.write_bytes(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', 4, 2, 8, 2, 0, 0, 0))
                           + chunk(b'IDAT', zlib.compress((b'\0' + bytes([64, 128, 192]) * 4) * 2)) + chunk(b'IEND', b''))
        original = hashlib.sha256(source.read_bytes()).hexdigest()
        def run(*args, success=True):
            process = subprocess.run([cli, *map(str, args)], capture_output=True, text=True, timeout=60)
            if (process.returncode == 0) != success:
                raise RuntimeError(f'Unexpected CLI result ({process.returncode}): {process.stderr}\n{process.stdout}')
            return process
        schema = json.loads(run('--schema').stdout)
        assert len(schema['parameters']) == 12
        assert {c['name'] for c in schema['commands']} >= {'geometry.crop', 'geometry.rotate', 'geometry.flip', 'hsl.set', 'curve.set', 'develop.reset'}
        commands = root / 'commands.json'
        commands.write_text(json.dumps([{'command': 'develop.set', 'parameter': 'exposure', 'value': 1}]))
        destination = root / 'result.png'
        rendered = json.loads(run('--commands', commands, '--space', 'display-p3', source, destination).stdout)
        assert rendered['adjustments']['exposure'] == 1 and rendered['backend'] == 'cpu-reference'
        assert destination.read_bytes()[24] == 16  # PNG IHDR bit depth, independent of Qt reader.
        exported = hashlib.sha256(destination.read_bytes()).hexdigest()
        run(source, destination, success=False)
        assert hashlib.sha256(destination.read_bytes()).hexdigest() == exported
        run(source, source, success=False)
        run('--space', 'invalid', source, root / 'invalid.jpg', success=False)
        run(source, root / 'invalid.tiff', success=False)
        commands.write_text(json.dumps([{'command': 'develop.set', 'parameter': 'unknown', 'value': 1}]))
        run('--commands', commands, source, root / 'bad.jpg', success=False)
        assert not (root / 'bad.jpg').exists()
        commands.write_text(json.dumps([
            {'command': 'geometry.crop', 'x': 0, 'y': 0, 'width': .5, 'height': 1},
            {'command': 'geometry.rotate', 'quarterTurns': 1},
            {'command': 'geometry.flip', 'axis': 'horizontal'},
            {'command': 'hsl.set', 'band': 2, 'component': 'saturation', 'value': 25},
            {'command': 'curve.set', 'channel': 'red', 'point': 2, 'value': .6},
        ]))
        transformed = root / 'transformed.png'
        result = json.loads(run('--commands', commands, source, transformed).stdout)
        assert struct.unpack('>II', transformed.read_bytes()[16:24]) == (2, 2)
        assert result['adjustments']['geometry']['quarterTurns'] == 1
        assert result['adjustments']['geometry']['flipHorizontal'] is True
        assert result['adjustments']['hslSaturation'][2] == 25
        assert result['adjustments']['redCurve'][2] == .6
        for bad in [
            {'command': 'geometry.crop', 'x': .8, 'y': 0, 'width': .5, 'height': 1},
            {'command': 'geometry.rotate', 'quarterTurns': .5},
            {'command': 'geometry.flip', 'axis': 'diagonal'},
            {'command': 'hsl.set', 'band': 8, 'component': 'saturation', 'value': 25},
            {'command': 'curve.set', 'channel': 'red', 'point': -1, 'value': .6},
            {'command': 'develop.set', 'parameter': 'exposure', 'value': 1, 'typo': 1},
        ]:
            commands.write_text(json.dumps([bad]))
            rejected = root / 'rejected.png'
            run('--commands', commands, source, rejected, success=False)
            assert not rejected.exists()
        run(source, root / 'result.jpg')
        assert (root / 'result.jpg').read_bytes().startswith(b'\xff\xd8')
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original
        print(json.dumps({'ok': True, 'checks': ['schema', 'develop.set', 'geometry-hsl-curves', 'invalid-edit-commands', 'png16', 'jpeg', 'icc-space', 'invalid-command', 'no-overwrite', 'original-read-only']}))


if __name__ == '__main__':
    main()
