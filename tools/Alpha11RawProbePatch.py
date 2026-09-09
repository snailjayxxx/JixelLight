#!/usr/bin/env python3
from pathlib import Path

raw_path = Path("core/raw/RawDecoder.cpp")
raw = raw_path.read_text(encoding="utf-8")
old = """    params.adjust_maximum_thr = 0.0f;\n    params.bright = 1.0f;\n    params.highlight = 2;         // LibRaw highlight blend before our scene-linear tone stage.\n"""
new = """    {\n        bool ok = false;\n        const float probe = qEnvironmentVariable(\"JIXELLIGHT_PROBE_ADJUST_MAXIMUM_THR\").toFloat(&ok);\n        params.adjust_maximum_thr = ok ? std::clamp(probe, 0.0f, 1.0f) : 0.0f;\n    }\n    params.bright = 1.0f;\n    {\n        bool ok = false;\n        const int probe = qEnvironmentVariableIntValue(\"JIXELLIGHT_PROBE_LIBRAW_HIGHLIGHT\", &ok);\n        params.highlight = ok ? std::clamp(probe, 0, 9) : 2;\n    }\n"""
if old not in raw:
    raise SystemExit("RawDecoder probe patch anchor not found")
raw = raw.replace(old, new, 1)
raw = raw.replace(
    '    image.setText(QStringLiteral("JixelLightSource"), QStringLiteral("RAW"));\n',
    '    image.setText(QStringLiteral("JixelLightSource"), QStringLiteral("RAW"));\n'
    '    image.setText(QStringLiteral("JixelLightProbeAdjustMaximumThr"), QString::number(params.adjust_maximum_thr, \'f\', 3));\n'
    '    image.setText(QStringLiteral("JixelLightProbeLibRawHighlight"), QString::number(params.highlight));\n',
    1,
)
raw_path.write_text(raw, encoding="utf-8")

pipeline_path = Path("core/pipeline/ImagePipeline.cpp")
pipeline = pipeline_path.read_text(encoding="utf-8")
if "#include <QtGlobal>\n" not in pipeline:
    include_anchor = "#include <QRgba64>\n"
    if include_anchor not in pipeline:
        raise SystemExit("ImagePipeline Qt include anchor not found")
    pipeline = pipeline.replace(include_anchor, include_anchor + "#include <QtGlobal>\n", 1)
old_gain = """    const float rawBaseGain = encoding == ImagePipeline::InputEncoding::LinearProPhoto\n        ? ProcessingPlan::RawBaseGain : 1.0f;\n"""
new_gain = """    float rawBaseGain = 1.0f;\n    if (encoding == ImagePipeline::InputEncoding::LinearProPhoto) {\n        bool ok = false;\n        float stops = qEnvironmentVariable(\"JIXELLIGHT_PROBE_RAW_BASE_EV\").toFloat(&ok);\n        if (!ok || !std::isfinite(stops)) stops = ProcessingPlan::RawBaseExposureStops;\n        rawBaseGain = std::exp2(std::clamp(stops, -4.0f, 6.0f));\n    }\n"""
if old_gain not in pipeline:
    raise SystemExit("ImagePipeline raw base gain probe anchor not found")
pipeline = pipeline.replace(old_gain, new_gain, 1)
pipeline = pipeline.replace(
    '    out.setText(QStringLiteral("JixelLightICCManaged"), QStringLiteral("true"));\n',
    '    out.setText(QStringLiteral("JixelLightICCManaged"), QStringLiteral("true"));\n'
    '    out.setText(QStringLiteral("JixelLightProbeRawBaseGain"), QString::number(plan.data[ProcessingPlan::Flags].w, \'f\', 6));\n',
    1,
)
pipeline_path.write_text(pipeline, encoding="utf-8")

print("alpha11 RAW A/B probe patch applied")
