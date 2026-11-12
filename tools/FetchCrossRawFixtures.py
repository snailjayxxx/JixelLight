"""Fetch three CC0 format fixtures from pinned raw.pixls.us records; never replace files."""
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile
import urllib.parse
import urllib.request


def read_manifest(path):
    with Path(path).open('rb') as stream:
        data = stream.read(65537)
    if len(data) > 65536:
        raise ValueError('Fixture manifest too large')
    spec = json.loads(data)
    if type(spec['schema']) is not int or spec['schema'] != 1 or spec['purpose'] != 'cross-format-raw-readonly-validation' or len(spec['items']) != 3:
        raise ValueError('Unexpected fixture manifest')
    names, ids = set(), set()
    for item in spec['items']:
        name, identifier = item['file'], item['id']
        url = urllib.parse.urlsplit(item['source'])
        if (not re.fullmatch(r'[A-Za-z0-9_-]+\.(?:CR3|NEF|RAF)', name) or name in names
                or not re.fullmatch(r'[0-9]{1,8}', identifier) or identifier in ids
                or item['license'] != 'CC0-1.0' or item['license_url'] != 'https://creativecommons.org/publicdomain/zero/1.0/'
                or not re.fullmatch(r'[0-9a-f]{64}', item['sha256']) or type(item['bytes']) is not int
                or not 0 < item['bytes'] <= 64*1024*1024 or url.scheme != 'https' or url.netloc != 'raw.pixls.us'
                or not url.path.startswith('/getfile.php/' + identifier + '/nice/') or url.query or url.fragment):
            raise ValueError('Unsafe or unpinned fixture record')
        names.add(name)
        ids.add(identifier)
    return spec


def file_sha(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def fetch(manifest, directory):
    spec = read_manifest(manifest)
    root = Path(directory)
    root.mkdir(parents=True, exist_ok=True)
    for item in spec['items']:
        target = root / item['file']
        if target.exists() or target.is_symlink():
            if target.is_symlink() or not target.is_file() or target.stat().st_size != item['bytes'] or file_sha(target) != item['sha256']:
                raise ValueError('Existing fixture differs; retained unchanged: ' + item['file'])
            print('Verified cached ' + item['file'], flush=True)
            continue
        # An interrupted/mismatched download stays in its unique owned folder
        # for diagnosis. Only a fully verified new file is published.
        staging = Path(tempfile.mkdtemp(prefix='.cross-raw-download-', dir=root))
        partial = staging / item['file']
        with urllib.request.urlopen(item['source'], timeout=60) as response, partial.open('xb') as output:
            if urllib.parse.urlsplit(response.url).netloc != 'raw.pixls.us' or urllib.parse.urlsplit(response.url).scheme != 'https':
                raise ValueError('Unexpected fixture redirect')
            total = 0
            while block := response.read(1024*1024):
                total += len(block)
                if total > item['bytes']:
                    raise ValueError('Fixture exceeds pinned size')
                output.write(block)
        if partial.stat().st_size != item['bytes'] or file_sha(partial) != item['sha256']:
            raise ValueError('Fixture SHA/size differs; download retained')
        os.link(partial, target)  # Exclusive publication, including a raced target.
        partial.unlink()
        staging.rmdir()
        print('Fetched and verified ' + item['file'], flush=True)


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: FetchCrossRawFixtures.py pinned-manifest.json output-directory')
    fetch(sys.argv[1], sys.argv[2])
