"""Optional independent byte filters; standalone validators retain Python paths."""
from __future__ import annotations
import ctypes
import hashlib
import os
from pathlib import Path
import re
import sys

_loaded = False
_library = None
_function = None
_metadata = None

def _candidates():
    explicit = os.environ.get('JIXELLIGHT_PNG_ROWS_LIBRARY')
    if explicit:
        path = Path(explicit).resolve()
        if not path.is_file():
            raise ValueError('Explicit PNG row library is missing')
        return [path]
    if len(sys.argv) < 2:
        return []
    executable = Path(sys.argv[1]).resolve()
    if not executable.is_file():
        return []
    name = ('JixelLightPngRows.dll' if sys.platform == 'win32' else
            'libJixelLightPngRows.dylib' if sys.platform == 'darwin' else 'libJixelLightPngRows.so')
    directories = [executable.parent]
    if sys.platform == 'darwin' and executable.parent.name == 'MacOS' and len(executable.parents) > 3:
        directories.append(executable.parents[3])  # Local unbundled QA library beside App.
    return [folder / name for folder in directories]

def _load():
    global _loaded, _library, _function, _metadata
    if _loaded:
        return _function
    path = next((p for p in _candidates() if p.is_file()), None)
    if path is None:
        _loaded = True
        return None
    library = ctypes.CDLL(str(path))
    try:
        abi = library.jixel_png_rows_abi
        abi.argtypes, abi.restype = [], ctypes.c_uint
        stamp = library.jixel_png_rows_commit
        stamp.argtypes, stamp.restype = [], ctypes.c_char_p
        function = library.jixel_png_unfilter_row
    except AttributeError as error:
        raise ValueError('Invalid PNG row library exports') from error
    if abi() != 1:
        raise ValueError('Unsupported PNG row library ABI')
    raw_stamp = stamp()
    if not isinstance(raw_stamp, bytes):
        raise ValueError('Missing PNG row library compile stamp')
    commit = raw_stamp.decode('ascii')
    if re.fullmatch(r'[0-9a-f]{7,40}|unknown', commit) is None:
        raise ValueError('Invalid PNG row library compile stamp')
    if '--expected-commit' in sys.argv:
        position = sys.argv.index('--expected-commit') + 1
        if position >= len(sys.argv) or not sys.argv[position].startswith(commit):
            raise ValueError('PNG row library compile stamp differs from expected build')
    function.argtypes = [ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t, ctypes.c_size_t, ctypes.c_uint]
    function.restype = ctypes.c_int
    with path.open('rb') as stream:
        fingerprint = hashlib.file_digest(stream, 'sha256').hexdigest()
    _library, _function = library, function
    _metadata = {'backend': 'independent-native-byte-filters', 'ABI': 1,
                 'source_commit': commit, 'library_sha256': fingerprint}
    _loaded = True
    return function

def unfilter(row, previous, bpp, kind):
    """True means all bytes were decoded in place; False requests Python."""
    function = _load()
    if function is None:
        return False
    if row is previous or len(row) != len(previous) or not row:
        raise ValueError('Invalid PNG row buffers')
    mutable = (ctypes.c_uint8 * len(row)).from_buffer(row)
    above = (ctypes.c_uint8 * len(previous)).from_buffer(previous)
    if function(mutable, above, len(row), bpp, kind) != 1:
        raise ValueError('PNG row library refused the row contract')
    return True

def metadata():
    return dict(_metadata) if _metadata else None
