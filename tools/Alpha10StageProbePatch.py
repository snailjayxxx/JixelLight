from pathlib import Path


def replace_once(text, old, new, label):
    count = text.count(old)
    if count != 1:
        raise SystemExit(f"{label}: expected exactly one marker, got {count}")
    return text.replace(old, new, 1)

# Temporary diagnostic API. The workflow applies this only in its checkout;
# this file itself is removed after the probe result is collected.
p = Path("core/pipeline/ImagePipeline.h")
t = p.read_text()
t = replace_once(t, '#include <QImage>\n', '#include <QImage>\n#include <QVector>\n#include <QVector3D>\n#include <QRgba64>\n', 'ImagePipeline includes')
t = replace_once(t,
'''    static QImage processWithPlan(const QImage &source, const ProcessingPlan &plan,\n                          const CancelToken &cancel = {}, bool parallel = true);\n''',
'''    static QImage processWithPlan(const QImage &source, const ProcessingPlan &plan,\n                          const CancelToken &cancel = {}, bool parallel = true);\n    // Temporary CI diagnostic: exact per-pixel CPU stages for alpha.10 parity.\n    static QVector<QVector3D> debugPixelStages(QRgba64 pixel, const AdjustmentState &state,\n                          InputEncoding encoding = InputEncoding::LinearProPhoto,\n                          ColorManagement::OutputSpace output = ColorManagement::OutputSpace::SRgb);\n''', 'ImagePipeline debug declaration')
p.write_text(t)

p = Path("core/pipeline/ImagePipeline.cpp")
t = p.read_text()
marker = 'ProcessingPlan ProcessingPlan::compile(const AdjustmentState &original, ImagePipeline::InputEncoding encoding,\n'
if marker not in t:
    raise SystemExit('ProcessingPlan compile marker missing')
debug_impl = r'''QVector<QVector3D> ImagePipeline::debugPixelStages(QRgba64 original, const AdjustmentState &state,
                                                    InputEncoding encoding, ColorManagement::OutputSpace output) {
    const ProcessingPlan plan = ProcessingPlan::compile(state, encoding, output);
    QVector<QVector3D> stages;
    auto push = [&](Vec3 v) { stages.append(QVector3D(v.x, v.y, v.z)); };
    Vec3 v{original.red()/65535.0f, original.green()/65535.0f, original.blue()/65535.0f};
    if (encoding == InputEncoding::SRgb) {
        v = {srgbToLinear(v.x), srgbToLinear(v.y), srgbToLinear(v.z)};
        v = multiply(plan.data.data()+ProcessingPlan::Input0, v);
    }
    push(v); // 1: decoded working input
    v = multiply(plan.data.data()+ProcessingPlan::Wb0, v);
    push(v); // 2: camera-neutral source + user WB/exposure
    const auto tone = plan.data[ProcessingPlan::Tone];
    const auto tonal = plan.data[ProcessingPlan::Tonal];
    const auto color = plan.data[ProcessingPlan::Color];
    const auto flags = plan.data[ProcessingPlan::Flags];
    const auto lum = plan.data[ProcessingPlan::Luminance];
    const auto style = plan.data[ProcessingPlan::LookStyle];
    v = applyHighlightRecovery(v, tone.y);
    if (color.w != 0) {
        const float Y = std::max(0.0f, proPhotoToXyzD50(v).y);
        const float stops = tonal.y*(1-smooth(Y/.32f)) + tonal.x*smooth((Y-.26f)/.82f)
                          + tonal.z*smooth((Y-.62f)/.70f)*.75f + tonal.w*(1-smooth(Y/.16f))*.75f;
        v = scale(v, std::exp2(stops));
    }
    if (flags.x == 0) {
        const float Y = std::max(0.0f, proPhotoToXyzD50(v).y);
        if (Y > 1.0e-6f) v = scale(v, middleGrayContrast(Y,tone.x)/Y);
    }
    push(v); // 3: tone zones + contrast
    v = scale(v, flags.w);
    push(v); // 4: Jixel Neutral RAW scene placement
    v = multiply(plan.data.data()+ProcessingPlan::Working0, v);
    push(v); // 5: linear sRGB before perceptual color
    if (color.z != 0) v = applyPerceptualColor(v, plan);
    else if (std::max({v.x,v.y,v.z}) > 3.3f || std::min({v.x,v.y,v.z}) < 0) {
        auto lab = linearSrgbToOklab(v); lab.L = std::clamp(lab.L,0.0f,plan.data[ProcessingPlan::LookOptions].w); v = oklabToLinearSrgb(lab);
    }
    push(v); // 6: perceptual color / Oklab round-trip
    if (style.x > 0) {
        const float y=(.2126f*v.x+.7152f*v.y)+.0722f*v.z;
        v=style.x>=1 ? Vec3{y,y,y} : Vec3{v.x+(y-v.x)*style.x,v.y+(y-v.y)*style.x,v.z+(y-v.z)*style.x};
        if(style.y>0)v={v.x*(1+.09f*style.y),v.y*(1-.01f*style.y),v.z*(1-.22f*style.y)};
    }
    v = multiply(plan.data.data()+ProcessingPlan::Out0, v);
    push(v); // 7: output-space linear RGB before gamut mapping
    v = compressNegativeGamut(v, lum);
    push(v); // 8: negative-gamut mapping
    const float recovery = tone.y;
    v = {displayShoulder(v.x,recovery),displayShoulder(v.y,recovery),displayShoulder(v.z,recovery)};
    push(v); // 9: display shoulder
    if (flags.y == 0) v = applyMasterCurve(v, state.masterCurve, lum);
    if (flags.z == 0) v = {curveSample(state.redCurve,v.x),curveSample(state.greenCurve,v.y),curveSample(state.blueCurve,v.z)};
    if(style.z>0)v={v.x*(1-style.z)+style.z,v.y*(1-style.z)+style.z,v.z*(1-style.z)+style.z};
    v = {encodeOutput(v.x,output),encodeOutput(v.y,output),encodeOutput(v.z,output)};
    push(v); // 10: encoded display RGB before 16-bit quantization
    return stages;
}

'''
t = t.replace(marker, debug_impl + marker, 1)
p.write_text(t)

