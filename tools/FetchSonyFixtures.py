"""Acquire bounded, checksum-pinned, public test fixtures. Not used by the application.
Original photographs must not be included in source-control or application packages.
"""
from __future__ import annotations
import hashlib
import json
import os
from pathlib import Path
import sys
import time
from urllib.parse import urlparse
from urllib.request import Request, urlopen


def digest(path: Path) -> str:
    h = hashlib.sha256()
    with path.open('rb') as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b''):
            h.update(chunk)
    return h.hexdigest()


def fetch(manifest: Path, destination: Path) -> None:
    data = json.loads(manifest.read_text(encoding='utf-8'))
    destination.mkdir(parents=True, exist_ok=True)
    for item in data['items']:
        name, url, expected = item['file'], item['url'], item['sha256']
        if Path(name).name != name or urlparse(url).hostname != 'img.photographyblog.com':
            raise ValueError('Unexpected fixture destination or host')
        target = destination / name
        if target.is_file() and digest(target) == expected:
            print('Verified cached fixture:', name, flush=True)
            continue
        part = destination / (name + '.part')
        for attempt in range(3):
            try:
                request = Request(url, headers={'User-Agent': 'JixelLight-compatibility-test/0.1',
                                                'Referer': item['sourcePage']})
                with urlopen(request, timeout=90) as response, part.open('wb') as output:
                    count = 0
                    while chunk := response.read(1024 * 1024):
                        count += len(chunk)
                        if count > item['bytes'] or count > 100_000_000:
                            raise ValueError('Fixture exceeds pinned size')
                        output.write(chunk)
                if part.stat().st_size != item['bytes'] or digest(part) != expected:
                    raise ValueError('Fixture content changed: refusing unverified pixels')
                os.replace(part, target)
                print('Fetched and verified:', name, flush=True)
                break
            except Exception:
                part.unlink(missing_ok=True)
                if attempt == 2:
                    raise
                time.sleep(2 * (attempt + 1))
    (destination / 'dataset.json').write_text(json.dumps({'pairs': data['pairs']}, indent=2), encoding='utf-8')


if __name__ == '__main__':
    if len(sys.argv) != 3:
        raise SystemExit('Usage: FetchSonyFixtures.py manifest.json fixture-directory')
    fetch(Path(sys.argv[1]), Path(sys.argv[2]))
