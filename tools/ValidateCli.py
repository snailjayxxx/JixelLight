"""Exercise offline exports, shared commands and destination protection without external fixtures."""
import hashlib
import copy
import contextlib
import json
from pathlib import Path
import struct
import sqlite3
import subprocess
import sys
import tempfile
import zlib


def chunk(kind, data):
    return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)


@contextlib.contextmanager
def open_database(path):
    with contextlib.closing(sqlite3.connect(path)) as connection, connection:
        yield connection


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
        assert len(schema['parameters']) == 15
        assert {c['name'] for c in schema['commands']} >= {'geometry.crop', 'geometry.rotate', 'geometry.flip', 'geometry.straighten', 'geometry.set', 'geometry.resetCorrections', 'hsl.set', 'curve.set', 'develop.reset', 'vignette.reset'}
        assert {p['name'] for p in schema['parameters']} >= {'vignetteAmount', 'vignetteMidpoint', 'vignetteFeather'}
        assert len(next(c for c in schema['commands'] if c['name'] == 'geometry.set')['parameters']) == 5
        assert schema['batch']['schema'] == 1 and schema['batch']['maximum_jobs'] == 1000
        commands = root / 'commands.json'
        commands.write_text(json.dumps([{'command': 'develop.set', 'parameter': 'exposure', 'value': 1}]))
        destination = root / 'result.png'
        rendered = json.loads(run('--commands', commands, '--space', 'display-p3', source, destination).stdout)
        assert rendered['adjustments']['exposure'] == 1 and rendered['backend'] == 'cpu-reference'
        assert destination.read_bytes()[24] == 16  # PNG IHDR bit depth, independent of Qt reader.
        # Parse container metadata independently from the Qt decoder. These
        # checks also run against the deployed CLI with SDK paths removed.
        tiff = root / 'result.tiff'
        run('--commands', commands, '--space', 'display-p3', source, tiff)
        data = tiff.read_bytes()
        byte_order = '<' if data[:2] == b'II' else '>'
        assert data[:2] in (b'II', b'MM') and struct.unpack_from(byte_order+'H', data, 2)[0] == 42
        ifd = struct.unpack_from(byte_order+'I', data, 4)[0]
        tags = {}
        for index in range(struct.unpack_from(byte_order+'H', data, ifd)[0]):
            offset = ifd+2+12*index
            tag, kind, count, value = struct.unpack_from(byte_order+'HHII', data, offset)
            size = {1:1,2:1,3:2,4:4,5:8,7:1}.get(kind, 1)*count
            payload = data[offset+8:offset+8+size] if size <= 4 else data[value:value+size]
            tags[tag] = (kind, count, payload)
        assert struct.unpack(byte_order+'H'*tags[258][1], tags[258][2]) == (16,)*tags[258][1]
        assert tags[34675][2][36:40] == b'acsp'
        assert struct.unpack(byte_order+'H', tags[259][2])[0] == 5  # lossless LZW
        webp = root / 'result.webp'
        run('--commands', commands, '--space', 'display-p3', source, webp)
        data = webp.read_bytes()
        assert data[:4] == b'RIFF' and data[8:12] == b'WEBP'
        assert struct.unpack_from('<I', data, 4)[0]+8 == len(data)
        chunks, offset = {}, 12
        while offset+8 <= len(data):
            name, size = data[offset:offset+4], struct.unpack_from('<I',data,offset+4)[0]
            chunks[name] = data[offset+8:offset+8+size]
            offset += 8+size+(size & 1)
        assert chunks[b'ICCP'][36:40] == b'acsp'
        assert b'VP8 ' in chunks or b'VP8L' in chunks
        for index, output in enumerate((tiff, webp)):
            reimported = root / f'reimported-{index}.png'
            run(output, reimported)
            assert struct.unpack('>II', reimported.read_bytes()[16:24]) == (4, 2)
            digest = hashlib.sha256(output.read_bytes()).hexdigest()
            run(source, output, success=False)
            assert hashlib.sha256(output.read_bytes()).hexdigest() == digest
        exported = hashlib.sha256(destination.read_bytes()).hexdigest()
        run(source, destination, success=False)
        assert hashlib.sha256(destination.read_bytes()).hexdigest() == exported
        run(source, source, success=False)
        run('--space', 'invalid', source, root / 'invalid.jpg', success=False)
        run(source, root / 'invalid.pdf', success=False)
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
        commands.write_text(json.dumps([{'command':'geometry.straighten','degrees':15}]))
        straightened = root / 'straightened.png'
        result = json.loads(run('--commands', commands, source, straightened).stdout)
        assert struct.unpack('>II', straightened.read_bytes()[16:24]) == (2, 1)
        assert result['adjustments']['geometry']['schema'] == 2 and result['adjustments']['geometry']['straighten'] == 15
        corrections = [{'command': 'geometry.set', 'parameter': key, 'value': value}
                       for key, value in {'perspectiveHorizontal': .12, 'perspectiveVertical': -.08,
                                          'distortion': .1, 'redCa': 1, 'blueCa': -1}.items()]
        commands.write_text(json.dumps(corrections))
        corrected = root / 'corrected.png'
        result = json.loads(run('--commands', commands, source, corrected).stdout)
        assert result['adjustments']['geometry']['schema'] == 3
        assert all(result['adjustments']['geometry'][c['parameter']] == c['value'] for c in corrections)
        width, height = struct.unpack('>II', corrected.read_bytes()[16:24])
        assert 0 < width <= 4 and 0 < height <= 2 and corrected.read_bytes()[24] == 16
        commands.write_text(json.dumps(corrections + [{'command': 'geometry.resetCorrections'}]))
        reset = root / 'corrections-reset.png'
        result = json.loads(run('--commands', commands, source, reset).stdout)
        assert result['adjustments']['geometry']['schema'] == 1
        assert struct.unpack('>II', reset.read_bytes()[16:24]) == (4, 2)
        vignette = [{'command': 'develop.set', 'parameter': key, 'value': value}
                    for key, value in {'vignetteAmount': -1.8, 'vignetteMidpoint': .3, 'vignetteFeather': .6}.items()]
        exposure = [{'command': 'develop.set', 'parameter': 'exposure', 'value': 1}]
        commands.write_text(json.dumps(exposure))
        plain = root / 'plain.png'
        run('--commands', commands, source, plain)
        commands.write_text(json.dumps(exposure + vignette))
        darkened = root / 'vignette.png'
        result = json.loads(run('--commands', commands, source, darkened).stdout)
        assert result['adjustments']['vignette'] == {'schema': 1, 'amount': -1.8, 'midpoint': .3, 'feather': .6}
        def png_pixel_stream(path):
            data, compressed, offset = path.read_bytes(), bytearray(), 8
            while offset+12 <= len(data):
                size = struct.unpack_from('>I', data, offset)[0]
                if data[offset+4:offset+8] == b'IDAT':
                    compressed.extend(data[offset+8:offset+8+size])
                offset += size+12
            return zlib.decompress(compressed)
        assert png_pixel_stream(darkened) != png_pixel_stream(plain)
        commands.write_text(json.dumps(exposure + vignette + [{'command': 'vignette.reset'}]))
        reset_vignette = root / 'vignette-reset.png'
        result = json.loads(run('--commands', commands, source, reset_vignette).stdout)
        assert 'vignette' not in result['adjustments'] and result['adjustments']['exposure'] == 1
        assert png_pixel_stream(reset_vignette) == png_pixel_stream(plain)
        for bad in [
            {'command': 'geometry.crop', 'x': .8, 'y': 0, 'width': .5, 'height': 1},
            {'command': 'geometry.rotate', 'quarterTurns': .5},
            {'command': 'geometry.straighten', 'degrees': 45.1},
            {'command': 'geometry.straighten', 'degrees': '15'},
            {'command': 'geometry.set', 'parameter': 'perspectiveHorizontal', 'value': .401},
            {'command': 'geometry.set', 'parameter': 'distortion', 'value': -.301},
            {'command': 'geometry.set', 'parameter': 'redCa', 'value': 2.01},
            {'command': 'geometry.set', 'parameter': 'blueCa', 'value': '1'},
            {'command': 'geometry.set', 'parameter': 'unknown', 'value': 0},
            {'command': 'geometry.resetCorrections', 'extra': 1},
            {'command': 'vignette.reset', 'extra': 1},
            {'command': 'develop.set', 'parameter': 'vignetteAmount', 'value': '-1'},
            {'command': 'develop.set', 'parameter': 'vignetteFeather', 'value': None},
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
        plan = root / 'batch.json'
        def write_batch(jobs, **overrides):
            manifest = {'schema': 1, 'engine': rendered['engine'], 'jobs': jobs}
            manifest.update(overrides)
            plan.write_text(json.dumps(manifest))
        def job(destination, commands=None, source='original.png', **options):
            return {'source': source, 'destination': destination, 'commands': commands or [], **options}
        # Relative paths resolve against the manifest, independently of the CLI cwd.
        write_batch([
            job('batch-a.png', [{'command': 'develop.set', 'parameter': 'exposure', 'value': .5}], space='adobe-rgb'),
            job('batch-b.png', [{'command': 'geometry.crop', 'x': 0, 'y': 0, 'width': .5, 'height': 1}]),
        ])
        batch = json.loads(run('--batch', plan, '--space', 'display-p3').stdout)
        assert batch['ok'] and batch['completed'] == 2 and batch['failed'] == batch['not_attempted'] == 0
        assert [r['space'] for r in batch['results']] == ['adobe-rgb', 'display-p3']
        assert [r['adjustments']['exposure'] for r in batch['results']] == [.5, 0]
        assert struct.unpack('>II', (root / 'batch-b.png').read_bytes()[16:24]) == (2, 2)
        assert (root / 'batch-a.png').read_bytes()[24] == 16
        preserved = hashlib.sha256((root / 'batch-a.png').read_bytes()).hexdigest()
        refused = root / 'batch-refused.png'
        # The second job is invalid: the first must also produce no file.
        write_batch([job(refused.name), job('batch-a.png')])
        run('--batch', plan, success=False)
        assert not refused.exists()
        assert hashlib.sha256((root / 'batch-a.png').read_bytes()).hexdigest() == preserved
        write_batch([job(refused.name), job('./' + refused.name)])
        run('--batch', plan, success=False)
        assert not refused.exists()
        write_batch([job(refused.name), job('BATCH-REFUSED.PNG')])
        run('--batch', plan, success=False)
        assert not refused.exists()
        invalid_batches = [
            ([job(refused.name), job('second.png', [{'command': 'geometry.flip', 'axis': 'diagonal'}])], {}),
            ([job(refused.name), job('second.png', source='missing.png')], {}),
            ([job(refused.name, typo=True)], {}),
            ([job(refused.name, space='unknown')], {}),
            ([job('missing-directory/result.png')], {}),
            ([job(refused.name)], {'schema': 2}),
            ([job(refused.name)], {'engine': 'future-engine'}),
            ([job(refused.name)], {'typo': True}),
            ([], {}),
            ([job(refused.name)] * 1001, {}),
        ]
        for jobs, overrides in invalid_batches:
            write_batch(jobs, **overrides)
            run('--batch', plan, success=False)
            assert not refused.exists() and not (root / 'second.png').exists()
        write_batch([job(refused.name)])
        run('--batch', plan, '--commands', commands, success=False)
        run('--batch', plan, source, refused, success=False)
        # Runtime decode failures retain completed outputs and report unattempted jobs.
        (root / 'corrupt.png').write_bytes(b'not an image')
        write_batch([job('partial.png'), job('failed.png', source='corrupt.png'), job('unattempted.jpg')])
        partial = json.loads(run('--batch', plan, success=False).stdout)
        assert not partial['ok'] and partial['completed'] == partial['failed'] == partial['not_attempted'] == 1
        assert [r['ok'] for r in partial['results']] == [True, False]
        assert (root / 'partial.png').is_file() and not (root / 'failed.png').exists() and not (root / 'unattempted.jpg').exists()
        assert not list(root.glob('.jixellight-export-*'))
        catalog = root / 'Saved.jlp'
        catalog.mkdir()
        db_path = catalog / 'Project.db'
        original_state = copy.deepcopy(rendered['adjustments'])
        original_state['exposure'] = .25
        redo_state = copy.deepcopy(original_state)
        redo_state['exposure'] = 1.25
        saved = copy.deepcopy(original_state)
        saved['_history'] = {'schema': 1, 'cursor': 0, 'luts': [], 'entries': [
            {'state': original_state, 'action': 'initial'}, {'state': redo_state, 'action': 'exposure'}]}
        alternate = copy.deepcopy(original_state)
        alternate['exposure'] = -.5
        alternate['geometry'].update(x=0, y=0, width=.5, height=1)
        alternate['look'].update(mode='manual', code='FL', strength=.7)
        key = 'jixel-copy:11111111-1111-4111-8111-111111111111'
        with open_database(db_path) as db:
            db.executescript("CREATE TABLE meta(key TEXT PRIMARY KEY,value TEXT);"
                             "INSERT INTO meta VALUES('project_name','Saved');"
                             "CREATE TABLE photos(path TEXT PRIMARY KEY, imported_at TEXT DEFAULT CURRENT_TIMESTAMP, adjustment_json TEXT);"
                             "CREATE TABLE virtual_sources(key TEXT PRIMARY KEY,json TEXT);")
            db.execute('INSERT INTO photos(path,adjustment_json) VALUES(?,?)', ('../original.png', json.dumps(saved)))
            db.execute('INSERT INTO photos(path,adjustment_json) VALUES(?,?)', (key, json.dumps(alternate)))
            db.execute('INSERT INTO virtual_sources VALUES(?,?)', (key, json.dumps({'schema': 1, 'source': '../original.png', 'name': 'Alternative'})))
        before_catalog = hashlib.sha256(db_path.read_bytes()).hexdigest()
        output = root / 'catalog-output'
        output.mkdir()
        snapshot = json.loads(run('--catalog', catalog, '--output-dir', output).stdout)
        assert snapshot['ok'] and snapshot['mode'] == 'catalog' and snapshot['completed'] == 2
        assert [r['adjustments']['exposure'] for r in snapshot['results']] == [.25, -.5]
        assert snapshot['results'][1]['catalog_key'] == key and snapshot['results'][1]['version_name'] == 'Alternative'
        assert snapshot['results'][1]['adjustments']['look']['code'] == 'FL'
        assert struct.unpack('>II', Path(snapshot['results'][1]['destination']).read_bytes()[16:24]) == (2, 2)
        assert hashlib.sha256(db_path.read_bytes()).hexdigest() == before_catalog
        assert set(p.name for p in catalog.iterdir()) == {'Project.db'}  # No migration, backup or WAL switch.
        run('--catalog', catalog, '--output-dir', output, success=False)
        run('--catalog', catalog, success=False)
        run('--catalog', catalog, '--batch', plan, success=False)
        run('--catalog', catalog, '--output-dir', output, '--format', 'pdf', success=False)
        override = root / 'catalog-override'
        override.mkdir()
        commands.write_text(json.dumps([{'command': 'develop.set', 'parameter': 'exposure', 'value': 1},
                                        {'command': 'geometry.straighten', 'degrees': 15}]))
        edited = json.loads(run('--catalog', catalog, '--output-dir', override, '--commands', commands, '--format', 'jpeg').stdout)
        assert [r['adjustments']['exposure'] for r in edited['results']] == [1, 1]
        assert all(r['adjustments']['geometry']['straighten'] == 15 for r in edited['results'])
        assert all(Path(r['destination']).read_bytes().startswith(b'\xff\xd8') for r in edited['results'])
        assert hashlib.sha256(db_path.read_bytes()).hexdigest() == before_catalog
        # Unsupported saved history / Sony / geometry schemas reject before export.
        for format, suffix in (('tiff', '.tif'), ('webp', '.webp')):
            target = root / ('catalog-'+format)
            target.mkdir()
            raster = json.loads(run('--catalog', catalog, '--output-dir', target, '--format', format).stdout)
            assert raster['ok'] and raster['completed'] == 2
            assert [r['adjustments']['exposure'] for r in raster['results']] == [.25, -.5]
            assert all(Path(r['destination']).suffix == suffix for r in raster['results'])
            assert hashlib.sha256(db_path.read_bytes()).hexdigest() == before_catalog
        bad_output = root / 'catalog-rejected'
        bad_output.mkdir()
        for corrupt in [{**saved, '_history': {'schema': 2}}, {**original_state, 'look': {'schema': 2}},
                        {**original_state, 'geometry': {'schema': 3, 'straighten': 5}},
                        {**original_state, 'vignette': {'schema': 2, 'amount': -1, 'midpoint': .5, 'feather': 1}},
                        {**original_state, 'vignette': {'schema': 1, 'amount': -4, 'midpoint': .5, 'feather': 1}}]:
            with open_database(db_path) as db:
                db.execute('UPDATE photos SET adjustment_json=? WHERE path=?', (json.dumps(corrupt), '../original.png'))
            current = hashlib.sha256(db_path.read_bytes()).hexdigest()
            run('--catalog', catalog, '--output-dir', bad_output, success=False)
            assert not list(bad_output.iterdir())
            assert hashlib.sha256(db_path.read_bytes()).hexdigest() == current
        assert hashlib.sha256(source.read_bytes()).hexdigest() == original
        assert not list(root.rglob('.jixellight-export-*'))  # Includes catalog output directories.
        print(json.dumps({'ok': True, 'checks': ['schema', 'develop.set', 'geometry-hsl-curves', 'straighten', 'manual-perspective-lens-ca', 'correction-reset', 'vignette-ev-midpoint-feather', 'vignette-pixel-export', 'vignette-isolated-reset', 'invalid-edit-commands', 'png16', 'tiff16-lzw', 'webp8', 'jpeg', 'icc-space', 'invalid-command', 'no-overwrite', 'original-read-only', 'batch-relative-paths', 'batch-state-isolation', 'batch-full-preflight', 'batch-duplicate-destinations', 'batch-partial-failure', 'staging-cleanup', 'catalog-read-only', 'catalog-history-cursor', 'catalog-virtual-copies', 'catalog-command-overrides', 'catalog-unknown-history-look-rejection']}))


if __name__ == '__main__':
    main()