# Preserve Dimensions.w as a diagnostic stage selector in the ephemeral build.
p = Path("core/gpu/GpuEngine.cpp")
t = p.read_text()
t = replace_once(t,
'        plan.data[ProcessingPlan::Dimensions] = {float(m_size.width()), float(m_size.height()), float(m_groups), 0};\n',
'        plan.data[ProcessingPlan::Dimensions] = {float(m_size.width()), float(m_size.height()), float(m_groups), plan.data[ProcessingPlan::Dimensions].w};\n',
'GpuEngine Dimensions')
p.write_text(t)

# Shader early-outs exposing the same stages as raw RGBA32F values.
p = Path("shaders/pipeline.comp")
t = p.read_text()
t = replace_once(t,
'    if(p[5].y==0.0) v=mulRows(25,vec3(decode(v.r),decode(v.g),decode(v.b)));\n    v=mulRows(0,v);\n',
'    if(p[5].y==0.0) v=mulRows(25,vec3(decode(v.r),decode(v.g),decode(v.b)));\n    if(int(p[24].w)==1){imageStore(outputImage,coord,vec4(v,original.a));return;}\n    v=mulRows(0,v);\n    if(int(p[24].w)==2){imageStore(outputImage,coord,vec4(v,original.a));return;}\n', 'shader stages 1/2')
t = replace_once(t,
'    if(p[10].x==0.0) { float y=max(0.0,luminancePro(v)); if(y>1e-6) v*=.18*pow(y/.18,p[4].x)/y; }\n',
'    if(p[10].x==0.0) { float y=max(0.0,luminancePro(v)); if(y>1e-6) v*=.18*pow(y/.18,p[4].x)/y; }\n    if(int(p[24].w)==3){imageStore(outputImage,coord,vec4(v,original.a));return;}\n', 'shader stage 3')
t = replace_once(t,
'    v*=p[10].w;\n    v=mulRows(28,v);\n',
'    v*=p[10].w;\n    if(int(p[24].w)==4){imageStore(outputImage,coord,vec4(v,original.a));return;}\n    v=mulRows(28,v);\n    if(int(p[24].w)==5){imageStore(outputImage,coord,vec4(v,original.a));return;}\n', 'shader stages 4/5')
t = replace_once(t,
'    else if(max(v.r,max(v.g,v.b))>3.3 || min(v.r,min(v.g,v.b))<0.0) { vec3 lab=toLab(v); lab.x=clamp(lab.x,0.0,p[33].w); v=fromLab(lab); }\n',
'    else if(max(v.r,max(v.g,v.b))>3.3 || min(v.r,min(v.g,v.b))<0.0) { vec3 lab=toLab(v); lab.x=clamp(lab.x,0.0,p[33].w); v=fromLab(lab); }\n    if(int(p[24].w)==6){imageStore(outputImage,coord,vec4(v,original.a));return;}\n', 'shader stage 6')
t = replace_once(t,
'    v=mulRows(6,v);\n    float y=max(0.0,dot3(p[9].xyz,v)),smallest=min(v.r,min(v.g,v.b));\n',
'    v=mulRows(6,v);\n    if(int(p[24].w)==7){imageStore(outputImage,coord,vec4(v,original.a));return;}\n    float y=max(0.0,dot3(p[9].xyz,v)),smallest=min(v.r,min(v.g,v.b));\n', 'shader stage 7')
t = replace_once(t,
'    v=max(v,vec3(0.0));\n    v=vec3(shoulder(v.r,recovery),shoulder(v.g,recovery),shoulder(v.b,recovery));\n',
'    v=max(v,vec3(0.0));\n    if(int(p[24].w)==8){imageStore(outputImage,coord,vec4(v,original.a));return;}\n    v=vec3(shoulder(v.r,recovery),shoulder(v.g,recovery),shoulder(v.b,recovery));\n    if(int(p[24].w)==9){imageStore(outputImage,coord,vec4(v,original.a));return;}\n', 'shader stages 8/9')
t = replace_once(t,
'    v=vec3(encode(v.r),encode(v.g),encode(v.b));\n',
'    v=vec3(encode(v.r),encode(v.g),encode(v.b));\n    if(int(p[24].w)==10){imageStore(outputImage,coord,vec4(v,original.a));return;}\n', 'shader stage 10')
p.write_text(t)

