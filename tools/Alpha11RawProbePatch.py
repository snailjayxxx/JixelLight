#!/usr/bin/env python3
from pathlib import Path

raw_path = Path("core/raw/RawDecoder.cpp")
raw = raw_path.read_text(encoding="utf-8")
old = """    params.adjust_maximum_thr = 0.0f;\n    params.bright = 1.0f;\n    params.highlight = 2;         // LibRaw highlight blend before our scene-linear tone stage.\n"""
new = """    {\n        bool ok = false;\n        const float probe = qEnvironmentVariable(\"JIXELLIGHT_PROBE_ADJUST_MAXIMUM_THR\").toFloat(&ok);\n        params.adjust_maximum_thr = ok ? std::clamp(probe, 0.0f, 1.0f) : 0.0f;\n    }\n    params.bright = 1.0f;\n    {\n        bool ok = false;\n        const int probe = qEnvironmentVariableIntValue(\"JIXELLIGHT_PROBE_LIBRAW_HIGHLIGHT\", &ok);\n        params.highlight = ok ? std::clamp(probe, 0, 9) : 2;\n    }\n"""
if old not in raw:
    raise SystemExit("RawDecoder probe patch anchor not found")
raw_path.write_text(raw.replace(old, new, 1), encoding="utf-8")

pipeline_path = Path("core/pipeline/ImagePipeline.cpp")
pipeline = pipeline_path.read_text(encoding="utf-8")n