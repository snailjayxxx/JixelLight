"""Portable corruption/refusal checks for the actual Mac shipping ZIP verifier."""
from pathlib import Path
import os
import shutil
import stat
import sys
import tempfile
import unittest
import warnings
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools'))
from ValidateMacBundleArchive import archive_manifest, compare_bundle_archive


class MacBundleArchiveTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix='bundle with spaces ')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app = self.root / 'original/JixelLight.app'
        (self.app / 'Contents/MacOS').mkdir(parents=True)
        (self.app / 'Contents/PlugIns').mkdir()
        (self.app / 'Contents/MacOS/JixelLight').write_bytes(b'owned main payload')
        (self.app / 'Contents/PlugIns/cocoa').write_bytes(b'owned plugin payload')
        os.chmod(self.app / 'Contents/MacOS/JixelLight', 0o755)
        self.copy = self.root / 'extracted/JixelLight.app'
        shutil.copytree(self.app, self.copy)
        self.archive = self.root / 'shipping.zip'
        with zipfile.ZipFile(self.archive, 'w', compression=zipfile.ZIP_STORED) as archive:
            for path in sorted(self.app.rglob('*')):
                archive.write(path, path.relative_to(self.app.parent).as_posix())

    def test_complete_file_bytes_and_executable_permissions_match(self):
        result = compare_bundle_archive(self.app, self.copy, self.archive)
        self.assertTrue(result['passed'])
        self.assertEqual(result['regular_files'], 2)

    def test_modified_extracted_plugin_refuses(self):
        (self.copy / 'Contents/PlugIns/cocoa').write_bytes(b'changed plugin payload')
        with self.assertRaises(ValueError):
            compare_bundle_archive(self.app, self.copy, self.archive)

    def test_missing_extracted_file_refuses(self):
        (self.copy / 'Contents/MacOS/JixelLight').unlink()
        with self.assertRaises(ValueError):
            compare_bundle_archive(self.app, self.copy, self.archive)

    def test_extra_unarchived_original_file_refuses(self):
        (self.app / 'unarchived').write_bytes(b'new bytes')
        with self.assertRaises(ValueError):
            compare_bundle_archive(self.app, self.copy, self.archive)

    def test_corrupt_archive_CRC_refuses(self):
        data = self.archive.read_bytes()
        self.assertIn(b'owned plugin payload', data)
        self.archive.write_bytes(data.replace(b'owned plugin payload', b'brokenplugin payload'))
        with self.assertRaises(zipfile.BadZipFile):
            archive_manifest(self.archive, self.app.name)

    def test_archive_parent_traversal_refuses(self):
        with zipfile.ZipFile(self.archive, 'a') as archive:
            archive.writestr('JixelLight.app/../outside', b'owned')
        with self.assertRaises(ValueError):
            archive_manifest(self.archive, self.app.name)

    def test_duplicate_archive_path_refuses(self):
        with warnings.catch_warnings():
            warnings.simplefilter('ignore', UserWarning)
            with zipfile.ZipFile(self.archive, 'a') as archive:
                archive.writestr('JixelLight.app/Contents/PlugIns/cocoa', b'other bytes')
        with self.assertRaises(ValueError):
            archive_manifest(self.archive, self.app.name)

    def test_escaping_archive_symlink_refuses_without_host_symlink_support(self):
        entry = zipfile.ZipInfo('JixelLight.app/Contents/escape')
        entry.create_system = 3
        entry.external_attr = (stat.S_IFLNK | 0o777) << 16
        with zipfile.ZipFile(self.archive, 'a') as archive:
            archive.writestr(entry, '../../outside')
        with self.assertRaises(ValueError):
            archive_manifest(self.archive, self.app.name)


if __name__ == '__main__':
    unittest.main()