# Add a diagnostic test that compares raw float stage values for the exact
# historical worst pixel. No production acceptance threshold is changed.
p = Path("tests/GpuTests.cpp")
t = p.read_text()
insert_after = '''    struct Result { QImage image; ScopesResult histogram; };\n'''
probe_helper = r'''    QVector3D renderProbe(const QImage &input,const AdjustmentState &state,int stage) {
        QRhiCommandBuffer *cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        const auto floatImage=input.convertToFormat(QImage::Format_RGBA32FPx4);
        auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        plan.data[ProcessingPlan::Dimensions].w=float(stage);
        const bool ok=engine->process(cb,floatImage,plan,++revision,GpuEngine::HistogramReady{},false);
        QRhiReadbackResult readback; bool done=false;
        if(ok) {
            readback.completed=[&]{done=true;};
            auto *updates=rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);
            cb->resourceUpdate(updates);
        }
        rhi->endOffscreenFrame(); rhi->finish();
        if(!ok || !done || readback.data.size()<16) return {};
        const auto *f=reinterpret_cast<const float *>(readback.data.constData());
        return QVector3D(f[0],f[1],f[2]);
    }
'''
t = replace_once(t, insert_after, insert_after + probe_helper, 'GpuTests helper insert')
slot_marker = '''    void pixelsAndHistogramMatchCpu() {\n'''
slot = r'''    void stageProbe() {
        const QRgba64 px=QRgba64::fromRgba64(28853,59865,40358,65535);
        QImage image(1,1,QImage::Format_RGBA64);
        reinterpret_cast<QRgba64 *>(image.scanLine(0))[0]=px;
        AdjustmentState state;
        state.exposure=3.0; state.saturation=-20; state.blacks=-50; state.whites=60;
        const auto cpu=ImagePipeline::debugPixelStages(px,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        QCOMPARE(cpu.size(),10);
        const char *names[]{"decoded","wb-exposure","tone-zones","raw-base","working-srgb","oklab-color","output-linear","gamut","shoulder","encoded"};
        for(int stage=1;stage<=10;++stage) {
            const QVector3D gpu=renderProbe(image,state,stage);
            const QVector3D c=cpu[stage-1];
            const QVector3D d=gpu-c;
            qInfo().noquote()<<QString("PROBE %1 %2 CPU %.9g %.9g %.9g GPU %.9g %.9g %.9g DELTA %.9g %.9g %.9g")
                .arg(stage).arg(names[stage-1])
                .arg(c.x()).arg(c.y()).arg(c.z()).arg(gpu.x()).arg(gpu.y()).arg(gpu.z())
                .arg(d.x()).arg(d.y()).arg(d.z());
            QVERIFY(std::isfinite(gpu.x())&&std::isfinite(gpu.y())&&std::isfinite(gpu.z()));
        }
    }
'''
t = replace_once(t, slot_marker, slot + slot_marker, 'GpuTests stage slot')
p.write_text(t)
