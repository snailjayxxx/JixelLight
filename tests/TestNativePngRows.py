"""Loader contracts; real integer-filter correctness runs in CoreTests."""
import hashlib
import importlib.util
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SOURCE = Path(__file__).resolve().parents[1] / 'tools' / 'NativePngRows.py'

def fresh():
    spec = importlib.util.spec_from_file_location('isolated_native_png_rows', SOURCE)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module

def library(abi=1, stamp=b'abc1234', result=1):
    calls = []
    def filter_row(*args):
        calls.append(args[2:])
        return result
    return SimpleNamespace(jixel_png_rows_abi=lambda: abi,
                           jixel_png_rows_commit=lambda: stamp,
                           jixel_png_unfilter_row=filter_row, calls=calls)

class NativePngRowsTests(unittest.TestCase):
    def test_absent_library_retains_python_contract_without_mutating_rows(self):
        module = fresh()
        row, previous = bytearray(b'123'), bytearray(b'456')
        with patch.object(module, '_candidates', return_value=[]):
            self.assertFalse(module.unfilter(row, previous, 1, 4))
        self.assertEqual((row, previous), (b'123', b'456'))
        self.assertIsNone(module.metadata())

    def test_present_wrong_abi_cannot_fall_back_to_python(self):
        module = fresh()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'owned-library'; path.write_bytes(b'owned mock')
            with patch.object(module, '_candidates', return_value=[path]), patch.object(module.ctypes, 'CDLL', return_value=library(abi=9)):
                with self.assertRaises(ValueError): module.unfilter(bytearray(b'1'), bytearray(b'2'), 1, 4)
        self.assertIsNone(module.metadata())

    def test_bad_stamp_and_expected_build_mismatch_refuse_loaded_library(self):
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'owned-library'; path.write_bytes(b'owned mock')
            for stamp, arguments in [(None, ['tool']), (b'invalid', ['tool']), (b'abc1234', ['tool', '--expected-commit', 'ffffffff'])]:
                module = fresh()
                with self.subTest(stamp=stamp), patch.object(module, '_candidates', return_value=[path]), patch.object(module.ctypes, 'CDLL', return_value=library(stamp=stamp)), patch.object(module.sys, 'argv', arguments):
                    with self.assertRaises(ValueError): module.unfilter(bytearray(b'1'), bytearray(b'2'), 1, 4)
                self.assertIsNone(module.metadata())

    def test_native_refusal_and_aliasing_raise_without_python_fallback(self):
        module, native = fresh(), library(result=0)
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'owned-library'; path.write_bytes(b'owned mock')
            row, previous = bytearray(b'12'), bytearray(b'34')
            with patch.object(module, '_candidates', return_value=[path]), patch.object(module.ctypes, 'CDLL', return_value=native):
                with self.assertRaises(ValueError): module.unfilter(row, previous, 1, 4)
                self.assertEqual(len(native.calls), 1)
                with self.assertRaises(ValueError): module.unfilter(row, row, 1, 4)
                self.assertEqual(len(native.calls), 1)
            self.assertEqual((row, previous), (b'12', b'34'))

    def test_loaded_verifier_records_actual_abi_stamp_and_file_fingerprint(self):
        module = fresh()
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / 'owned-library'; path.write_bytes(b'owned mock')
            with patch.object(module, '_candidates', return_value=[path]), patch.object(module.ctypes, 'CDLL', return_value=library()), patch.object(module.sys, 'argv', ['tool', '--expected-commit', 'abc123456789']):
                self.assertTrue(module.unfilter(bytearray(b'12'), bytearray(b'34'), 1, 4))
            metadata = module.metadata()
            self.assertEqual(metadata['ABI'], 1)
            self.assertEqual(metadata['source_commit'], 'abc1234')
            self.assertEqual(metadata['library_sha256'], hashlib.sha256(path.read_bytes()).hexdigest())
            metadata['ABI'] = 99
            self.assertEqual(module.metadata()['ABI'], 1)

    def test_explicit_missing_verifier_refuses_instead_of_selecting_another(self):
        module = fresh()
        with tempfile.TemporaryDirectory() as folder, patch.dict(module.os.environ, {'JIXELLIGHT_PNG_ROWS_LIBRARY': str(Path(folder) / 'missing')}):
            with self.assertRaises(ValueError): module.unfilter(bytearray(b'1'), bytearray(b'2'), 1, 4)

if __name__ == '__main__': unittest.main()
