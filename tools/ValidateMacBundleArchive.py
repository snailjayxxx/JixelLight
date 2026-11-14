"""Compare every deployed bundle entry with its ZIP and fresh extracted copy."""
from pathlib import Path, PurePosixPath
import argparse
import hashlib
import json
import os
import posixpath
import stat
import zipfile


def digest(stream):
    return hashlib.file_digest(stream, 'sha256').hexdigest()


def link_target(relative, target):
    if not target or len(target.encode('utf-8')) > 4096 or '\\' in target or '\x00' in target or target.startswith('/'):
        raise ValueError('Invalid bundle symlink target')
    resolved = posixpath.normpath(posixpath.join(posixpath.dirname(relative), target))
    if resolved == '..' or resolved.startswith('../'):
        raise ValueError('Bundle symlink escapes its root')
    return target


def bundle_manifest(root):
    root = Path(root)
    if root.is_symlink() or not root.is_dir():
        raise ValueError('Bundle must be an existing ordinary directory')
    entries = {}
    for parent, directories, files in os.walk(root, followlinks=False):
        for name in directories + files:
            path = Path(parent) / name
            relative = path.relative_to(root).as_posix()
            mode = path.lstat().st_mode
            if stat.S_ISLNK(mode):
                target = link_target(relative, os.readlink(path))
                if not path.resolve(strict=True).is_relative_to(root.resolve()):
                    raise ValueError('Resolved bundle symlink escapes its root')
                entries[relative] = ['symlink', target]
            elif stat.S_ISDIR(mode):
                entries[relative] = ['directory']
            elif stat.S_ISREG(mode):
                with path.open('rb') as stream:
                    entries[relative] = ['file', path.stat().st_size, mode & 0o111, digest(stream)]
            else:
                raise ValueError('Unsupported bundle entry type')
    return entries


def archive_manifest(path, bundle_name):
    path = Path(path)
    if path.stat().st_size > 512 * 1024**2:
        raise ValueError('Bundle ZIP exceeds the validation byte budget')
    entries, names, unpacked = {}, set(), 0
    with zipfile.ZipFile(path) as archive:
        if len(archive.infolist()) > 50000:
            raise ValueError('Bundle ZIP exceeds the entry budget')
        for item in archive.infolist():
            name = item.filename.rstrip('/')
            parts = name.split('/')
            if not name or '\\' in name or '\x00' in name or any(part in ('', '.', '..') for part in parts):
                raise ValueError('Unsafe bundle archive path')
            if PurePosixPath(name).is_absolute() or parts[0] != bundle_name or name in names:
                raise ValueError('Wrong bundle root or duplicate archive entry')
            names.add(name)
            if item.is_dir() and archive.read(item):
                raise ValueError('Archive directory contains unexpected payload')
            if len(parts) == 1:
                if not item.is_dir():
                    raise ValueError('Bundle archive root is not a directory')
                continue
            relative = '/'.join(parts[1:])
            unpacked += item.file_size
            if unpacked > 2 * 1024**3:
                raise ValueError('Bundle ZIP exceeds the unpacked byte budget')
            mode = item.external_attr >> 16
            if stat.S_ISLNK(mode):
                if item.file_size > 4096:
                    raise ValueError('Oversize bundle symlink')
                entries[relative] = ['symlink', link_target(relative, archive.read(item).decode('utf-8'))]
            elif item.is_dir():
                entries[relative] = ['directory']
            elif stat.S_IFMT(mode) in (0, stat.S_IFREG):
                with archive.open(item) as stream:
                    entries[relative] = ['file', item.file_size, mode & 0o111, digest(stream)]
            else:
                raise ValueError('Unsupported archive entry type')
    return entries


def compare_bundle_archive(original, extracted, archive):
    original, extracted, archive = Path(original), Path(extracted), Path(archive)
    if original.resolve() == extracted.resolve() or original.name != extracted.name:
        raise ValueError('A separate extracted bundle with the same name is required')
    expected = bundle_manifest(original)
    if not expected or expected != archive_manifest(archive, original.name) or expected != bundle_manifest(extracted):
        raise ValueError('Original bundle, ZIP and fresh extracted bundle differ')
    with archive.open('rb') as stream:
        archive_sha = digest(stream)
    return {'passed': True, 'original_bundle': str(original), 'extracted_bundle': str(extracted),
            'archive': str(archive), 'archive_bytes': archive.stat().st_size,
            'archive_sha256': archive_sha, 'complete_entries': expected,
            'regular_files': sum(item[0] == 'file' for item in expected.values()),
            'symlinks': sum(item[0] == 'symlink' for item in expected.values()),
            'all_archive_CRC_and_regular_bytes_and_exec_bits_and_symlink_targets_equal': True}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('original', type=Path)
    parser.add_argument('extracted', type=Path)
    parser.add_argument('archive', type=Path)
    parser.add_argument('--report', type=Path, required=True)
    args = parser.parse_args()
    try:
        result = compare_bundle_archive(args.original, args.extracted, args.archive)
    except (OSError, ValueError, RuntimeError, zipfile.BadZipFile, UnicodeError) as error:
        result = {'passed': False, 'error': str(error)}
    with args.report.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')
    print(json.dumps({key: value for key, value in result.items() if key != 'complete_entries'}))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
