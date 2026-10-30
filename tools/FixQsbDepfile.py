#!/usr/bin/env python3
"""Escape the fresh, one-path-per-line depfile emitted by Qt qsb 6.8.3.

Run immediately after qsb, before CMake consumes DEPFILE. The shader bundle is
never changed. This deliberately accepts qsb's format, not arbitrary Makefiles.
"""

import argparse
import ntpath
import os
from pathlib import Path
import stat
import sys
import tempfile


CONTINUATION = " \\\n  "


def make_path(value):
    if not value or any(c in value for c in "\r\n\0"):
        raise ValueError("qsb depfile contains an empty or multiline path")
    drive, _ = ntpath.splitdrive(value)
    if drive:
        value = value.replace("\\", "/")
    elif "\\" in value or ":" in value:
        raise ValueError("qsb depfile contains an unsupported POSIX path")
    if not (value.startswith("/") or ntpath.isabs(value)):
        raise ValueError("qsb depfile contains a non-absolute path")
    return value.replace("$", "$$").replace("#", "\\#").replace(" ", "\\ ").replace("\t", "\\\t")


def normalize(data, output, source):
    text = data.decode("utf-8")
    prefix = output + ":" + CONTINUATION + source
    if not text.startswith(prefix):
        raise ValueError("depfile does not match the fresh qsb output/source paths")
    tail = text[len(prefix):]
    if tail and not tail.startswith(CONTINUATION):
        raise ValueError("depfile does not match the qsb dependency-line format")
    dependencies = [source]
    if tail:
        dependencies.extend(tail[len(CONTINUATION):].split(CONTINUATION))
    return (make_path(output) + ":" + CONTINUATION
            + CONTINUATION.join(make_path(p) for p in dependencies) + "\n").encode("utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("depfile", type=Path)
    parser.add_argument("output")
    parser.add_argument("source")
    args = parser.parse_args()
    temporary = None
    try:
        original = args.depfile.read_bytes()
        escaped = normalize(original, args.output, args.source)
        mode = stat.S_IMODE(args.depfile.stat().st_mode)
        with tempfile.NamedTemporaryFile(dir=args.depfile.parent, prefix=".qsb-depfile-", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(escaped)
        temporary.chmod(mode)
        os.replace(temporary, args.depfile)
    except (OSError, UnicodeError, ValueError) as error:
        print(f"Cannot normalize qsb depfile: {error}", file=sys.stderr)
        return 1
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
