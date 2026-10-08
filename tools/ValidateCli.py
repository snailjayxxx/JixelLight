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
        run(source, root / 'result.jpg')
        assert (root / 'result.jpg').read_bytes().startswith(b'\xff\xd8')
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original
        print(json.dumps({'ok': True, 'checks': ['schema', 'develop.set', 'png16', 'jpeg', 'icc-space', 'invalid-command', 'no-overwrite', 'original-read-only']}))


if __name__ == '__main__':
    main()
