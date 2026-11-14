#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QScopeGuard>
#include <QRgba64>
#include <random>
#include <cstring>
#include <cmath>
#include <limits>
#include "core/gpu/GpuEngine.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include "core/pipeline/FloatFrameFingerprint.h"
#include "core/pipeline/FloatSceneResampler.h"
#include "core/color/MonitorColorTransform.h"
#include "core/scopes/ScopesEngine.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include "core/cache/SourceCache.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QFile>
#include <QScopeGuard>
#include <QDir>
#include <QJsonDocument>
#include <QFutureWatcher>
#include <QtConcurrent>
#include "core/gpu/brush/NativeBrushFrame.h"

class GpuTests : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    quint64 revision=0;
    struct Result { QImage image; ScopesResult histogram; QJsonObject diagnostic, independentFingerprint; };
    Result render(const QImage &input,const AdjustmentState &state,bool statistics=true,
                  ImagePipeline::InputEncoding encoding=ImagePipeline::InputEncoding::LinearProPhoto,
                  ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb,const QRectF &frameRect={0,0,1,1},bool diagnostic=false,QSize referenceFrameSize={},const std::array<Float4,3> *inputRows=nullptr) {
        QRhiCommandBuffer *cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        QByteArray counts;
        const auto floatImage=ImagePipeline::floatSource(input);
        const auto callback=statistics ? GpuEngine::HistogramReady([&](quint64,QByteArray data,quint64){ counts=std::move(data); }) : GpuEngine::HistogramReady{};
        auto plan=ProcessingPlan::compile(state,encoding,output); plan.setReferenceFrameSize(referenceFrameSize);plan.setFrameRect(frameRect);
        if(inputRows)std::copy(inputRows->begin(),inputRows->end(),plan.data.begin()+ProcessingPlan::Input0);
        // These owned fixtures represent complete corrected frames; density/viewport metadata is simulated.
        if(plan.dehazeActive())plan.setDehazeAirlight(ImagePipeline::estimateDehazeAirlight(input,plan));
        const bool ok=engine->process(cb,floatImage,plan,++revision,callback,true);
        QJsonObject captured;
        if (ok && diagnostic && !engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){captured=std::move(value);})) {
            rhi->endOffscreenFrame(); rhi->finish(); return {};
        }
        QRhiReadbackResult readback; bool done=false;
        if (ok) {
            readback.completed=[&] { done=true; };
            auto *updates=rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);
            cb->resourceUpdate(updates);
        }
        rhi->endOffscreenFrame(); rhi->finish();
        if (!ok) { qWarning()<<engine->error(); return {}; }
        if (!done || readback.data.size()!=input.width()*input.height()*16) return {};
        // Check before integer conversion, which could otherwise conceal NaNs.
        const auto *values=reinterpret_cast<const float *>(readback.data.constData());
        for(qsizetype i=0;i<readback.data.size()/qsizetype(sizeof(float));++i)
            if(!std::isfinite(values[i])) { qWarning()<<"Non-finite GPU result at"<<i;return {}; }
        QImage image(reinterpret_cast<const uchar *>(readback.data.constData()),input.width(),input.height(),input.width()*16,QImage::Format_RGBA32FPx4);
        return {ImagePipeline::rgba64Source(image),ScopesEngine::fromGpu(counts,quint64(input.width())*input.height()),
                captured,diagnostic?FloatFrameFingerprint::capture(image):QJsonObject{}};
    }
    void verifyParity(const QImage &input,const AdjustmentState &state,
                      ImagePipeline::InputEncoding encoding,ColorManagement::OutputSpace space,
                      bool statistics,const char *label,const QRectF &frameRect={0,0,1,1},QSize referenceFrameSize={},const std::array<Float4,3> *inputRows=nullptr) {
        const auto actual=render(input,state,statistics,encoding,space,frameRect,false,referenceFrameSize,inputRows);
        QVERIFY2(!actual.image.isNull(),qPrintable(engine->error()));
        auto plan=ProcessingPlan::compile(state,encoding,space);plan.setReferenceFrameSize(referenceFrameSize);plan.setFrameRect(frameRect);
        if(inputRows)std::copy(inputRows->begin(),inputRows->end(),plan.data.begin()+ProcessingPlan::Input0);
        // These owned fixtures represent complete corrected frames; density/viewport metadata is simulated.
        if(plan.dehazeActive())plan.setDehazeAirlight(ImagePipeline::estimateDehazeAirlight(input,plan));
        const auto reference=ImagePipeline::processWithPlan(input,plan);
        int maximum=0,worstX=0,worstY=0;
        quint64 absolute=0,overTolerance=0;
        for(int y=0;y<input.height();++y) {
            const auto *a=reinterpret_cast<const QRgba64 *>(actual.image.constScanLine(y));
            const auto *b=reinterpret_cast<const QRgba64 *>(reference.constScanLine(y));
            for(int x=0;x<input.width();++x) {
                const int dr=std::abs(int(a[x].red())-int(b[x].red()));
                const int dg=std::abs(int(a[x].green())-int(b[x].green()));
                const int db=std::abs(int(a[x].blue())-int(b[x].blue()));
                const int delta=std::max({dr,dg,db});
                absolute+=dr+dg+db;
                if(delta>40) ++overTolerance;
                if(delta>maximum) { maximum=delta;worstX=x;worstY=y; }
                QCOMPARE(a[x].alpha(),b[x].alpha());
            }
        }
        const auto original=input.pixelColor(worstX,worstY).rgba64();
        const auto cpu=reference.pixelColor(worstX,worstY).rgba64();
        const auto gpu=actual.image.pixelColor(worstX,worstY).rgba64();
        qInfo()<<label<<"encoding"<<int(encoding)<<"output"<<int(space)
               <<"max"<<maximum<<"mean"<<double(absolute)/(input.width()*input.height()*3)
               <<"pixels over 40"<<overTolerance<<"at"<<worstX<<worstY
               <<"input"<<original.red()<<original.green()<<original.blue()
               <<"CPU"<<cpu.red()<<cpu.green()<<cpu.blue()<<"GPU"<<gpu.red()<<gpu.green()<<gpu.blue();
        // This is the original bound. Do not make platform-specific exceptions.
        QVERIFY2(maximum<=40,qPrintable(QString::number(maximum)));
        if(statistics) {
            const auto expected=ScopesEngine::analyze(actual.image);
            QCOMPARE(actual.histogram.pixelCount,quint64(input.width())*input.height());
            QCOMPARE(actual.histogram.red,expected.red);
            QCOMPARE(actual.histogram.green,expected.green);
            QCOMPARE(actual.histogram.blue,expected.blue);
            QCOMPARE(actual.histogram.luma,expected.luma);
            QCOMPARE(actual.histogram.shadowClipPercent,expected.shadowClipPercent);
            QCOMPARE(actual.histogram.highlightClipPercent,expected.highlightClipPercent);
        }
    }
private slots:
    void clarityMatchesReferenceAcrossSourcesAndDensities() {
        QImage integer(137,139,QImage::Format_RGBA64),floating(137,139,QImage::Format_RGBA32FPx4);
        const quint16 alpha16[]{0,655,24576,65535};const float alpha32[]{0,1.e-8f,.375f,1.f};
        for(int y=0;y<139;++y)for(int x=0;x<137;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,alpha16[(x+y)%4]);
            auto *pixel=reinterpret_cast<float *>(floating.scanLine(y))+4*x;
            pixel[0]=-.04f+2.f*x/136.f;pixel[1]=.02f+1.5f*y/138.f;pixel[2]=.1f+1.2f*((x+y)%101)/100.f;pixel[3]=alpha32[(x+y)%4];
        }
        int compute=0,cpu=0;
        for(bool fp32:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)
        for(double amount:{-73.,87.})for(bool preview:{false,true}) {
            const auto &source=fp32?floating:integer;AdjustmentState state;state.clarityAmount=amount;
            state.denoiseLuma=71;state.denoiseChroma=83;state.sharpenAmount=61;state.grainAmount=19;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",3},{"clarity",2}};
            const auto space=ColorManagement::OutputSpace(output);const auto input=ImagePipeline::InputEncoding(encoding);
            const auto frame=preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);const auto reference=preview?source.size()*4:QSize{};
            verifyParity(source,state,input,space,true,"spatial clarity",frame,reference);if(QTest::currentTestFailed())return;
            const bool fallback=space==ColorManagement::OutputSpace::AdobeRgb||(fp32&&encoding==1&&space==ColorManagement::OutputSpace::ProPhotoRgb);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);fallback?++cpu:++compute;
            const auto actual=render(source,state,false,input,space,frame,true,reference);
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["spatial_clarity"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
        }
        QCOMPARE(compute,44);QCOMPARE(cpu,20);qInfo()<<"clarity64: actual compute"<<compute<<"explicit CPU policies"<<cpu;
    }
    void clarityDarkFieldsFractionalRadiiAndAlphaHoles() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);const float alpha[]{0,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.001f+.015f*float((x*17+y*7)%23)/23;p[1]=.003f+.01f*float((x+y)%13)/13;p[2]=.008f;p[3]=alpha[(x+y)%4];
            if(p[3]==0){p[0]=64000;p[1]=32000;p[2]=16000;}
        }
        for(int output=0;output<4;++output)for(double amount:{-100.,100.})for(bool denoise:{false,true})for(bool tiny:{false,true}) {
            AdjustmentState state;state.clarityAmount=amount;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"clarity dark/holes",{0,0,1,1},source.size()*(tiny?40:2));
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void clarityResidentStagesTrackCompleteDependencies() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.15f+.03f*std::sin(float(x));p[1]=.2f+.02f*std::cos(float(y));p[2]=.12f;p[3]=1;}
        AdjustmentState state;state.clarityAmount=73;QSize reference;
        auto encoding=ImagePipeline::InputEncoding::LinearProPhoto;auto outputSpace=ColorManagement::OutputSpace::SRgb;
        std::array<Float4,3> inputRows{};const std::array<Float4,3> *customRows=nullptr;
        const auto verify=[&]{verifyParity(source,state,encoding,outputSpace,true,"clarity cache",{0,0,1,1},reference,customRows);QVERIFY(!engine->lastProcessUsedCpuFallback());};
        verify();auto guide=engine->clarityGuideDispatches(),horizontal=engine->clarityHorizontalDispatches(),filter=engine->clarityFilterDispatches(),uploads=engine->sourceUploads();
        state.exposure=.3;state.temperature=17;verify();QCOMPARE(engine->clarityGuideDispatches(),guide);QCOMPARE(engine->clarityHorizontalDispatches(),horizontal);QCOMPARE(engine->clarityFilterDispatches(),filter);
        state.clarityAmount=-49;verify();QCOMPARE(engine->clarityGuideDispatches(),guide);QCOMPARE(engine->clarityHorizontalDispatches(),horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        reference=source.size()*4;verify();QCOMPARE(engine->clarityGuideDispatches(),guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        state.denoiseLuma=77;state.denoiseChroma=83;verify();QCOMPARE(engine->clarityGuideDispatches(),++guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        state.denoiseLuma=91;verify();QCOMPARE(engine->clarityGuideDispatches(),++guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        state.sharpenAmount=61;state.grainAmount=19;verify();QCOMPARE(engine->clarityGuideDispatches(),guide);QCOMPARE(engine->clarityHorizontalDispatches(),horizontal);QCOMPARE(engine->clarityFilterDispatches(),filter);QCOMPARE(engine->sourceUploads(),uploads);
        auto next=source.copy();source=next;state.clarityAmount=0;state.denoiseLuma=0;state.denoiseChroma=0;verify();QCOMPARE(engine->clarityGuideDispatches(),guide);
        state.clarityAmount=73;verify();QCOMPARE(engine->clarityGuideDispatches(),++guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);QCOMPARE(engine->sourceUploads(),uploads+1);
        source=source.copy(0,0,67,71);reference={};verify();QCOMPARE(engine->clarityGuideDispatches(),++guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        encoding=ImagePipeline::InputEncoding::SRgb;verify();QCOMPARE(engine->clarityGuideDispatches(),++guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        const auto matrixPlan=ProcessingPlan::compile(state,encoding,outputSpace);std::copy_n(matrixPlan.data.data()+ProcessingPlan::Input0,3,inputRows.data());inputRows[0].x+=.01f;customRows=&inputRows;
        verify();QCOMPARE(engine->clarityGuideDispatches(),++guide);QCOMPARE(engine->clarityHorizontalDispatches(),++horizontal);QCOMPARE(engine->clarityFilterDispatches(),++filter);
        outputSpace=ColorManagement::OutputSpace::DisplayP3;verify();QCOMPARE(engine->clarityGuideDispatches(),guide);QCOMPARE(engine->clarityHorizontalDispatches(),horizontal);QCOMPARE(engine->clarityFilterDispatches(),filter);
    }
    void clarityRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const auto source=loaded.image.copy((loaded.image.width()-512)/2,(loaded.image.height()-512)/2,512,512);
        for(int output=0;output<4;++output)for(bool denoise:{false,true}) {
            AdjustmentState state;state.clarityAmount=65;if(denoise){state.denoiseLuma=70;state.denoiseChroma=90;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"real Sony clarity");
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void clarityUsesExplicitCompleteCpuReferencePolicy() {
        QImage source(39,41,QImage::Format_RGBA32FPx4);
        const float alpha[]{0,std::numeric_limits<float>::denorm_min(),1.e-19f,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.2f+.08f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=alpha[(x+y)%6];
        }
        for(int output=0;output<4;++output)for(double amount:{-100.,73.})for(bool denoise:{false,true}) {
            AdjustmentState state;state.clarityAmount=amount;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            state.sharpenAmount=61;state.grainAmount=19;
            const auto space=ColorManagement::OutputSpace(output);const auto actual=render(source,state,true,ImagePipeline::InputEncoding::LinearProPhoto,space,{0,0,1,1},true);
            QVERIFY(!actual.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
            const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space));
            for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);
            QCOMPARE(actual.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
            QCOMPARE(actual.diagnostic["spatial_clarity"].toString(),QString("cpu-reference"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),QString("clarity-near-zero-alpha"));
            const auto counts=ScopesEngine::analyze(expected);QCOMPARE(actual.histogram.red,counts.red);QCOMPARE(actual.histogram.green,counts.green);QCOMPARE(actual.histogram.blue,counts.blue);QCOMPARE(actual.histogram.luma,counts.luma);
        }
    }

    void colorMaskMatchesReferenceWithBothRangeMasksAndAllSpatialStages() {
        QImage source(47,43,QImage::Format_RGBA32FPx4);const float alpha[]{0,1.e-8f,.4f,1};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {auto *q=reinterpret_cast<float *>(source.scanLine(y))+4*x;q[0]=-.04f+2.f*x/46;q[1]=.02f+1.5f*y/42;q[2]=.2f;q[3]=alpha[(x+y)%4];}
        int cases=0,compute=0,cpu=0;
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)for(double exposure:{-2.7,1.9})for(bool inverted:{false,true}) {
            AdjustmentState state;state.colorMaskEnabled=true;state.colorMaskExposure=exposure;state.colorMaskInverted=inverted;state.colorMaskLab={.6,.02,.01};state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;state.luminanceMaskEnabled=true;state.luminanceMaskExposure=.4;
            state.denoiseLuma=71;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=57;state.clarityAmount=-41;state.sharpenAmount=61;state.grainAmount=19;state.pointColorEnabled=true;state.pointColorHue=39;
            const auto input=integer?ImagePipeline::rgba64Source(source):source;const auto space=ColorManagement::OutputSpace(output);const auto enc=ImagePipeline::InputEncoding(encoding);
            verifyParity(input,state,enc,space,true,"color range mask full stack");if(QTest::currentTestFailed())return;
            const bool fallback=output==2||(!integer&&encoding==1&&output==3);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);fallback?++cpu:++compute;
            const auto actual=render(input,state,false,enc,space,{0,0,1,1},true);
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            for(const auto key:{"color_mask","luminance_mask"})QCOMPARE(actual.diagnostic[key].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),fallback?(output==3?QString("signed-linear-prophoto"):QString("existing-color-or-brush")):QString("none"));
            if(fallback){const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,enc,space));for(int y=0;y<input.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(input.width())*8)==0);}
            ++cases;
        }
        QCOMPARE(cases,64);QCOMPARE(compute,44);QCOMPARE(cpu,20);
    }
    void colorMaskNarrowRangesHueSeamsAndNeutrals() {
        constexpr float Pi=3.14159265358979323846f;int compute=0,cpu=0;
        for(const auto center : {PointColor::Lab{.62f,.08f,359.f},PointColor::Lab{.035f,.012f,179.f},PointColor::Lab{.6f,0,0}})
        for(int range=0;range<13;++range) {
            const float rL=(range==0||range==3)?.001f:range==1?.003f:.4f,rC=(range==0||range==4)?.001f:range==1?.02f:.12f,rH=(range==0||range==5)?1:range==6?2:range==7?3:range==8?5:range==9?10:range==10?20:range==11?44.9f:range==2?180:45;
            const float angle=center.b*(Pi/180.f);QImage source(31,29,QImage::Format_RGBA32FPx4);
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
                const float t=float((x*7+y*11)%101)/100.f;
                const float L=center.L+rL*(t*2-1),C=std::max(0.f,center.a+rC*(float(x)/30.f*2-1));
                const float h=(center.b+rH*(float(y)/28.f*2-1))*(Pi/180.f);
                const auto rgb=PointColor::toEncodedSrgb({L,C*std::cos(h),C*std::sin(h)});auto *q=reinterpret_cast<float *>(source.scanLine(y))+4*x;
                for(int c=0;c<3;++c)q[c]=rgb[c];const float alpha[]{0,1.e-9f,1.f/65535.f,.8f,1};q[3]=alpha[(x+y)%5];
            }
            for(int output=0;output<4;++output)for(double exposure:{-3.,3.})for(bool inverted:{false,true}) {
                AdjustmentState state;state.colorMaskEnabled=true;state.colorMaskExposure=exposure;state.colorMaskInverted=inverted;
                state.colorMaskLab={center.L,center.a*std::cos(angle),center.a*std::sin(angle)};
                state.colorMaskHueRange=rH;state.colorMaskChromaRange=rC;state.colorMaskLightnessRange=rL;
                const auto label=QString("Color mask narrow/seam/neutral center(%1,%2,%3) H%4 C%5 L%6 EV%7 invert%8").arg(center.L).arg(center.a).arg(center.b).arg(rH).arg(rC).arg(rL).arg(exposure).arg(inverted).toUtf8();
                verifyParity(source,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),true,label.constData());
                if(QTest::currentTestFailed())return;
                const bool precise=range==0||range==3||range==4||range==5||range==6||range==7||range==8||range==9||range==10||range==11;
                const bool fallback=precise||output==2;fallback?++cpu:++compute;QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
                const auto actual=render(source,state,false,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),{0,0,1,1},true);
                QCOMPARE(actual.diagnostic["color_mask"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
                QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),precise?QString("color-mask-narrow-range"):output==2?QString("color-mask-adobe-boundary"):QString("none"));
                if(fallback){const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output)));for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);}
            }
        }
        QCOMPARE(compute,108);QCOMPARE(cpu,516);qInfo()<<"Color mask624 ranges: compute"<<compute<<"exact CPU precision"<<cpu;
    }
    void colorMaskEditsKeepEveryResidentSpatialStageAndSource() {
        QImage source(83,89,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.2f+.03f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=(x+y)%3?.7f:0;}
        AdjustmentState state;state.denoiseLuma=71;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=61;state.clarityAmount=37;
        const auto counters=[&]{return std::array<quint64,15>{engine->sourceUploads(),engine->denoiseGuideDispatches(),engine->denoiseFilterDispatches(),engine->dehazeGuideDispatches(),engine->dehazeHorizontalDispatches(),engine->dehazeVerticalDispatches(),engine->dehazeFilterDispatches(),engine->textureGuideDispatches(),engine->textureSmallHorizontalDispatches(),engine->textureSmallVerticalDispatches(),engine->textureLargeHorizontalDispatches(),engine->textureFilterDispatches(),engine->clarityGuideDispatches(),engine->clarityHorizontalDispatches(),engine->clarityFilterDispatches()};};
        const auto verify=[&]{verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"color mask resident stages");QVERIFY(!engine->lastProcessUsedCpuFallback());};
        verify();const auto initial=counters();
        for(int i=0;i<18;++i){state.colorMaskEnabled=i%5!=0;state.colorMaskExposure=(i%2?-1:1)*(.1+i*.15);state.colorMaskInverted=i%3==0;state.colorMaskLab={.5+i*.01,.02-i*.001,.03};state.colorMaskHueRange=45+i*7;state.colorMaskChromaRange=.01+i*.01;state.colorMaskLightnessRange=.01+i*.02;state.luminanceMaskEnabled=true;state.luminanceMaskExposure=.4;verify();if(QTest::currentTestFailed())return;QCOMPARE(counters(),initial);}
        const auto wideRange=state.colorMaskHueRange;state.colorMaskHueRange=1;
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"color mask resident precision fallback");QVERIFY(engine->lastProcessUsedCpuFallback());QCOMPARE(counters(),initial);
        state.colorMaskHueRange=wideRange;verify();QCOMPARE(counters(),initial);
        state.pointColorEnabled=true;state.pointColorHue=30;state.pointColorHueRange=1;
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"color mask inherited precision fallback");QVERIFY(engine->lastProcessUsedCpuFallback());QCOMPARE(counters(),initial);
        state.pointColorEnabled=false;verify();QCOMPARE(counters(),initial);
    }
    void colorMaskRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const auto source=loaded.image.copy((loaded.image.width()-512)/2,(loaded.image.height()-512)/2,512,512);
        for(int output=0;output<4;++output)for(bool denoise:{false,true}) {
            AdjustmentState state;state.colorMaskEnabled=true;state.colorMaskExposure=-1.9;state.colorMaskLab={.6,.02,.01};state.colorMaskHueRange=180;state.colorMaskChromaRange=1;state.colorMaskLightnessRange=1;state.colorMaskInverted=denoise;
            state.dehazeAmount=65;if(denoise){state.denoiseLuma=70;state.denoiseChroma=90;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"real Sony color mask");
            if(QTest::currentTestFailed())return;QCOMPARE(engine->lastProcessUsedCpuFallback(),output==2);
        }
    }
    void colorMaskRetainsExplicitSourceAndBrushCpuPolicies() {
        QImage source(39,41,QImage::Format_RGBA32FPx4);
        const float alpha[]{0,1.e-8f,1.f/65535.f,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.2f+.08f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=alpha[(x+y)%6];}
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)for(double exposure:{-3.,3.}) {
            AdjustmentState state;state.colorMaskEnabled=true;state.colorMaskExposure=exposure;state.colorMaskLab={.6,.02,.01};state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;
            state.denoiseLuma=77;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=57;state.clarityAmount=-41;
            auto input=integer?ImagePipeline::rgba64Source(source):source;
            if(!integer)reinterpret_cast<float *>(input.scanLine(0))[3]=std::numeric_limits<float>::denorm_min();
            if(integer){BrushMaskGeometry::Stroke paint;paint.style={.2,.4,.8};paint.dabs={{{.5,.5},.7}};state.brushMasks.layers={{"color-mask-reference",true,.6,{paint}}};}
            const auto space=ColorManagement::OutputSpace(output);const auto actual=render(input,state,true,ImagePipeline::InputEncoding(encoding),space,{0,0,1,1},true);
            QVERIFY(!actual.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
            const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),space));
            for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);
            QCOMPARE(actual.diagnostic["producer"].toString(),QString("cpu-reference-upload"));QCOMPARE(actual.diagnostic["color_mask"].toString(),QString("cpu-reference"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),integer?QString("existing-color-or-brush"):QString("dehaze-near-zero-alpha"));
            const auto counts=ScopesEngine::analyze(expected);QCOMPARE(actual.histogram.red,counts.red);QCOMPARE(actual.histogram.green,counts.green);QCOMPARE(actual.histogram.blue,counts.blue);QCOMPARE(actual.histogram.luma,counts.luma);
        }
    }
    void colorMaskComposesExistingBrightPerceptualPolicy() {
        QImage source(41,37,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.1f+.1f*x/40;p[1]=.15f+.1f*y/36;p[2]=.1f;p[3]=(x+y)%3?.7f:0;}
        const bool restricted=rhi->backend()==QRhi::D3D11||rhi->backend()==QRhi::OpenGLES2;
        for(int output=0;output<4;++output)for(double exposure:{-3.,2.})for(bool inverted:{false,true}) {
            AdjustmentState state;state.exposure=.5;state.temperature=17;state.tint=-7;state.saturation=13;state.vibrance=12;
            state.colorMaskEnabled=true;state.colorMaskExposure=exposure;state.colorMaskLab={.6,.02,.01};state.colorMaskHueRange=90;state.colorMaskChromaRange=.12;state.colorMaskLightnessRange=.4;state.colorMaskInverted=inverted;
            const auto space=ColorManagement::OutputSpace(output);
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,"color mask bright perceptual policy");if(QTest::currentTestFailed())return;
            const bool fallback=output==2||(restricted&&exposure>0);QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
            const auto actual=render(source,state,false,ImagePipeline::InputEncoding::LinearProPhoto,space,{0,0,1,1},true);
            QCOMPARE(actual.diagnostic["color_mask"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
            if(fallback){const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space));for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);}
        }
    }
    void luminanceMaskMatchesReferenceAcrossSourcesAndOutputs() {
        QImage source(47,43,QImage::Format_RGBA32FPx4);const float alpha[]{0,1.e-8f,.4f,1};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {auto *q=reinterpret_cast<float *>(source.scanLine(y))+4*x;q[0]=-.04f+2.f*x/46;q[1]=.02f+1.5f*y/42;q[2]=.2f;q[3]=alpha[(x+y)%4];}
        int cases=0,compute=0,cpu=0;
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)for(double exposure:{-2.7,1.9})for(bool inverted:{false,true}) {
            AdjustmentState state;state.luminanceMaskEnabled=true;state.luminanceMaskExposure=exposure;state.luminanceMaskInverted=inverted;state.luminanceMaskLower=.04;state.luminanceMaskUpper=.3;state.luminanceMaskFeather=.03;
            state.denoiseLuma=71;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=57;state.clarityAmount=-41;state.sharpenAmount=61;state.grainAmount=19;state.pointColorEnabled=true;state.pointColorHue=39;
            const auto input=integer?ImagePipeline::rgba64Source(source):source;const auto space=ColorManagement::OutputSpace(output);const auto enc=ImagePipeline::InputEncoding(encoding);
            verifyParity(input,state,enc,space,true,"luminance mask full stack");if(QTest::currentTestFailed())return;
            const bool fallback=output==2||(!integer&&encoding==1&&output==3);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);fallback?++cpu:++compute;
            const auto actual=render(input,state,false,enc,space,{0,0,1,1},true);
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["luminance_mask"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),fallback?(output==3?QString("signed-linear-prophoto"):QString("existing-color-or-brush")):QString("none"));
            if(fallback){const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,enc,space));for(int y=0;y<input.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(input.width())*8)==0);}
            ++cases;
        }
        QCOMPARE(cases,64);QCOMPARE(compute,44);QCOMPARE(cpu,20);
    }
    void luminanceMaskNarrowShouldersSignedHdrAndAlphaHoles() {
        int cases=0;
        for(double feather:{.001,.003,.03,.2})for(double lower:{0.,.1,1.5}) {
            QImage source(53,47,QImage::Format_RGBA32FPx4);
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
                auto *q=reinterpret_cast<float *>(source.scanLine(y))+4*x;
                const float target=float(lower)+(y%2?.1f:0)+float(feather)*(float(x)/52*4-2);
                q[0]=-.3f+2.f*y/46;q[2]=.2f;
                q[1]=(target-.2880402f*q[0]-.0000857f*q[2])/.7118741f;
                const float alpha[]{0,1.e-9f,1.f/65535.f,.8f,1};q[3]=alpha[(x+y)%5];
            }
            for(int output=0;output<4;++output)for(double exposure:{-3.,3.})for(bool invert:{false,true}) {
                AdjustmentState state;state.luminanceMaskEnabled=true;state.luminanceMaskExposure=exposure;
                state.luminanceMaskLower=lower;state.luminanceMaskUpper=lower+.1;state.luminanceMaskFeather=feather;state.luminanceMaskInverted=invert;
                state.exposure=.3;state.temperature=12;state.tint=-7;
                verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"luminance narrow/signed/HDR");
                if(QTest::currentTestFailed())return;QCOMPARE(engine->lastProcessUsedCpuFallback(),output==3);++cases;
            }
        }
        QCOMPARE(cases,192);
    }
    void luminanceMaskEditsKeepEveryResidentSpatialStageAndSource() {
        QImage source(83,89,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.2f+.03f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=(x+y)%3?.7f:0;}
        AdjustmentState state;state.denoiseLuma=71;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=61;state.clarityAmount=37;
        const auto counters=[&]{return std::array<quint64,15>{engine->sourceUploads(),engine->denoiseGuideDispatches(),engine->denoiseFilterDispatches(),engine->dehazeGuideDispatches(),engine->dehazeHorizontalDispatches(),engine->dehazeVerticalDispatches(),engine->dehazeFilterDispatches(),engine->textureGuideDispatches(),engine->textureSmallHorizontalDispatches(),engine->textureSmallVerticalDispatches(),engine->textureLargeHorizontalDispatches(),engine->textureFilterDispatches(),engine->clarityGuideDispatches(),engine->clarityHorizontalDispatches(),engine->clarityFilterDispatches()};};
        const auto verify=[&]{verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"luminance resident stages");QVERIFY(!engine->lastProcessUsedCpuFallback());};
        verify();const auto initial=counters();
        for(int i=0;i<18;++i){state.luminanceMaskEnabled=i%5!=0;state.luminanceMaskExposure=(i%2?-1:1)*(.1+i*.15);state.luminanceMaskInverted=i%3==0;state.luminanceMaskLower=.01+i*.01;state.luminanceMaskUpper=.3+i*.02;state.luminanceMaskFeather=.001+i*.01;verify();if(QTest::currentTestFailed())return;QCOMPARE(counters(),initial);}
        state.pointColorEnabled=true;state.pointColorHue=30;state.pointColorHueRange=1;
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"luminance inherited precision fallback");QVERIFY(engine->lastProcessUsedCpuFallback());QCOMPARE(counters(),initial);
        state.pointColorEnabled=false;verify();QCOMPARE(counters(),initial);
    }
    void luminanceMaskRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const auto source=loaded.image.copy((loaded.image.width()-512)/2,(loaded.image.height()-512)/2,512,512);
        for(int output=0;output<4;++output)for(bool denoise:{false,true}) {
            AdjustmentState state;state.luminanceMaskEnabled=true;state.luminanceMaskExposure=-1.9;state.luminanceMaskLower=.08;state.luminanceMaskUpper=.35;state.luminanceMaskFeather=.03;state.luminanceMaskInverted=denoise;
            state.dehazeAmount=65;if(denoise){state.denoiseLuma=70;state.denoiseChroma=90;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"real Sony luminance mask");
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void luminanceMaskRetainsExplicitSourceAndBrushCpuPolicies() {
        QImage source(39,41,QImage::Format_RGBA32FPx4);
        const float alpha[]{0,1.e-8f,1.f/65535.f,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.2f+.08f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=alpha[(x+y)%6];}
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)for(double exposure:{-3.,3.}) {
            AdjustmentState state;state.luminanceMaskEnabled=true;state.luminanceMaskExposure=exposure;state.luminanceMaskLower=.1;state.luminanceMaskUpper=.3;state.luminanceMaskFeather=.001;
            state.denoiseLuma=77;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=57;state.clarityAmount=-41;
            auto input=integer?ImagePipeline::rgba64Source(source):source;
            if(!integer)reinterpret_cast<float *>(input.scanLine(0))[3]=std::numeric_limits<float>::denorm_min();
            if(integer){BrushMaskGeometry::Stroke paint;paint.style={.2,.4,.8};paint.dabs={{{.5,.5},.7}};state.brushMasks.layers={{"luminance-reference",true,.6,{paint}}};}
            const auto space=ColorManagement::OutputSpace(output);const auto actual=render(input,state,true,ImagePipeline::InputEncoding(encoding),space,{0,0,1,1},true);
            QVERIFY(!actual.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
            const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),space));
            for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);
            QCOMPARE(actual.diagnostic["producer"].toString(),QString("cpu-reference-upload"));QCOMPARE(actual.diagnostic["luminance_mask"].toString(),QString("cpu-reference"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),integer?QString("existing-color-or-brush"):QString("dehaze-near-zero-alpha"));
            const auto counts=ScopesEngine::analyze(expected);QCOMPARE(actual.histogram.red,counts.red);QCOMPARE(actual.histogram.green,counts.green);QCOMPARE(actual.histogram.blue,counts.blue);QCOMPARE(actual.histogram.luma,counts.luma);
        }
    }
    void luminanceMaskComposesExistingBrightPerceptualPolicy() {
        QImage source(41,37,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.1f+.1f*x/40;p[1]=.15f+.1f*y/36;p[2]=.1f;p[3]=(x+y)%3?.7f:0;}
        const bool restricted=rhi->backend()==QRhi::D3D11||rhi->backend()==QRhi::OpenGLES2;
        for(int output=0;output<4;++output)for(double exposure:{-3.,2.})for(bool inverted:{false,true}) {
            AdjustmentState state;state.exposure=.5;state.saturation=13;state.vibrance=12;
            state.luminanceMaskEnabled=true;state.luminanceMaskExposure=exposure;state.luminanceMaskLower=.1;state.luminanceMaskUpper=.3;state.luminanceMaskFeather=.02;state.luminanceMaskInverted=inverted;
            const auto space=ColorManagement::OutputSpace(output);
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,"luminance bright perceptual policy");if(QTest::currentTestFailed())return;
            const bool fallback=restricted&&exposure>0;QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
            const auto actual=render(source,state,false,ImagePipeline::InputEncoding::LinearProPhoto,space,{0,0,1,1},true);
            QCOMPARE(actual.diagnostic["luminance_mask"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
            if(fallback){const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,space));for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);}
        }
    }
    void pointColorMatchesReferenceAcrossSourcesAndDensities() {
        QImage integer(137,139,QImage::Format_RGBA64),floating(137,139,QImage::Format_RGBA32FPx4);
        const quint16 alpha16[]{0,655,24576,65535};const float alpha32[]{0,1.e-8f,.375f,1.f};
        for(int y=0;y<139;++y)for(int x=0;x<137;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,alpha16[(x+y)%4]);
            auto *pixel=reinterpret_cast<float *>(floating.scanLine(y))+4*x;
            pixel[0]=-.04f+2.f*x/136.f;pixel[1]=.02f+1.5f*y/138.f;pixel[2]=.1f+1.2f*((x+y)%101)/100.f;pixel[3]=alpha32[(x+y)%4];
        }
        int compute=0,cpu=0;
        for(bool fp32:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)
        for(double amount:{-73.,87.})for(bool preview:{false,true}) {
            const auto &source=fp32?floating:integer;AdjustmentState state;state.dehazeAmount=53;state.pointColorEnabled=true;state.pointColorLab={.65,.02,.01};state.pointColorHue=amount;state.pointColorSaturation=-39;state.pointColorLuminance=19;state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;state.textureAmount=57;state.clarityAmount=41;
            state.denoiseLuma=71;state.denoiseChroma=83;state.sharpenAmount=61;state.grainAmount=19;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",3},{"clarity",2}};
            const auto space=ColorManagement::OutputSpace(output);const auto input=ImagePipeline::InputEncoding(encoding);
            const auto frame=preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);const auto reference=preview?source.size()*4:QSize{};
            verifyParity(source,state,input,space,true,"Point Color combined",frame,reference);if(QTest::currentTestFailed())return;
            const bool fallback=space==ColorManagement::OutputSpace::AdobeRgb||(fp32&&encoding==1&&space==ColorManagement::OutputSpace::ProPhotoRgb);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);fallback?++cpu:++compute;
            const auto actual=render(source,state,false,input,space,frame,true,reference);
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["point_color"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
        }
        QCOMPARE(compute,44);QCOMPARE(cpu,20);qInfo()<<"Point Color64: actual compute"<<compute<<"explicit CPU policies"<<cpu;
    }
    void pointColorDarkFieldsAndAlphaHoles() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);const float alpha[]{0,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.001f+.015f*float((x*17+y*7)%23)/23;p[1]=.003f+.01f*float((x+y)%13)/13;p[2]=.008f;p[3]=alpha[(x+y)%4];
            if(p[3]==0){p[0]=64000;p[1]=32000;p[2]=16000;}
        }
        for(int output=0;output<4;++output)for(double amount:{-100.,100.})for(bool denoise:{false,true})for(bool tiny:{false,true}) {
            AdjustmentState state;state.dehazeAmount=53;state.pointColorEnabled=true;state.pointColorLab={.65,.02,.01};state.pointColorHue=amount;state.pointColorSaturation=-39;state.pointColorLuminance=19;state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"Point Color dark/holes",{0,0,1,1},source.size()*(tiny?40:2));
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void pointColorRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const auto source=loaded.image.copy((loaded.image.width()-512)/2,(loaded.image.height()-512)/2,512,512);
        for(int output=0;output<4;++output)for(bool denoise:{false,true}) {
            AdjustmentState state;state.dehazeAmount=65;state.pointColorEnabled=true;state.pointColorLab={.65,.02,.01};state.pointColorHue=-87;state.pointColorSaturation=41;state.pointColorLuminance=-19;state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;if(denoise){state.denoiseLuma=70;state.denoiseChroma=90;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"real Sony Point Color");
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void dehazeMatchesReferenceAcrossSourcesAndDensities() {
        QImage integer(137,139,QImage::Format_RGBA64),floating(137,139,QImage::Format_RGBA32FPx4);
        const quint16 alpha16[]{0,655,24576,65535};const float alpha32[]{0,1.e-8f,.375f,1.f};
        for(int y=0;y<139;++y)for(int x=0;x<137;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,alpha16[(x+y)%4]);
            auto *pixel=reinterpret_cast<float *>(floating.scanLine(y))+4*x;
            pixel[0]=-.04f+2.f*x/136.f;pixel[1]=.02f+1.5f*y/138.f;pixel[2]=.1f+1.2f*((x+y)%101)/100.f;pixel[3]=alpha32[(x+y)%4];
        }
        int compute=0,cpu=0;
        for(bool fp32:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)
        for(double amount:{-73.,87.})for(bool preview:{false,true}) {
            const auto &source=fp32?floating:integer;AdjustmentState state;state.dehazeAmount=amount;state.textureAmount=57;state.clarityAmount=41;
            state.denoiseLuma=71;state.denoiseChroma=83;state.sharpenAmount=61;state.grainAmount=19;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",3},{"clarity",2}};
            const auto space=ColorManagement::OutputSpace(output);const auto input=ImagePipeline::InputEncoding(encoding);
            const auto frame=preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);const auto reference=preview?source.size()*4:QSize{};
            verifyParity(source,state,input,space,true,"spatial dehaze",frame,reference);if(QTest::currentTestFailed())return;
            const bool fallback=space==ColorManagement::OutputSpace::AdobeRgb||(fp32&&encoding==1&&space==ColorManagement::OutputSpace::ProPhotoRgb);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);fallback?++cpu:++compute;
            const auto actual=render(source,state,false,input,space,frame,true,reference);
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["spatial_dehaze"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
        }
        QCOMPARE(compute,44);QCOMPARE(cpu,20);qInfo()<<"dehaze64: actual compute"<<compute<<"explicit CPU policies"<<cpu;
    }
    void dehazeDarkFieldsFractionalRadiiAndAlphaHoles() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);const float alpha[]{0,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.001f+.015f*float((x*17+y*7)%23)/23;p[1]=.003f+.01f*float((x+y)%13)/13;p[2]=.008f;p[3]=alpha[(x+y)%4];
            if(p[3]==0){p[0]=64000;p[1]=32000;p[2]=16000;}
        }
        for(int output=0;output<4;++output)for(double amount:{-100.,100.})for(bool denoise:{false,true})for(bool tiny:{false,true}) {
            AdjustmentState state;state.dehazeAmount=amount;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"dehaze dark/holes",{0,0,1,1},source.size()*(tiny?40:2));
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void dehazeRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const auto source=loaded.image.copy((loaded.image.width()-512)/2,(loaded.image.height()-512)/2,512,512);
        for(int output=0;output<4;++output)for(bool denoise:{false,true}) {
            AdjustmentState state;state.dehazeAmount=65;if(denoise){state.denoiseLuma=70;state.denoiseChroma=90;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"real Sony dehaze");
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void textureMatchesReferenceAcrossSourcesAndDensities() {
        QImage integer(137,139,QImage::Format_RGBA64),floating(137,139,QImage::Format_RGBA32FPx4);
        const quint16 alpha16[]{0,655,24576,65535};const float alpha32[]{0,1.e-8f,.375f,1.f};
        for(int y=0;y<139;++y)for(int x=0;x<137;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,alpha16[(x+y)%4]);
            auto *pixel=reinterpret_cast<float *>(floating.scanLine(y))+4*x;
            pixel[0]=-.04f+2.f*x/136.f;pixel[1]=.02f+1.5f*y/138.f;pixel[2]=.1f+1.2f*((x+y)%101)/100.f;pixel[3]=alpha32[(x+y)%4];
        }
        int compute=0,cpu=0;
        for(bool fp32:{false,true})for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)
        for(double amount:{-73.,87.})for(bool preview:{false,true}) {
            const auto &source=fp32?floating:integer;AdjustmentState state;state.textureAmount=amount;state.clarityAmount=41;
            state.denoiseLuma=71;state.denoiseChroma=83;state.sharpenAmount=61;state.grainAmount=19;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"sharpness",3},{"clarity",2}};
            const auto space=ColorManagement::OutputSpace(output);const auto input=ImagePipeline::InputEncoding(encoding);
            const auto frame=preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);const auto reference=preview?source.size()*4:QSize{};
            verifyParity(source,state,input,space,true,"spatial texture",frame,reference);if(QTest::currentTestFailed())return;
            const bool fallback=space==ColorManagement::OutputSpace::AdobeRgb||(fp32&&encoding==1&&space==ColorManagement::OutputSpace::ProPhotoRgb);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);fallback?++cpu:++compute;
            const auto actual=render(source,state,false,input,space,frame,true,reference);
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["spatial_texture"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
        }
        QCOMPARE(compute,44);QCOMPARE(cpu,20);qInfo()<<"texture64: actual compute"<<compute<<"explicit CPU policies"<<cpu;
    }
    void textureDarkFieldsFractionalRadiiAndAlphaHoles() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);const float alpha[]{0,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.001f+.015f*float((x*17+y*7)%23)/23;p[1]=.003f+.01f*float((x+y)%13)/13;p[2]=.008f;p[3]=alpha[(x+y)%4];
            if(p[3]==0){p[0]=64000;p[1]=32000;p[2]=16000;}
        }
        for(int output=0;output<4;++output)for(double amount:{-100.,100.})for(bool denoise:{false,true})for(bool tiny:{false,true}) {
            AdjustmentState state;state.textureAmount=amount;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"texture dark/holes",{0,0,1,1},source.size()*(tiny?40:2));
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void textureRealSonyCropMatchesReference() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony fixtures configured");}
        const auto loaded=loadSourceFresh(QDir(folder).filePath("sony_a7_iv_07.arw"),{});QVERIFY2(!loaded.image.isNull(),qPrintable(loaded.error));
        const auto source=loaded.image.copy((loaded.image.width()-512)/2,(loaded.image.height()-512)/2,512,512);
        for(int output=0;output<4;++output)for(bool denoise:{false,true}) {
            AdjustmentState state;state.textureAmount=65;if(denoise){state.denoiseLuma=70;state.denoiseChroma=90;}
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"real Sony texture");
            if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void dehazeResidentStagesTrackCompleteDependencies() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.15f+.03f*std::sin(float(x));p[1]=.2f+.02f*std::cos(float(y));p[2]=.12f;p[3]=1;}
        AdjustmentState state;state.dehazeAmount=73;QSize reference;
        auto encoding=ImagePipeline::InputEncoding::LinearProPhoto;auto output=ColorManagement::OutputSpace::SRgb;
        std::array<Float4,3> rows{};const std::array<Float4,3> *customRows=nullptr;
        const auto verify=[&]{verifyParity(source,state,encoding,output,true,"dehaze cache",{0,0,1,1},reference,customRows);QVERIFY(!engine->lastProcessUsedCpuFallback());};
        verify();auto guide=engine->dehazeGuideDispatches(),horizontal=engine->dehazeHorizontalDispatches(),vertical=engine->dehazeVerticalDispatches(),filter=engine->dehazeFilterDispatches(),uploads=engine->sourceUploads();
        const auto counts=[&]{QCOMPARE(engine->dehazeGuideDispatches(),guide);QCOMPARE(engine->dehazeHorizontalDispatches(),horizontal);QCOMPARE(engine->dehazeVerticalDispatches(),vertical);QCOMPARE(engine->dehazeFilterDispatches(),filter);};
        const auto allMiss=[&]{++guide;++horizontal;++vertical;++filter;};
        state.exposure=.2;state.temperature=17;state.tint=-13;verify();counts();
        state.dehazeAmount=49;++filter;verify();counts();
        state.dehazeAmount=-73;++filter;verify();counts();
        state.exposure=.3;verify();counts();
        state.dehazeAmount=87;++filter;verify();counts(); // Reuse positive guide/coarse across a negative veil.
        reference=source.size()*4;++horizontal;++vertical;++filter;verify();counts();
        output=ColorManagement::OutputSpace::DisplayP3;verify();counts();
        state.denoiseLuma=71;state.denoiseChroma=83;allMiss();verify();counts();
        state.denoiseChroma=19;allMiss();verify();counts();
        state.textureAmount=57;state.clarityAmount=41;verify();counts();
        const auto textureGuide=engine->textureGuideDispatches(),clarityGuide=engine->clarityGuideDispatches();
        state.dehazeAmount=35;++filter;verify();counts();
        QCOMPARE(engine->textureGuideDispatches(),textureGuide+1);QCOMPARE(engine->clarityGuideDispatches(),clarityGuide+1);
        state.textureAmount=-35;verify();counts();
        state.dehazeAmount=-49;++filter;verify();counts();
        encoding=ImagePipeline::InputEncoding::SRgb;++filter;verify();counts(); // New negative input invalidates retained positive helpers.
        state.dehazeAmount=73;allMiss();verify();counts();
        {const auto p=ProcessingPlan::compile(state,encoding,output);std::copy_n(p.data.data()+ProcessingPlan::Input0,3,rows.data());rows[0].x+=.005f;customRows=&rows;}
        allMiss();verify();counts();
        auto next=source.copy();source=next;state.dehazeAmount=0;state.denoiseLuma=0;state.denoiseChroma=0;state.textureAmount=0;state.clarityAmount=0;verify();counts();
        QCOMPARE(engine->sourceUploads(),uploads+1);state.dehazeAmount=73;allMiss();verify();counts();QCOMPARE(engine->sourceUploads(),uploads+1);
        source=source.copy(0,0,47,53);reference={};customRows=nullptr;allMiss();verify();counts();QCOMPARE(engine->sourceUploads(),uploads+2);
    }
    void textureResidentStagesTrackCompleteDependencies() {
        QImage source(73,79,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.15f+.03f*std::sin(float(x));p[1]=.2f+.02f*std::cos(float(y));p[2]=.12f;p[3]=1;}
        AdjustmentState state;state.textureAmount=73;QSize reference;
        auto encoding=ImagePipeline::InputEncoding::LinearProPhoto;auto output=ColorManagement::OutputSpace::SRgb;
        std::array<Float4,3> rows{};const std::array<Float4,3> *customRows=nullptr;
        const auto verify=[&]{verifyParity(source,state,encoding,output,true,"texture cache",{0,0,1,1},reference,customRows);QVERIFY(!engine->lastProcessUsedCpuFallback());};
        verify();auto guide=engine->textureGuideDispatches(),smallH=engine->textureSmallHorizontalDispatches(),smallV=engine->textureSmallVerticalDispatches(),largeH=engine->textureLargeHorizontalDispatches(),filter=engine->textureFilterDispatches(),uploads=engine->sourceUploads();
        const auto counts=[&]{QCOMPARE(engine->textureGuideDispatches(),guide);QCOMPARE(engine->textureSmallHorizontalDispatches(),smallH);QCOMPARE(engine->textureSmallVerticalDispatches(),smallV);QCOMPARE(engine->textureLargeHorizontalDispatches(),largeH);QCOMPARE(engine->textureFilterDispatches(),filter);};
        const auto allMiss=[&]{++guide;++smallH;++smallV;++largeH;++filter;};
        state.exposure=.3;state.temperature=17;output=ColorManagement::OutputSpace::DisplayP3;verify();counts();
        state.textureAmount=-49;++filter;verify();counts();
        reference=source.size()*4;++smallH;++smallV;++largeH;++filter;verify();counts();
        state.denoiseLuma=77;state.denoiseChroma=83;allMiss();verify();counts();
        state.denoiseChroma=19;allMiss();verify();counts();
        state.sharpenAmount=61;state.grainAmount=19;verify();counts();QCOMPARE(engine->sourceUploads(),uploads);
        state.clarityAmount=41;verify();counts();const auto clarityGuide=engine->clarityGuideDispatches();
        state.clarityAmount=-61;verify();counts();QCOMPARE(engine->clarityGuideDispatches(),clarityGuide);
        state.textureAmount=35;++filter;verify();counts();QCOMPARE(engine->clarityGuideDispatches(),clarityGuide+1);
        auto next=source.copy();source=next;state.textureAmount=0;state.clarityAmount=0;state.denoiseLuma=0;state.denoiseChroma=0;verify();counts();
        state.textureAmount=73;allMiss();verify();counts();QCOMPARE(engine->sourceUploads(),uploads+1);
        source=source.copy(0,0,67,71);reference={};allMiss();verify();counts();
        encoding=ImagePipeline::InputEncoding::SRgb;allMiss();verify();counts();
        const auto matrix=ProcessingPlan::compile(state,encoding,output);std::copy_n(matrix.data.data()+ProcessingPlan::Input0,3,rows.data());rows[0].x+=.01f;customRows=&rows;
        allMiss();verify();counts();output=ColorManagement::OutputSpace::SRgb;verify();counts();
    }
    void rejectedPlanPreservesAcceptedFrameProvenance_data() {
        QTest::addColumn<bool>("invalidLut");QTest::addColumn<bool>("resized");QTest::addColumn<bool>("invalidFloat");
        QTest::newRow("spatial-settings")<<false<<false<<false;QTest::newRow("lut-atlas")<<true<<false<<false;
        QTest::newRow("resized-spatial-settings")<<false<<true<<false;QTest::newRow("resized-lut-atlas")<<true<<true<<false;
        QTest::newRow("resized-invalid-float")<<false<<true<<true;
    }
    void rejectedPlanPreservesAcceptedFrameProvenance() {
        QFETCH(bool,invalidLut);QFETCH(bool,resized);QFETCH(bool,invalidFloat);
        auto saved=std::move(engine);engine=std::make_unique<GpuEngine>(rhi.get());
        auto restore=qScopeGuard([&]{engine=std::move(saved);});
        QImage original(9,7,QImage::Format_RGBA32FPx4);
        for(int y=0;y<original.height();++y)for(int x=0;x<original.width();++x){auto *p=reinterpret_cast<float *>(original.scanLine(y))+4*x;p[0]=.2f;p[1]=.3f;p[2]=.1f;p[3]=x==1?std::numeric_limits<float>::denorm_min():1.f;}
        AdjustmentState state;state.textureAmount=73;
        const auto accepted=render(original,state,false,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,{0,0,1,1},true);
        QVERIFY(!accepted.image.isNull());QCOMPARE(accepted.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
        const auto uploads=engine->sourceUploads(),acceptedRevision=engine->renderedRevision();
        auto other=resized?original.copy(0,0,7,5):original.copy();for(int y=0;y<other.height();++y)for(int x=0;x<other.width();++x)reinterpret_cast<float *>(other.scanLine(y))[4*x+3]=1;
        auto malformed=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        if(invalidLut){auto lut=std::make_shared<LookLut>();lut->digest="invalid-owned-lut";malformed.state.look.lut=lut;malformed.data[ProcessingPlan::LookStyle].w=1;}
        else if(invalidFloat)reinterpret_cast<float *>(other.scanLine(0))[0]=std::numeric_limits<float>::quiet_NaN();
        else malformed.data[ProcessingPlan::Texture].y=1;
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        const bool processed=engine->process(cb,other,malformed,++revision,{});rhi->endOffscreenFrame();rhi->finish();
        QVERIFY(!processed);QCOMPARE(engine->sourceUploads(),uploads);QCOMPARE(engine->renderedRevision(),acceptedRevision);
        QJsonObject frozen;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        const bool captured=engine->captureWorkingFrame(cb,acceptedRevision,[&](QJsonObject value){frozen=std::move(value);});
        rhi->endOffscreenFrame();rhi->finish();QVERIFY(captured);QVERIFY(frozen["available"].toBool());
        QCOMPARE(frozen["source_cache_key"],accepted.diagnostic["source_cache_key"]);
        QCOMPARE(accepted.diagnostic["pixel_sha256"].toString().size(),64);
        QCOMPARE(frozen["pixel_sha256"],accepted.diagnostic["pixel_sha256"]);
        QCOMPARE(frozen["pixels"],accepted.diagnostic["pixels"]);
        QCOMPARE(frozen["producer"].toString(),QString("cpu-reference-upload"));
        QCOMPARE(frozen["spatial_texture"].toString(),QString("cpu-reference"));
        QCOMPARE(frozen["numeric_source_policy"].toString(),QString("texture-near-zero-alpha"));
    }
    void pointColorNarrowRangesHueSeamsAndNeutrals() {
        constexpr float Pi=3.14159265358979323846f;int compute=0,cpu=0;
        for(const auto center : {PointColor::Lab{.62f,.08f,359.f},PointColor::Lab{.035f,.012f,179.f},PointColor::Lab{.6f,0,0}})
        for(int range=0;range<13;++range) {
            const float rL=(range==0||range==3)?.001f:range==1?.003f:.4f,rC=(range==0||range==4)?.001f:range==1?.02f:.12f,rH=(range==0||range==5)?1:range==6?2:range==7?3:range==8?5:range==9?10:range==10?20:range==11?44.9f:range==2?180:45;
            const float angle=center.b*(Pi/180.f);QImage source(31,29,QImage::Format_RGBA32FPx4);
            for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
                const float t=float((x*7+y*11)%101)/100.f;
                const float L=center.L+rL*(t*2-1),C=std::max(0.f,center.a+rC*(float(x)/30.f*2-1));
                const float h=(center.b+rH*(float(y)/28.f*2-1))*(Pi/180.f);
                const auto rgb=PointColor::toEncodedSrgb({L,C*std::cos(h),C*std::sin(h)});auto *q=reinterpret_cast<float *>(source.scanLine(y))+4*x;
                for(int c=0;c<3;++c)q[c]=rgb[c];const float alpha[]{0,1.e-9f,1.f/65535.f,.8f,1};q[3]=alpha[(x+y)%5];
            }
            for(int output=0;output<4;++output)for(double shift:{-180.,180.}) {
                AdjustmentState state;state.pointColorEnabled=true;state.pointColorLab={center.L,center.a*std::cos(angle),center.a*std::sin(angle)};
                state.pointColorHue=shift;state.pointColorSaturation=shift<0?-100:100;state.pointColorLuminance=shift<0?-100:100;
                state.pointColorHueRange=rH;state.pointColorChromaRange=rC;state.pointColorLightnessRange=rL;
                verifyParity(source,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),true,"Point Color narrow/seam/neutral");
                if(QTest::currentTestFailed())return;
                const bool precise=range==0||range==3||range==4||range==5||range==6||range==7||range==8||range==9||range==10||range==11;precise?++cpu:++compute;QCOMPARE(engine->lastProcessUsedCpuFallback(),precise);
                const auto actual=render(source,state,false,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output),{0,0,1,1},true);
                QCOMPARE(actual.diagnostic["point_color"].toString(),precise?QString("cpu-reference"):QString("gpu-compute"));
                QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),precise?QString("point-color-narrow-range"):QString("none"));
                if(precise){const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace(output)));for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);}
            }
        }
        QCOMPARE(compute,72);QCOMPARE(cpu,240);qInfo()<<"Point Color312 ranges: compute"<<compute<<"exact CPU precision"<<cpu;
    }
    void pointColorEditsKeepEveryResidentSpatialStageAndSource() {
        QImage source(83,89,QImage::Format_RGBA32FPx4);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.2f+.03f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=(x+y)%3?.7f:0;}
        AdjustmentState state;state.denoiseLuma=71;state.denoiseChroma=83;state.dehazeAmount=53;state.textureAmount=61;state.clarityAmount=37;
        const auto counters=[&]{return std::array<quint64,15>{engine->sourceUploads(),engine->denoiseGuideDispatches(),engine->denoiseFilterDispatches(),engine->dehazeGuideDispatches(),engine->dehazeHorizontalDispatches(),engine->dehazeVerticalDispatches(),engine->dehazeFilterDispatches(),engine->textureGuideDispatches(),engine->textureSmallHorizontalDispatches(),engine->textureSmallVerticalDispatches(),engine->textureLargeHorizontalDispatches(),engine->textureFilterDispatches(),engine->clarityGuideDispatches(),engine->clarityHorizontalDispatches(),engine->clarityFilterDispatches()};};
        const auto verify=[&]{verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"Point Color resident stages");QVERIFY(!engine->lastProcessUsedCpuFallback());};
        verify();const auto initial=counters();
        for(int i=0;i<18;++i) {
            state.pointColorEnabled=i%5!=0;state.pointColorHue=(i%2?-1:1)*(10+i*9);state.pointColorSaturation=(i%3-1)*49;state.pointColorLuminance=(i%4-2)*19;
            state.pointColorLab={.5+i*.01,.02-i*.001,.03};state.pointColorHueRange=45+i*7;state.pointColorChromaRange=.001+i*.01;state.pointColorLightnessRange=.001+i*.02;
            verify();if(QTest::currentTestFailed())return;QCOMPARE(counters(),initial);
        }
        state.pointColorEnabled=true;state.pointColorChromaRange=.001;state.pointColorLightnessRange=.001;
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"Point Color resident precision fallback");
        QVERIFY(engine->lastProcessUsedCpuFallback());QCOMPARE(counters(),initial);
        state.pointColorChromaRange=.12;state.pointColorLightnessRange=.4;verify();QCOMPARE(counters(),initial);
    }
    void pointColorRetainsExplicitSourceAndBrushCpuPolicies() {
        QImage source(39,41,QImage::Format_RGBA32FPx4);
        const float alpha[]{0,1.e-8f,1.f/65535.f,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.2f+.08f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=alpha[(x+y)%6];
        }
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)
        for(int output=0;output<4;++output)for(double amount:{-100.,73.})for(bool denoise:{false,true}) {
            AdjustmentState state;state.pointColorEnabled=true;state.pointColorHue=amount;state.pointColorSaturation=-37;state.pointColorLuminance=19;state.pointColorHueRange=180;state.pointColorChromaRange=1;state.pointColorLightnessRange=1;state.dehazeAmount=53;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            state.sharpenAmount=61;state.grainAmount=19;state.textureAmount=57;state.clarityAmount=-41;
            auto input=integer?ImagePipeline::rgba64Source(source):source;
            if(!integer)reinterpret_cast<float *>(input.scanLine(0))[3]=std::numeric_limits<float>::denorm_min();
            if(integer){BrushMaskGeometry::Stroke paint;paint.style={.2,.4,.8};paint.dabs={{{.5,.5},.7}};state.brushMasks.layers={{"point-color-reference",true,.6,{paint}}};}
            const auto space=ColorManagement::OutputSpace(output);const auto actual=render(input,state,true,ImagePipeline::InputEncoding(encoding),space,{0,0,1,1},true);
            QVERIFY(!actual.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
            const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),space));
            for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);
            QCOMPARE(actual.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
            QCOMPARE(actual.diagnostic["point_color"].toString(),QString("cpu-reference"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),integer?QString("existing-color-or-brush"):QString("dehaze-near-zero-alpha"));
            const auto counts=ScopesEngine::analyze(expected);QCOMPARE(actual.histogram.red,counts.red);QCOMPARE(actual.histogram.green,counts.green);QCOMPARE(actual.histogram.blue,counts.blue);QCOMPARE(actual.histogram.luma,counts.luma);
        }
    }

    void dehazeUsesExplicitCompleteCpuReferencePolicy() {
        QImage source(39,41,QImage::Format_RGBA32FPx4);
        const float alpha[]{0,1.e-8f,1.f/65535.f,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.2f+.08f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=alpha[(x+y)%6];
        }
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)
        for(int output=0;output<4;++output)for(double amount:{-100.,73.})for(bool denoise:{false,true}) {
            AdjustmentState state;state.dehazeAmount=amount;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            state.sharpenAmount=61;state.grainAmount=19;state.textureAmount=57;state.clarityAmount=-41;
            auto input=integer?ImagePipeline::rgba64Source(source):source;
            if(!integer)reinterpret_cast<float *>(input.scanLine(0))[3]=std::numeric_limits<float>::denorm_min();
            if(integer){BrushMaskGeometry::Stroke paint;paint.style={.2,.4,.8};paint.dabs={{{.5,.5},.7}};state.brushMasks.layers={{"dehaze-reference",true,.6,{paint}}};}
            const auto space=ColorManagement::OutputSpace(output);const auto actual=render(input,state,true,ImagePipeline::InputEncoding(encoding),space,{0,0,1,1},true);
            QVERIFY(!actual.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
            const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),space));
            for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);
            QCOMPARE(actual.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
            QCOMPARE(actual.diagnostic["spatial_dehaze"].toString(),QString("cpu-reference"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),integer?QString("existing-color-or-brush"):QString("dehaze-near-zero-alpha"));
            const auto counts=ScopesEngine::analyze(expected);QCOMPARE(actual.histogram.red,counts.red);QCOMPARE(actual.histogram.green,counts.green);QCOMPARE(actual.histogram.blue,counts.blue);QCOMPARE(actual.histogram.luma,counts.luma);
        }
    }

    void textureUsesExplicitCompleteCpuReferencePolicy() {
        QImage source(39,41,QImage::Format_RGBA32FPx4);
        const float alpha[]{0,std::numeric_limits<float>::denorm_min(),1.e-19f,.01f,.5f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.2f+.08f*std::sin(float(x));p[1]=.3f+.04f*std::cos(float(y));p[2]=.1f;p[3]=alpha[(x+y)%6];
        }
        for(bool integer:{false,true})for(int encoding=0;encoding<2;++encoding)
        for(int output=0;output<4;++output)for(double amount:{-100.,73.})for(bool denoise:{false,true}) {
            AdjustmentState state;state.textureAmount=amount;if(denoise){state.denoiseLuma=77;state.denoiseChroma=83;}
            state.sharpenAmount=61;state.grainAmount=19;
            if(integer){BrushMaskGeometry::Stroke paint;paint.style={.2,.4,.8};paint.dabs={{{.5,.5},.7}};state.brushMasks.layers={{"texture-reference",true,.6,{paint}}};}
            const auto input=integer?ImagePipeline::rgba64Source(source):source;
            const auto space=ColorManagement::OutputSpace(output);const auto actual=render(input,state,true,ImagePipeline::InputEncoding(encoding),space,{0,0,1,1},true);
            QVERIFY(!actual.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
            const auto expected=ImagePipeline::processWithPlan(input,ProcessingPlan::compile(state,ImagePipeline::InputEncoding(encoding),space));
            for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),size_t(source.width())*8)==0);
            QCOMPARE(actual.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
            QCOMPARE(actual.diagnostic["spatial_texture"].toString(),QString("cpu-reference"));
            QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),integer?QString("existing-color-or-brush"):QString("texture-near-zero-alpha"));
            const auto counts=ScopesEngine::analyze(expected);QCOMPARE(actual.histogram.red,counts.red);QCOMPARE(actual.histogram.green,counts.green);QCOMPARE(actual.histogram.blue,counts.blue);QCOMPARE(actual.histogram.luma,counts.luma);
        }
    }

    void spatialDenoiseMatchesCpuAcrossDensities() {
        QImage integer(137,139,QImage::Format_RGBA64),hdr(137,139,QImage::Format_RGBA32FPx4);
        const quint16 alpha16[]{0,1,24576,65535};const float alpha32[]{0,1.e-8f,1.f/65535,.375f,1.f};
        for(int y=0;y<139;++y)for(int x=0;x<137;++x) {
            reinterpret_cast<QRgba64 *>(integer.scanLine(y))[x]=QRgba64::fromRgba64((x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,alpha16[(x+y)%4]);
            auto *p=reinterpret_cast<float *>(hdr.scanLine(y))+4*x;p[0]=-.04f+2.f*x/136.f;p[1]=.02f+1.5f*y/138.f;p[2]=.1f+1.2f*((x+y)%101)/100.f;p[3]=alpha32[(x+y)%5];
        }
        const auto verify=[&](const QImage &input,const AdjustmentState &state,ImagePipeline::InputEncoding encoding,ColorManagement::OutputSpace output,bool preview) {
            const auto reference=preview?QSize(1096,1112):QSize{};const auto frame=preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);
            verifyParity(input,state,encoding,output,true,"spatial denoise",frame,reference);
            if(QTest::currentTestFailed())return;
            const bool fallback=BrushMaskTileRaster::anyEnabled(state.brushMasks)||(state.grainAmount>0&&output==ColorManagement::OutputSpace::AdobeRgb)||(input.format()==QImage::Format_RGBA32FPx4&&encoding==ImagePipeline::InputEncoding::LinearProPhoto&&output==ColorManagement::OutputSpace::ProPhotoRgb);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
            const auto actual=render(input,state,false,encoding,output,frame,true,reference);QVERIFY(actual.diagnostic["available"].toBool());
            QCOMPARE(actual.diagnostic["producer"].toString(),fallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            QCOMPARE(actual.diagnostic["spatial_denoise"].toString(),fallback?QString("cpu-reference"):QString("gpu-compute"));
            if(fallback) {
                auto p=ProcessingPlan::compile(state,encoding,output);p.setReferenceFrameSize(reference);p.setFrameRect(frame);const auto expected=ImagePipeline::processWithPlan(input,p);
                for(int y=0;y<input.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),std::size_t(input.width())*8)==0);
            }
        };
        for(const auto amounts : {QPoint(100,0),QPoint(0,100),QPoint(87,93)}) {
            AdjustmentState state;state.denoiseLuma=amounts.x();state.denoiseChroma=amounts.y();state.sharpenAmount=71;state.sharpenRadius=5.7;state.grainAmount=31;state.grainSize=3.3;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"clarity",3},{"sharpness",4}};
            for(int output=0;output<4;++output)for(bool preview : {false,true}) {
                for(int encoding=0;encoding<2;++encoding)verify(integer,state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(output),preview);
                verify(hdr,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),preview);
            }
        }
        AdjustmentState state;state.denoiseLuma=87;state.denoiseChroma=93;
        BrushMaskGeometry::Stroke paint;paint.style={.13,.42,.8};paint.dabs={{{.28,.37},.5},{{.69,.62},.9}};auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.3,.38},.3}};
        state.brushMasks.layers.push_back({"denoise-fallback",true,.85,{paint,erase}});
        for(int output=0;output<4;++output)verify(hdr,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true);
        if(!QTest::currentTestFailed())qInfo()<<"spatial denoise: 76 denoise cases: 48 mandatory compute, 28 explicit CPU policies";
    }
    void spatialDenoiseNoisyDarkFieldsAndResourceTransitions() {
        QImage noisy(137,139,QImage::Format_RGBA32FPx4);std::mt19937 rng(95951);std::normal_distribution<float> noise(0,.003f);const float alphas[]{0,1.e-8f,.375f,1.f};
        AdjustmentState state;state.denoiseLuma=89;state.denoiseChroma=97;
        for(float level : {.001f,.18f}) {
            for(int y=0;y<139;++y)for(int x=0;x<137;++x){auto *p=reinterpret_cast<float *>(noisy.scanLine(y))+4*x;for(int c=0;c<3;++c)p[c]=level+noise(rng)+(x>68&&c==0?.07f:0.f);p[3]=alphas[(x+y)%4];}
            for(int encoding=0;encoding<2;++encoding)for(int output=0;output<4;++output)for(bool preview : {false,true}) {
                verifyParity(noisy,state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(output),true,"noisy/dark denoise",{0,0,1,1},preview?noisy.size()*4:QSize{});if(QTest::currentTestFailed())return;QCOMPARE(engine->lastProcessUsedCpuFallback(),level==.001f&&encoding==1&&output==3);
            }
        }
        const auto first=render(noisy,state,false);QVERIFY(!first.image.isNull());const auto uploads=engine->sourceUploads();
        AdjustmentState detail=state;detail.sharpenAmount=83;detail.sharpenRadius=4.3;
        verifyParity(noisy,detail,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"denoise -> detail allocation");if(QTest::currentTestFailed())return;
        const auto disabled=render(noisy,{},false,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,{0,0,1,1},true);QVERIFY(!disabled.image.isNull());QCOMPARE(disabled.diagnostic["spatial_denoise"].toString(),QString("inactive"));const auto repeated=render(noisy,state,false);QCOMPARE(repeated.image,first.image);QCOMPARE(engine->sourceUploads(),uploads);
        const auto resized=noisy.copy(0,0,71,73);verifyParity(resized,detail,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"denoise source resize");if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        const auto restored=render(noisy,state,false);QCOMPARE(restored.image,first.image);
        if(!QTest::currentTestFailed())qInfo()<<"32 pure noisy/dark denoise compute cases and enable/detail/resize transitions passed";
    }
    void spatialDenoiseCacheTracksOnlyItsRealDependencies() {
        QImage source(137,139,QImage::Format_RGBA32FPx4);for(int y=0;y<139;++y)for(int x=0;x<137;++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.1f+.001f*((x*17+y*7)%23);p[1]=.12f;p[2]=.13f;p[3]=1.f;}
        AdjustmentState state;state.denoiseLuma=80;state.denoiseChroma=90;
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"denoise cached base");if(QTest::currentTestFailed())return;
        const auto guides=engine->denoiseGuideDispatches(),filters=engine->denoiseFilterDispatches(),uploads=engine->sourceUploads();
        state.exposure=.3;state.temperature=17;state.tint=-11;state.saturation=13;
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::DisplayP3,true,"denoise cache downstream edits");if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guides);QCOMPARE(engine->denoiseFilterDispatches(),filters);QCOMPARE(engine->sourceUploads(),uploads);
        state.denoiseLuma=91;verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"denoise cache amount invalidation");if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guides);QCOMPARE(engine->denoiseFilterDispatches(),filters+1);
        verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"denoise cache density invalidation",{0,0,1,1},source.size()*4);if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guides);QCOMPARE(engine->denoiseFilterDispatches(),filters+2);
        verifyParity(source,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace::SRgb,true,"denoise cache encoding invalidation",{0,0,1,1},source.size()*4);if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guides+1);QCOMPARE(engine->denoiseFilterDispatches(),filters+3);
        auto changed=source.copy();reinterpret_cast<float *>(changed.scanLine(0))[0]+=.02f;
        // A new source can be uploaded while the denoiser is off. Enabling it
        // later must compare against the guide's own source key, not merely
        // the most recently uploaded texture's key.
        const auto off=render(changed,{},false);QVERIFY(!off.image.isNull());
        verifyParity(changed,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace::SRgb,true,"denoise cache source uploaded while off",{0,0,1,1},changed.size()*4);if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guides+2);QCOMPARE(engine->denoiseFilterDispatches(),filters+4);
        const auto before=render(changed,state,false,ImagePipeline::InputEncoding::SRgb);QVERIFY(!before.image.isNull());const auto guideBefore=engine->denoiseGuideDispatches(),filterBefore=engine->denoiseFilterDispatches();
        auto fallback=state;fallback.grainAmount=31;fallback.grainSize=3.3;const auto cpu=render(changed,fallback,false,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace::AdobeRgb);QVERIFY(!cpu.image.isNull());QVERIFY(engine->lastProcessUsedCpuFallback());
        const auto after=render(changed,state,false,ImagePipeline::InputEncoding::SRgb);QCOMPARE(after.image,before.image);QCOMPARE(engine->denoiseGuideDispatches(),guideBefore);QCOMPARE(engine->denoiseFilterDispatches(),filterBefore);QVERIFY(!engine->lastProcessUsedCpuFallback());
        const auto p=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::SRgb);std::array<Float4,3> rows;std::copy_n(p.data.data()+ProcessingPlan::Input0,3,rows.data());rows[0].x+=.005f;
        verifyParity(changed,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace::SRgb,true,"denoise cache input-matrix invalidation",{0,0,1,1},{},&rows);if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guideBefore+1);QCOMPARE(engine->denoiseFilterDispatches(),filterBefore+1);
        verifyParity(changed,state,ImagePipeline::InputEncoding::SRgb,ColorManagement::OutputSpace::SRgb,true,"denoise cache unchanged input rows",{0,0,1,1},{},&rows);if(QTest::currentTestFailed())return;
        QCOMPARE(engine->denoiseGuideDispatches(),guideBefore+1);QCOMPARE(engine->denoiseFilterDispatches(),filterBefore+1);
    }
    void spatialDenoiseRealSonyCropUsesCompute() {
        const auto folder=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(folder.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony dataset configured; not a real-denoise pass");}
        QString error;const auto source=RawDecoder::decode(QDir(folder).filePath("sony_a7_iv_07.arw"),&error);QVERIFY2(!source.isNull(),qPrintable(error));
        const auto crop=source.copy((source.width()-512)/2,(source.height()-512)/2,512,512);AdjustmentState state;state.denoiseLuma=70;state.denoiseChroma=90;
        for(int output=0;output<4;++output)for(bool preview : {false,true}) {
            verifyParity(crop,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"actual Sony denoise",{0,0,1,1},preview?crop.size()*4:QSize{});if(QTest::currentTestFailed())return;QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void numericSourceBoundariesUseExplicitReferencePolicies() {
        QImage source(33,35,QImage::Format_RGBA32FPx4);AdjustmentState state;state.denoiseLuma=100;state.denoiseChroma=100;
        const auto exactPolicy=[&](bool expectedFallback,const char *policy,const AdjustmentState &edits,ColorManagement::OutputSpace output) {
            const auto actual=render(source,edits,true,ImagePipeline::InputEncoding::LinearProPhoto,output,{0,0,1,1},true);QVERIFY(!actual.image.isNull());
            QCOMPARE(engine->lastProcessUsedCpuFallback(),expectedFallback);QCOMPARE(actual.diagnostic["numeric_source_policy"].toString(),QString::fromLatin1(policy));
            QCOMPARE(actual.diagnostic["producer"].toString(),expectedFallback?QString("cpu-reference-upload"):QString("gpu-compute"));
            if(expectedFallback){const auto expected=ImagePipeline::processWithPlan(source,ProcessingPlan::compile(edits,ImagePipeline::InputEncoding::LinearProPhoto,output));for(int y=0;y<source.height();++y)QVERIFY(std::memcmp(actual.image.constScanLine(y),expected.constScanLine(y),std::size_t(source.width())*8)==0);}
        };
        for(float alpha : {0.f,std::numeric_limits<float>::denorm_min(),1.e-38f,1.e-30f,1.e-21f,1.e-20f,1.e-18f,1.e-8f,1.f/65535,1.f}) {
            for(int y=0;y<35;++y)for(int x=0;x<33;++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=.15f+.001f*((x*17+y*7)%23);p[1]=.12f;p[2]=.18f;p[3]=1;}
            auto *center=reinterpret_cast<float *>(source.scanLine(17))+4*16;center[0]=.17f;center[1]=.14f;center[2]=.19f;center[3]=alpha;
            const bool near=alpha>0&&alpha<1.e-18f;ImagePipeline::FloatSourceFacts facts;QVERIFY(ImagePipeline::validFloatSource(source,{},&facts));QCOMPARE(facts.nearZeroAlpha,near);QVERIFY(!facts.negativeRgb);
            exactPolicy(false,"none",{},ColorManagement::OutputSpace::SRgb);if(QTest::currentTestFailed())return;const auto uploads=engine->sourceUploads();
            for(int output=0;output<4;++output) {
                qInfo()<<"boundary alpha"<<alpha;verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"alpha boundary denoise");if(QTest::currentTestFailed())return;
                exactPolicy(near,near?"denoise-near-zero-alpha":"none",state,ColorManagement::OutputSpace(output));if(QTest::currentTestFailed())return;
            }
            QCOMPARE(engine->sourceUploads(),uploads);
        }
        for(float scale : {1.e-10f,4.f,256.f,65536.f}) {
            for(int y=0;y<35;++y)for(int x=0;x<33;++x){auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;p[0]=scale*float((x*7+y*3)%19-9)/10;p[1]=scale*float((x*3+y*7)%17-8)/10;p[2]=scale*float((x*5+y*11)%23-11)/12;p[3]=1;}
            ImagePipeline::FloatSourceFacts facts;QVERIFY(ImagePipeline::validFloatSource(source,{},&facts));QVERIFY(facts.negativeRgb);QVERIFY(!facts.nearZeroAlpha);
            for(int output=0;output<4;++output)for(bool active : {false,true}) {
                qInfo()<<"boundary RGB scale"<<scale<<"denoise active"<<active;const auto edits=active?state:AdjustmentState{};
                verifyParity(source,edits,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(output),true,"range boundary");if(QTest::currentTestFailed())return;
                exactPolicy(output==3,output==3?"signed-linear-prophoto":"none",edits,ColorManagement::OutputSpace(output));if(QTest::currentTestFailed())return;
            }
        }
        auto invalid=source.copy();reinterpret_cast<float *>(invalid.scanLine(0))[0]=std::numeric_limits<float>::quiet_NaN();ImagePipeline::FloatSourceFacts facts{true,true};QVERIFY(!ImagePipeline::validFloatSource(invalid,{},&facts));QVERIFY(!facts.nearZeroAlpha&&!facts.negativeRgb);
    }
    void independentSharpenMatchesCpuAtFullAndPreviewDensity() {
        QImage source(137,139,QImage::Format_RGBA64);const quint16 alpha[]{0,1,24576,65535};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*1597+y*311)%65536,(x*431+y*1973)%65536,(x*1131+y*89)%65536,alpha[(x+y)%4]);
        for(int encoding=0;encoding<2;++encoding)for(int space=0;space<4;++space)
        for(double radius : {.5,2.7,6.})for(bool preview : {false,true}) {
            AdjustmentState state;state.sharpenAmount=87;state.sharpenRadius=radius;state.exposure=.17;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"clarity",3},{"sharpness",4}};
            const auto reference=preview?QSize(1096,1112):QSize{};
            const auto frame=preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);
            verifyParity(source,state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),true,"independent sharpen",frame,reference);
            QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
        QImage hdr(137,139,QImage::Format_RGBA32FPx4);const float alphas[]{0,1.e-8f,1.f/65535,.375f,1.f};
        for(int y=0;y<hdr.height();++y)for(int x=0;x<hdr.width();++x) {
            auto *p=reinterpret_cast<float *>(hdr.scanLine(y))+4*x;
            p[0]=-.04f+2.f*x/136.f;p[1]=.02f+1.5f*y/138.f;p[2]=.1f+1.2f*((x+y)%101)/100.f;p[3]=alphas[(x+y)%5];
        }
        AdjustmentState state;state.sharpenAmount=100;state.sharpenRadius=5.7;
        for(int space=0;space<4;++space)for(bool preview : {false,true}) {
            verifyParity(hdr,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"FP32 independent sharpen",preview?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1),preview?QSize(548,556):QSize{});
            QCOMPARE(engine->lastProcessUsedCpuFallback(),space==3);
        }
        state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
        const bool restricted=rhi->backend()==QRhi::D3D11||rhi->backend()==QRhi::OpenGLES2;
        for(int space=0;space<4;++space) {
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"FL independent sharpen");
            QCOMPARE(engine->lastProcessUsedCpuFallback(),restricted);
        }
        BrushMaskGeometry::Stroke paint;paint.style={.13,.42,.8};paint.dabs={{{.28,.37},.5},{{.69,.62},.9}};
        auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.3,.38},.3}};
        state.brushMasks.layers.push_back({"detail-fallback",true,.85,{paint,erase}});state.grainAmount=37;state.grainSize=3.5;
        for(int space=0;space<4;++space) {
            verifyParity(hdr,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"Paint/Erase independent sharpen",{.25,.125,.5,.5},{548,556});
            QVERIFY(engine->lastProcessUsedCpuFallback());
        }
        const auto fallback=render(hdr,state,true,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,{0,0,1,1},true);
        QVERIFY(fallback.diagnostic["available"].toBool());QCOMPARE(fallback.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
    }
    void filmGrainMatchesCpuInEveryOutputSpaceAndViewport() {
        QImage source(385,289,QImage::Format_RGBA64);
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*157+y*31)%65536,(x*43+y*197)%65536,(x*113+y*89)%65536,(x+y)%3?23456:0);
        for(int encoding=0;encoding<2;++encoding)for(int space=0;space<4;++space)
        for(double size : {.5,3.5})for(bool viewport : {false,true}) {
            AdjustmentState state;state.grainAmount=89;state.grainSize=size;state.exposure=.27;
            state.look.mode="manual";state.look.code="ST";state.look.parameters={{"clarity",3},{"sharpness",4}};
            const QRectF frame=viewport?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1);
            verifyParity(source,state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),true,"film grain",frame);
            QCOMPARE(engine->lastProcessUsedCpuFallback(),ColorManagement::OutputSpace(space)==ColorManagement::OutputSpace::AdobeRgb);
        }
        QImage hdr(385,289,QImage::Format_RGBA32FPx4);
        for(int y=0;y<hdr.height();++y)for(int x=0;x<hdr.width();++x) {
            auto *p=reinterpret_cast<float *>(hdr.scanLine(y))+4*x;
            p[0]=-.04f+2.f*x/384.f;p[1]=.02f+1.5f*y/288.f;p[2]=.1f+1.2f*((x+y)%101)/100.f;p[3]=(x%3)*.375f;
        }
        AdjustmentState state;state.grainAmount=77;state.grainSize=2.5;
        for(int space=0;space<4;++space)for(bool viewport : {false,true}) {
            verifyParity(hdr,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"FP32 film grain",viewport?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1));
            QCOMPARE(engine->lastProcessUsedCpuFallback(),ColorManagement::OutputSpace(space)==ColorManagement::OutputSpace::AdobeRgb||space==3);
        }
        const auto fallback=render(hdr,state,true,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::AdobeRgb,{0,0,1,1},true);
        QVERIFY(fallback.diagnostic["available"].toBool());QCOMPARE(fallback.diagnostic["producer"].toString(),QString("cpu-reference-upload"));
        // FL's inherited HSL overrides deliberately use the existing numeric
        // CPU policy on D3D/OpenGL. Keep that integration distinct from the
        // mandatory actual-compute ST/HDR cases above.
        state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
        const bool restricted=rhi->backend()==QRhi::D3D11||rhi->backend()==QRhi::OpenGLES2;
        for(int space=0;space<4;++space)for(bool viewport : {false,true}) {
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"FL grain compatibility",viewport?QRectF(.25,.125,.5,.5):QRectF(0,0,1,1));
            QCOMPARE(engine->lastProcessUsedCpuFallback(),restricted||ColorManagement::OutputSpace(space)==ColorManagement::OutputSpace::AdobeRgb);
        }
    }
    void filmGrainWithNativePaintErasePreservesCpuParity() {
        namespace N=NativeBrush;
        struct Prepared {N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;BrushImageLease reference;bool adobeRefused=false,sharpenRefused=false,denoiseRefused=true,clarityRefused=true,textureRefused=true,dehazePolicyOk=true,pointColorRefused=true,luminanceMaskAdmitted=true,colorMaskAdmitted=true,prophotoPolicyOk=false;};
        for(bool fp32 : {false,true}) {
            QImage image(257,193,fp32?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                if(fp32) {auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=-.02f+1.5f*x/256.f;p[1]=.1f+y/192.f;p[2]=.2f;p[3]=(x%3)*.375f;}
                else reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(10000+x*100,8000+y*150,12000,quint16((x%3)*24576));
            }
            auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
            auto budget=std::make_shared<BrushRenderBudget>();AdjustmentState state;state.grainAmount=83;state.grainSize=3.5;
            BrushMaskGeometry::Stroke paint;paint.style={.13,.42,.8};paint.dabs={{{.28,.37},.5},{{.69,.62},.9}};
            auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.3,.38},.3}};
            state.brushMasks.layers.push_back({"grain-native",true,.85,{paint,erase}});
            const auto p=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
            QFutureWatcher<Prepared> watcher;watcher.setFuture(QtConcurrent::run([=] {
                Prepared result;result.source=N::CanonicalSource::capture(image,{"grain-owned",p.encoding,false,0,{0,0,1,1}},caps.threads(),budget);
                if(!result.source)return result;
                result.upload=N::UploadAdmission::prepare(result.source,caps);result.frame=N::FrameAdmission::prepare(result.source,caps,p,{"grain-catalog","grain-input","grain-version",1},1);
                const auto adobe=ProcessingPlan::compile(state,p.encoding,ColorManagement::OutputSpace::AdobeRgb,false,0);N::FrameFailure why=N::FrameFailure::None;
                const auto refused=N::FrameAdmission::prepare(result.source,caps,adobe,{"grain-catalog","grain-input","grain-version",1},2,&why);
                result.adobeRefused=!refused&&why==N::FrameFailure::CPUPolicy;
                const auto prophoto=ProcessingPlan::compile(state,p.encoding,ColorManagement::OutputSpace::ProPhotoRgb,false,0);why=N::FrameFailure::None;
                const auto prophotoFrame=N::FrameAdmission::prepare(result.source,caps,prophoto,{"grain-catalog","grain-input","grain-version",1},5,&why);
                result.prophotoPolicyOk=fp32?(!prophotoFrame&&why==N::FrameFailure::CPUPolicy):bool(prophotoFrame);
                auto detailState=state;detailState.sharpenAmount=80;detailState.sharpenRadius=2.7;
                const auto detail=ProcessingPlan::compile(detailState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                const auto detailFrame=N::FrameAdmission::prepare(result.source,caps,detail,{"grain-catalog","grain-input","grain-version",1},3,&why);
                result.sharpenRefused=!detailFrame&&why==N::FrameFailure::CPUPolicy;
                for(const auto amounts : {QPoint(100,0),QPoint(0,100),QPoint(87,93)}) {
                    auto denoiseState=state;denoiseState.denoiseLuma=amounts.x();denoiseState.denoiseChroma=amounts.y();
                    const auto denoise=ProcessingPlan::compile(denoiseState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto denoiseFrame=N::FrameAdmission::prepare(result.source,caps,denoise,{"grain-catalog","grain-input","grain-version",1},4,&why);
                    result.denoiseRefused=result.denoiseRefused&&!denoiseFrame&&why==N::FrameFailure::CPUPolicy;
                }
                for(double amount:{-100.,73.}) {
                    auto clarityState=state;clarityState.clarityAmount=amount;
                    const auto clarity=ProcessingPlan::compile(clarityState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto clarityFrame=N::FrameAdmission::prepare(result.source,caps,clarity,{"grain-catalog","grain-input","grain-version",1},6,&why);
                    result.clarityRefused=result.clarityRefused&&!clarityFrame&&why==N::FrameFailure::CPUPolicy;
                }
                for(double amount:{-100.,73.}) {
                    auto textureState=state;textureState.textureAmount=amount;
                    const auto texture=ProcessingPlan::compile(textureState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto textureFrame=N::FrameAdmission::prepare(result.source,caps,texture,{"grain-catalog","grain-input","grain-version",1},6,&why);
                    result.textureRefused=result.textureRefused&&!textureFrame&&why==N::FrameFailure::CPUPolicy;
                }
                for(double amount:{-100.,73.}) {
                    auto dehazeState=state;dehazeState.dehazeAmount=amount;
                    const auto dehaze=ProcessingPlan::compile(dehazeState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto dehazeFrame=N::FrameAdmission::prepare(result.source,caps,dehaze,{"grain-catalog","grain-input","grain-version",1},6,&why);
                    result.dehazePolicyOk=result.dehazePolicyOk&&(amount<0 ? bool(dehazeFrame)&&why==N::FrameFailure::None : !dehazeFrame&&why==N::FrameFailure::CPUPolicy);
                }
                for(double hue : {-180.,90.}) {
                    auto pointState=state;pointState.pointColorEnabled=true;pointState.pointColorHue=hue;pointState.pointColorHueRange=5;
                    const auto point=ProcessingPlan::compile(pointState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto pointFrame=N::FrameAdmission::prepare(result.source,caps,point,{"grain-catalog","grain-input","grain-version",1},3,&why);
                    result.pointColorRefused=result.pointColorRefused&&!pointFrame&&why==N::FrameFailure::CPUPolicy;
                }
                for(double exposure:{-3.,1.7}) {
                    auto maskState=state;maskState.luminanceMaskEnabled=true;maskState.luminanceMaskExposure=exposure;
                    const auto mask=ProcessingPlan::compile(maskState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto frame=N::FrameAdmission::prepare(result.source,caps,mask,{"grain-catalog","grain-input","grain-version",1},3,&why);
                    result.luminanceMaskAdmitted=result.luminanceMaskAdmitted&&bool(frame)&&why==N::FrameFailure::None;
                }
                for(double exposure:{-3.,1.7}) {
                    auto maskState=state;maskState.colorMaskEnabled=true;maskState.colorMaskExposure=exposure;
                    const auto mask=ProcessingPlan::compile(maskState,p.encoding,ColorManagement::OutputSpace::SRgb,false,0);why=N::FrameFailure::None;
                    const auto frame=N::FrameAdmission::prepare(result.source,caps,mask,{"grain-catalog","grain-input","grain-version",1},3,&why);
                    result.colorMaskAdmitted=result.colorMaskAdmitted&&bool(frame)&&why==N::FrameFailure::None;
                }
                auto request=result.source->exactRequest(p);if(request)result.reference=request->render({});return result;
            }));
            QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),10000);auto prepared=watcher.result();QVERIFY(prepared.upload&&prepared.frame&&prepared.reference);QVERIFY(prepared.adobeRefused);QVERIFY(prepared.sharpenRefused);QVERIFY(prepared.denoiseRefused);QVERIFY(prepared.clarityRefused);QVERIFY(prepared.textureRefused);QVERIFY(prepared.dehazePolicyOk);QVERIFY(prepared.pointColorRefused);QVERIFY(prepared.luminanceMaskAdmitted);QVERIFY(prepared.colorMaskAdmitted);QVERIFY(prepared.prophotoPolicyOk);
            auto input=domain->attach(prepared.upload);QVERIFY(bool(input));QRhiCommandBuffer *cb=nullptr;
            QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->upload(input,cb));rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(domain->ready(input));
            QVERIFY(domain->activate(prepared.frame->stamp()));auto frame=domain->attachFrame(input,prepared.frame);QVERIFY(bool(frame));
            QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->compute(frame,cb));rhi->endOffscreenFrame();rhi->finish();domain->collect();QVERIFY(domain->ready(frame));
            QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QRhiReadbackResult readback;bool done=false;readback.completed=[&]{done=true;};
            auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(domain->texture(frame)),&readback);cb->resourceUpdate(updates);rhi->endOffscreenFrame();rhi->finish();QVERIFY(done);
            QCOMPARE(readback.data.size(),image.width()*image.height()*16);
            QImage gpu(reinterpret_cast<const uchar *>(readback.data.constData()),image.width(),image.height(),image.width()*16,QImage::Format_RGBA32FPx4);
            const auto actual=ImagePipeline::rgba64Source(gpu),expected=prepared.reference.image();int maximum=0;
            for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
                const auto a=reinterpret_cast<const QRgba64 *>(actual.constScanLine(y))[x],b=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y))[x];
                maximum=std::max({maximum,std::abs(int(a.red())-int(b.red())),std::abs(int(a.green())-int(b.green())),std::abs(int(a.blue())-int(b.blue()))});QCOMPARE(a.alpha(),b.alpha());
            }
            qInfo()<<"native Paint/Erase grain"<<fp32<<"max RGB16 error"<<maximum;QVERIFY(maximum<=40);
            domain->release(frame);domain->release(input);domain->collect();domain->shutdown();
        }
    }
    void initTestCase() {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;
        rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;
        rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
        QRhiGles2InitParams params;
        params.format=QSurfaceFormat::defaultFormat();
        surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));
        params.fallbackSurface=surface.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        if (!rhi || !rhi->isFeatureSupported(QRhi::Compute)) {
            if (qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")) QFAIL("A working compute backend is required");
            QSKIP("No compute GPU backend available: this is NOT a GPU test pass");
        }
        engine=std::make_unique<GpuEngine>(rhi.get());
        qInfo()<<engine->backendName();
    }
    void diagnosticCaptureMatchesIndependentReadbackAndNamesCpuFallback() {
        QImage source(23,19,QImage::Format_RGBA64);
        for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(x*2341,y*3127,12345,(x+y)%2?23456:65535);
        const bool fallback=rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2;
        for (int mode=0;mode<4;++mode) {
            AdjustmentState state; state.exposure=.3; state.vignetteAmount=-1.5;
            if(mode==1){state.hue=-23;state.hslSaturation[5]=-25;}
            if(mode>=2){state.look.mode="manual";state.look.code=mode==2?"FL":"ST";state.look.parameters={{"sharpness",3},{"clarity",2}};}
            const auto space=mode>=2?ColorManagement::OutputSpace::DisplayP3:ColorManagement::OutputSpace::SRgb;
            const auto result=render(source,state,true,ImagePipeline::InputEncoding::LinearProPhoto,space,{.1,.2,.7,.6},true);
            QVERIFY(!result.image.isNull()); QVERIFY(result.diagnostic["available"].toBool());
            for(auto it=result.independentFingerprint.begin();it!=result.independentFingerprint.end();++it)
                QCOMPARE(result.diagnostic[it.key()],it.value());
            QCOMPARE(result.diagnostic["parameter_revision"].toInteger(),qint64(revision));
            QCOMPARE(result.diagnostic["output_space"].toString(),ColorManagement::key(space));
            // FL compiles HSL band changes, so it uses the same existing
            // D3D11/OpenGL safety fallback. ST proves native spatial compute.
            QCOMPARE(result.diagnostic["producer"].toString(),QString((mode==1||mode==2)&&fallback?"cpu-reference-upload":"gpu-compute"));
            QVERIFY(!result.diagnostic["monitor_icc"].toBool());
            QCOMPARE(result.histogram.pixelCount,quint64(source.width()*source.height()));
        }
    }
    void diagnosticRefusalsDoNotPoisonRenderingOrUploadSource() {
        QImage source(17,13,QImage::Format_RGBA32FPx4);source.fill(QColor::fromRgbF(.2,.3,.4));
        auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        QVERIFY(engine->process(cb,source,plan,++revision,{}));
        const auto uploads=engine->sourceUploads();QString error;QJsonObject output;
        QVERIFY(!engine->captureWorkingFrame(cb,revision+1,[&](QJsonObject){},&error));QVERIFY(!error.isEmpty());
        QVERIFY(!engine->captureWorkingFrame(cb,revision,[&](QJsonObject){},&error,17*13*16-1));
        QVERIFY(!engine->captureWorkingFrame(nullptr,revision,[&](QJsonObject){},&error));
        QVERIFY(engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){output=std::move(value);},&error));QVERIFY(error.isEmpty());
        QVERIFY(!engine->captureWorkingFrame(cb,revision,[&](QJsonObject){},&error));
        QCOMPARE(engine->sourceUploads(),uploads);QVERIFY(engine->error().isEmpty());
        rhi->endOffscreenFrame();rhi->finish();QVERIFY(output["available"].toBool());QVERIFY(!engine->hasPendingReadback());
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QByteArray counts;
        QVERIFY(engine->process(cb,source,plan,revision,[&](quint64,QByteArray bytes,quint64){counts=std::move(bytes);},true));
        rhi->endOffscreenFrame();rhi->finish();QCOMPARE(counts.size(),HistogramCounts::GpuWords*int(sizeof(quint32)));
        QCOMPARE(engine->sourceUploads(),uploads);
    }
    void diagnosticReadbackSurvivesTeardownAndIgnoresDisplayLut() {
        QImage source(11,7,QImage::Format_RGBA32FPx4);source.fill(QColor::fromRgbF(.3,.4,.5));
        auto first=render(source,{},false,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,{0,0,1,1},true);
        QVERIFY(first.diagnostic["available"].toBool());
        QImage black(33*33,33,QImage::Format_RGBA32FPx4);black.fill(Qt::black);
        engine->setDisplayColorLut(black,"diagnostic-black-monitor","test monitor");
        std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,source.size(),1,QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
        QVERIFY(color->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());QVERIFY(target->create());
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb);
        QVERIFY(engine->process(cb,source,plan,++revision,{}));QJsonObject second;
        QVERIFY(engine->draw(cb,target.get()));
        QVERIFY(engine->captureWorkingFrame(cb,revision,[&](QJsonObject value){second=std::move(value);}));
        rhi->endOffscreenFrame();engine.reset(); // Destruction must drain a still-owned result.
        QVERIFY(second["available"].toBool());QCOMPARE(second["pixel_sha256"],first.diagnostic["pixel_sha256"]);
        engine=std::make_unique<GpuEngine>(rhi.get());
        QImage resized(5,3,QImage::Format_RGBA32FPx4);resized.fill(Qt::gray);
        QVERIFY(!render(resized,{}).image.isNull());
    }
    void numericSafetyFallbackIsScoped() {
        QImage image(32,24,QImage::Format_RGBA64);
        image.fill(QColor(96,128,160));
        AdjustmentState neutral;
        QVERIFY(!render(image,neutral,false).image.isNull());
        QVERIFY(!engine->lastProcessUsedCpuFallback());
        const bool backendNeedsFallback = rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2;
        AdjustmentState hsl; hsl.hue=-23; hsl.hslSaturation[5]=-25;
        QVERIFY(!render(image,hsl,false).image.isNull());
        QCOMPARE(engine->lastProcessUsedCpuFallback(), backendNeedsFallback);
        AdjustmentState bright; bright.exposure=3.0; bright.saturation=-20; bright.blacks=-50; bright.whites=60;
        QVERIFY(!render(image,bright,false).image.isNull());
        QCOMPARE(engine->lastProcessUsedCpuFallback(), backendNeedsFallback);
        AdjustmentState ordinary; ordinary.exposure=.8; ordinary.saturation=18; ordinary.vibrance=22;
        QVERIFY(!render(image,ordinary,false).image.isNull());
        QVERIFY(!engine->lastProcessUsedCpuFallback());
        ordinary.exposure=.3; ordinary.vignetteAmount=3;
        QVERIFY(!render(image,ordinary,false).image.isNull()); QCOMPARE(engine->lastProcessUsedCpuFallback(),backendNeedsFallback);
        ordinary.vignetteAmount=-3;
        QVERIFY(!render(image,ordinary,false).image.isNull()); QVERIFY(!engine->lastProcessUsedCpuFallback());
    }
    void fp32DownscaledPreviewCpuGpuParityAllSpacesAndFallbacks() {
        QImage scene(321,259,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) for(int x=0;x<scene.width();++x) {
            auto *p=reinterpret_cast<float *>(scene.scanLine(y))+4*x;
            p[0]=float(2.1+.004*x+.002*y);
            p[1]=float(.22+.001*x+.0005*y);
            p[2]=float(-.07+.0004*x);
            p[3]=(x+y)%19==0?0.0f:(x+y)%11==0?.375f:1.0f;
        }
        auto scaled=FloatSceneResampler::scale(scene,{101,79});
        QVERIFY(!scaled.isNull());
        QCOMPARE(scaled.format(),QImage::Format_RGBA32FPx4);
        QVERIFY(ImagePipeline::validFloatSource(scaled));
        const auto *mid=reinterpret_cast<const float *>(scaled.constScanLine(39))+4*50;
        QVERIFY(mid[0]>2.0f);
        QVERIFY(mid[2]<0.1f);
        for(int mode=0;mode<2;++mode) for(int output=0;output<4;++output) {
            AdjustmentState state;state.exposure=-2;
            if(mode==1) state.hslSaturation[5]=-22;
            verifyParity(scaled,state,ImagePipeline::InputEncoding::LinearProPhoto,
                         ColorManagement::OutputSpace(output),true,"FP32 antialiased preview");
            if(QTest::currentTestFailed()) return;
            const bool fallback=output==3||(mode==1 && (rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2));
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
        }
    }
    void fp32GeometryStraightenProjectiveCaMatchesCpuGpuAndScopes() {
        // Exercise the actual scene image returned by spatial geometry,
        // including >1 highlights, negative channels and premultiplied-alpha
        // resampling. GPU compute and numeric CPU fallback must see the exact
        // same FP32 source; no integer proxy is allowed.
        QImage scene(113,89,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) for(int x=0;x<scene.width();++x) {
            auto *p=reinterpret_cast<float *>(scene.scanLine(y))+4*x;
            p[0]=float(2.1+.006*x+.003*y);
            p[1]=float(.16+.004*x+.001*y);
            p[2]=float(-.22+.002*x+.002*y);
            p[3]=(x+y)%9==0?.75f:1.0f;
        }
        GeometryState geometry;
        geometry.straighten=8.5;
        geometry.perspectiveHorizontal=.13;
        geometry.perspectiveVertical=-.09;
        geometry.distortion=-.12;
        geometry.redCa=1.5;
        geometry.blueCa=-.9;
        geometry.crop={.1,.1,.8,.8};
        geometry.quarterTurns=1;
        geometry.flipHorizontal=true;
        const auto corrected=geometry.apply(scene);
        QVERIFY(!corrected.isNull());
        QCOMPARE(corrected.format(),QImage::Format_RGBA32FPx4);
        QVERIFY(ImagePipeline::validFloatSource(corrected));
        QVERIFY(reinterpret_cast<const float *>(corrected.constScanLine(corrected.height()/2))[4*(corrected.width()/2)]>1.0f);
        const auto fingerprint=FloatFrameFingerprint::capture(corrected);
        QVERIFY(fingerprint.value("available").toBool());
        QVERIFY(fingerprint.value("ranges_rgba").toArray()[0].toArray()[1].toDouble()>2.0);
        for(int mode=0;mode<2;++mode) for(int space=0;space<4;++space) {
            AdjustmentState state;
            state.exposure=-2.0;
            state.temperature=mode==0?0:9;
            state.highlightRecovery=mode==0?0:22;
            if(mode==1) state.hslSaturation[5]=-27; // verified CPU fallback on D3D/OpenGL
            const auto output=ColorManagement::OutputSpace(space);
            verifyParity(corrected,state,ImagePipeline::InputEncoding::LinearProPhoto,
                         output,true,"FP32 lens/CA/projective/straighten");
            if(QTest::currentTestFailed()) return;
            const bool expectedFallback=space==3||(mode==1 &&
                (rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2));
            QCOMPARE(engine->lastProcessUsedCpuFallback(),expectedFallback);
        }
    }
    void fp32SceneOverRangeCpuGpuAndCpuSafetyFallback() {
        QImage scene(53,47,QImage::Format_RGBA32FPx4);
        const std::array<quint16,4> alpha{0,1,12345,65535};
        for(int y=0;y<scene.height();++y) {
            auto *row=reinterpret_cast<float *>(scene.scanLine(y));
            for(int x=0;x<scene.width();++x) {
                row[4*x]=.19f+float((x*11+y*3)%43)*.071f; // reaches >3
                row[4*x+1]=.04f+float((x*7+y*5)%43)*.029f;
                row[4*x+2]=(x+y)%8==0?-.012f:(.07f+float((x*5+y*9)%43)*.042f);
                row[4*x+3]=float(alpha[(x+y)%4])/65535.0f;
            }
        }
        QVERIFY(ImagePipeline::validFloatSource(scene));
        QCOMPARE(ImagePipeline::floatSource(scene).cacheKey(),scene.cacheKey());
        const auto clipped=ImagePipeline::rgba64Source(scene);
        for(int mode=0;mode<3;++mode) for(int space=0;space<4;++space) {
            AdjustmentState state; state.exposure=-1.5;
            if(mode==1) { state.temperature=18;state.highlightRecovery=25;state.vignetteAmount=-.9; }
            if(mode==2) { state.hue=-15;state.hslSaturation[5]=-20; }
            const auto output=ColorManagement::OutputSpace(space);
            const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0);
            if(mode==0 && space==0) {
                const auto full=ImagePipeline::processWithPlan(scene,plan);
                const auto earlyClipped=ImagePipeline::processWithPlan(clipped,plan);
                QVERIFY(full!=earlyClipped); // no pre-tone clipping in the CPU reference
            }
            verifyParity(scene,state,ImagePipeline::InputEncoding::LinearProPhoto,output,true,"FP32 scene > 1");
            if(QTest::currentTestFailed()) return;
            const bool fallback=space==3||(mode==2 && (rhi->backend()==QRhi::D3D11 || rhi->backend()==QRhi::OpenGLES2));
            QCOMPARE(engine->lastProcessUsedCpuFallback(),fallback);
        }
        // Capture a real output fingerprint from the frozen FP32 source rather
        // than a preclipped RGBA64 proxy.
        AdjustmentState reference; reference.exposure=-2.0;
        auto result=render(scene,reference,true,ImagePipeline::InputEncoding::LinearProPhoto,
                           ColorManagement::OutputSpace::SRgb,{},true);
        QVERIFY(!result.image.isNull());
        QVERIFY(result.diagnostic.value("available").toBool());
        QCOMPARE(result.diagnostic.value("producer").toString(),QString("gpu-compute"));
    }
    void invalidFloatSceneIsRefusedBeforeGpuUpload() {
        QImage scene(4,4,QImage::Format_RGBA32FPx4);
        for(int y=0;y<scene.height();++y) {
            auto *row=reinterpret_cast<float *>(scene.scanLine(y));
            for(int x=0;x<scene.width();++x) {
                row[4*x]=.35f;row[4*x+1]=.1f;row[4*x+2]=.5f;row[4*x+3]=1.0f;
            }
        }
        reinterpret_cast<float *>(scene.scanLine(1))[4]=std::numeric_limits<float>::quiet_NaN();
        QVERIFY(!ImagePipeline::validFloatSource(scene));
        QRhiCommandBuffer *cb=nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        bool accepted=false;
        QString error;
        quint64 uploads=99,rendered=99;
        auto guarded=std::make_unique<GpuEngine>(rhi.get());
        const auto plan=ProcessingPlan::compile({},ImagePipeline::InputEncoding::LinearProPhoto,
                                                ColorManagement::OutputSpace::SRgb,false,0);
        accepted=guarded->process(cb,scene,plan,1,{});
        error=guarded->error();
        uploads=guarded->sourceUploads();
        rendered=guarded->renderedRevision();
        rhi->endOffscreenFrame();rhi->finish();
        guarded.reset();
        QVERIFY(!accepted);
        QVERIFY(error.contains("Invalid") && error.contains("FP32"));
        QCOMPARE(uploads,quint64(0));
        QCOMPARE(rendered,quint64(0));
    }
    void colorGradingCpuGpuParityOnEveryOutputAndThreeTones() {
        QImage source(59,67,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x) {
            const quint16 r=quint16((x*1039+y*1043+300)%65536);
            const quint16 g=quint16((x*2539+y*2381+4000)%65536);
            const quint16 b=quint16((x*4763+y*779+2700)%65536);
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(r,g,b,alpha[(x+y)%4]);
        }
        for(int mode=0;mode<3;++mode) for(int encoding=0;encoding<2;++encoding) for(int space=0;space<4;++space) {
            AdjustmentState state; state.gradingEnabled=true;
            state.gradingHue={20,132,246};
            state.gradingSaturation={90,35,75};
            state.gradingBalance=mode==0 ? -70 : mode==1 ? 0 : 70;
            if(mode==1) { state.blackWhite=true; state.bwMix[0]=50; }
            if(mode==2) { state.look.mode="manual"; state.look.code="ST"; state.exposure=.3; }
            verifyParity(source,state,ImagePipeline::InputEncoding(encoding),
                         ColorManagement::OutputSpace(space),true,"three-way perceptual grade");
            if(QTest::currentTestFailed()) return;
            // Grading requires no CPU upload fallback on the existing tested backends.
            QVERIFY(!engine->lastProcessUsedCpuFallback());
        }
    }
    void blackWhiteMixCpuGpuParityAndNativeCompute() {
        QImage source(47,39,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for(int y=0;y<source.height();++y) for(int x=0;x<source.width();++x) {
            const quint16 r=quint16((x*9421+y*1283)%65536);
            const quint16 g=quint16((x*2837+y*7431)%65536);
            const quint16 b=quint16((x*5213+y*6151)%65536);
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(r,g,b,alpha[(x+y)%4]);
        }
        for(bool sony : {false,true}) for(int encoding=0;encoding<2;++encoding)
            for(int space=0;space<4;++space) {
                AdjustmentState state; state.blackWhite=true; state.exposure=.15;
                state.bwMix={75,-30,45,-25,15,-55,30,65};
                if(sony){state.look.mode="manual";state.look.code="BW";}
                verifyParity(source,state,ImagePipeline::InputEncoding(encoding),
                             ColorManagement::OutputSpace(space),true,"eight-band B&W mix");
                if(QTest::currentTestFailed()) return;
                QVERIFY(!engine->lastProcessUsedCpuFallback());
            }
    }
    // In-memory research-only brush plans cannot be saved or enabled in the
    // user interface. Until the QRhi brush shader is validated, ALL backends
    // must use the existing CPU reference upload and preserve scopes/alpha.
    void researchBrushEmptyTilePruningKeepsFallbackAndGpuScopes() {
        QImage source(273,259,QImage::Format_RGBA64);
        for(int y=0;y<source.height();++y) {
            auto *row=reinterpret_cast<QRgba64 *>(source.scanLine(y));
            for(int x=0;x<source.width();++x)
                row[x]=QRgba64::fromRgba64(30000+x*50,12000+y*75,17000,(x+y)%2?65535:12345);
        }
        BrushMaskDocument::Layer layer;layer.id="sparse_gpu";layer.enabled=true;layer.exposureEv=1.3;
        layer.strokes={BrushMaskGeometry::Stroke{
            BrushMaskGeometry::Style{.035,.5,.8},{{{.12,.14},1.}}}};
        AdjustmentState state;state.brushMasks.layers={layer};
        for(const auto frame:{QRectF(0,0,1,1),QRectF(.5,.5,.5,.5)}) {
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,
                         ColorManagement::OutputSpace::SRgb,true,"M4.4B.3 sparse/empty tiles",frame);
            if(QTest::currentTestFailed())return;
            // Even an empty viewport is still an active research request; do
            // not misreport its CPU pixels as native GPU brush computation.
            QVERIFY(engine->lastProcessUsedCpuFallback());
        }
    }

    void researchBrushCandidateIndexLargeDocumentKeepsGpuFallbackAndScopes() {
        namespace BT=BrushMaskTileRaster;
        namespace BD=BrushMaskDocument;
        namespace BM=BrushMaskGeometry;
        QImage source(259,197,QImage::Format_RGBA32FPx4);
        const std::array<float,4> alphas{0.f,1.f/65535.f,.375f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *pixel=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            pixel[0]=1.05f+.0008f*x; // modest scene-linear HDR and negative RGB
            pixel[1]=-.035f+.001f*y;
            pixel[2]=.22f+.0007f*(x+y);
            pixel[3]=alphas[(x+y)%4];
        }
        const QImage original=source.copy();
        AdjustmentState state;
        std::size_t totalDabs=0;
        for(int layerIndex=0;layerIndex<2;++layerIndex) {
            BD::Layer layer;layer.id=QStringLiteral("indexed_gpu_%1").arg(layerIndex);
            layer.enabled=true;layer.exposureEv=layerIndex?-.45:.8;
            for(int strokeIndex=0;strokeIndex<64;++strokeIndex) {
                BM::Stroke stroke{BM::Style{.012,.35,.7},{}};
                for(int dabIndex=0;dabIndex<128;++dabIndex)
                    stroke.dabs.push_back({{.05+.001*(dabIndex%17),
                                             .05+.001*((dabIndex/17)%8)},1.});
                // Eight visible dabs per layer; all other dabs lie far
                // outside this panned viewport, in independent strokes.
                if(strokeIndex<8)
                    stroke.dabs[0].center={.65+.04*(strokeIndex%4),
                                           .66+.12*(strokeIndex/4)};
                totalDabs+=stroke.dabs.size();
                layer.strokes.push_back(std::move(stroke));
            }
            QCOMPARE(layer.strokes.size(),std::size_t(64));
            state.brushMasks.layers.push_back(std::move(layer));
        }
        QCOMPARE(totalDabs,BD::MaxTotalDabs);
        QVERIFY(BD::validDocument(state.brushMasks));
        QVERIFY(AdjustmentState::validBrushMaskJson(state.toJson()));
        const QRectF viewport(.6,.6,.3,.3);
        BT::FramePlan candidates;
        QVERIFY(BT::planFrame(state.brushMasks,source.width(),source.height(),
                              {0,0,source.width(),source.height()},&candidates,nullptr,{.6,.6,.3,.3}));
        QVERIFY(candidates.hasCandidateIndex());
        // These sixteen small visible dabs each intersect exactly one tile.
        QCOMPARE(candidates.candidateReferences(),std::size_t(16));
        QVERIFY(candidates.candidateVisits>0);
        for(const auto output:{ColorManagement::OutputSpace::SRgb,
                               ColorManagement::OutputSpace::DisplayP3,
                               ColorManagement::OutputSpace::ProPhotoRgb}) {
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,
                         output,true,"M4.4B.4 16K-dab indexed viewport",viewport);
            if(QTest::currentTestFailed())return;
            QVERIFY(engine->lastProcessUsedCpuFallback());
        }
        const auto captured=render(source,state,true,ImagePipeline::InputEncoding::LinearProPhoto,
                                   ColorManagement::OutputSpace::SRgb,viewport,true);
        QVERIFY(!captured.image.isNull());
        QVERIFY(captured.diagnostic.value("available").toBool());
        QCOMPARE(captured.diagnostic.value("producer").toString(),QString("cpu-reference-upload"));
        QCOMPARE(captured.histogram.pixelCount,quint64(source.width())*source.height());
        QVERIFY(engine->lastProcessUsedCpuFallback());
        QCOMPARE(source,original);
    }

    void researchBrushParallelTilesKeepHdrViewportFallbackAndScopes() {
        namespace BD=BrushMaskDocument;
        namespace BM=BrushMaskGeometry;
        // Exceed ParallelRows' 262144-pixel threshold with partial edge tiles,
        // HDR/negative scene channels and alpha that must survive unchanged.
        QImage source(541,509,QImage::Format_RGBA32FPx4);
        const std::array<float,4> alphas{0.f,1.f/65535.f,.375f,1.f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *pixel=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            pixel[0]=.12f+.0025f*x;
            pixel[1]=-.035f+.0017f*y;
            pixel[2]=.22f+.001f*((x+y)%97);
            pixel[3]=alphas[(x+y)%4];
        }
        const QImage original=source.copy();
        BD::Layer lighter;lighter.id="parallel_hdr";lighter.enabled=true;lighter.exposureEv=1.3;
        lighter.strokes={BM::Stroke{BM::Style{.12,.4,.85},
            {{{.36,.38},1.},{{.56,.47},.65},{{.72,.62},.9}}}};
        BD::Layer darker;darker.id="parallel_shadow";darker.enabled=true;darker.exposureEv=-.85;
        darker.strokes={BM::Stroke{BM::Style{.09,.72,.65},{{{.59,.41},1.}}}};
        AdjustmentState state;state.brushMasks.layers={lighter,darker};
        state.linearMaskEnabled=true;state.linearMaskExposure=-.4;state.linearMaskAngle=22;
        state.vignetteAmount=-.4;
        QVERIFY(BD::validDocument(state.brushMasks));
        QVERIFY(AdjustmentState::validBrushMaskJson(state.toJson()));
        const bool hadBudget=qEnvironmentVariableIsSet("JIXELLIGHT_CPU_THREADS");
        const auto previousBudget=qgetenv("JIXELLIGHT_CPU_THREADS");
        const auto restoreBudget=qScopeGuard([&] {
            if(hadBudget)qputenv("JIXELLIGHT_CPU_THREADS",previousBudget);
            else qunsetenv("JIXELLIGHT_CPU_THREADS");
        });
        qputenv("JIXELLIGHT_CPU_THREADS","4");
        const auto counter=[](const char *key) {
            return PerformanceRecorder::snapshot()["counters"].toObject().value(key).toInteger();
        };
        for(const auto frame:{QRectF(0,0,1,1),QRectF(.18,.13,.61,.58)})
            for(const auto output:{ColorManagement::OutputSpace::SRgb,
                                   ColorManagement::OutputSpace::DisplayP3,
                                   ColorManagement::OutputSpace::ProPhotoRgb}) {
                auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output);
                plan.setFrameRect(frame);
                const auto beforeHelpers=counter("brush_research_helper_tasks");
                const auto beforeParallel=counter("brush_research_parallel_frames");
                const auto beforeSerial=counter("brush_research_serial_frames");
                const auto serial=ImagePipeline::processWithPlan(source,plan,{},false);
                QVERIFY(!serial.isNull());
                QCOMPARE(counter("brush_research_helper_tasks"),beforeHelpers);
                QCOMPARE(counter("brush_research_parallel_frames"),beforeParallel);
                QCOMPARE(counter("brush_research_serial_frames")-beforeSerial,qint64(1));
                const auto parallel=ImagePipeline::processWithPlan(source,plan,{},true);
                QCOMPARE(parallel,serial);
                const auto helpers=counter("brush_research_helper_tasks")-beforeHelpers;
                const auto parallelFrames=counter("brush_research_parallel_frames")-beforeParallel;
                const auto serialFrames=counter("brush_research_serial_frames")-beforeSerial-1;
                // A busy shared pool may legitimately submit no helper. The
                // isolated CoreTests exercise successful helper submission;
                // here provenance must describe the work actually accepted.
                QVERIFY(helpers>=0 && helpers<=3);
                QCOMPARE(parallelFrames,qint64(helpers>0));
                QCOMPARE(serialFrames,qint64(helpers==0));
                verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,
                             output,true,"M4.4B.6 parallel HDR brush tiles",frame);
                if(QTest::currentTestFailed())return;
                QVERIFY(engine->lastProcessUsedCpuFallback());
            }
        const auto captured=render(source,state,true,ImagePipeline::InputEncoding::LinearProPhoto,
                                   ColorManagement::OutputSpace::SRgb,QRectF(.18,.13,.61,.58),true);
        QVERIFY(!captured.image.isNull());
        QVERIFY(captured.diagnostic.value("available").toBool());
        QCOMPARE(captured.diagnostic.value("producer").toString(),QString("cpu-reference-upload"));
        QCOMPARE(captured.histogram.pixelCount,quint64(source.width())*source.height());
        QVERIFY(engine->lastProcessUsedCpuFallback());
        QCOMPARE(source,original);
    }

    void researchBrushCpuGpuFallbackFullViewportAndHdrParity() {
        QImage source(47,39,QImage::Format_RGBA32FPx4);
        const std::array<float,4> alphas{0.0f,1.0f/65535.0f,.5f,1.0f};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x) {
            auto *p=reinterpret_cast<float *>(source.scanLine(y))+4*x;
            p[0]=.21f+float(x)*.031f; // HDR scene samples >1
            p[1]=-.03f+float(y)*.016f;
            p[2]=.4f+float((x+y)%9)*.06f;
            p[3]=alphas[(x+y)%4];
        }
        const QImage original=source.copy();
        BrushMaskDocument::Layer oval;
        oval.id="cpu_research";oval.enabled=true;oval.exposureEv=1.7;
        oval.strokes={BrushMaskGeometry::Stroke{
            BrushMaskGeometry::Style{.19,.4,.85},
            {{{.44,.51},1.},{{.56,.47},.65}}}};
        BrushMaskDocument::Layer darker;
        darker.id="shadow_research";darker.enabled=true;darker.exposureEv=-.85;
        darker.strokes={BrushMaskGeometry::Stroke{
            BrushMaskGeometry::Style{.13,.72,.65},{{{.59,.41},1.}}}};
        AdjustmentState brush;brush.brushMasks.layers={oval,darker};
        QVERIFY(BrushMaskDocument::validDocument(brush.brushMasks));
        QVERIFY(AdjustmentState::validBrushMaskJson(brush.toJson()));
        brush.linearMaskEnabled=true;brush.linearMaskExposure=-.4;brush.linearMaskAngle=22;
        brush.vignetteAmount=-.4;
        for(const auto frame : {QRectF(0,0,1,1),QRectF(.18,.13,.61,.58)})
            for(const auto output : {ColorManagement::OutputSpace::SRgb,
                                     ColorManagement::OutputSpace::DisplayP3,
                                     ColorManagement::OutputSpace::ProPhotoRgb}) {
                verifyParity(source,brush,ImagePipeline::InputEncoding::LinearProPhoto,
                             output,true,"M4.4A research CPU brush GPU fallback",frame);
                if (QTest::currentTestFailed()) return;
                QVERIFY(engine->lastProcessUsedCpuFallback());
            }
        // The sixth argument is the explicit full-image ROI. {} is an EMPTY
        // QRectF, not the default {0,0,1,1}; it correctly fails the strict
        // research mask viewport check and poisons this test engine.
        auto withDiagnostic=render(source,brush,true,ImagePipeline::InputEncoding::LinearProPhoto,
                                   ColorManagement::OutputSpace::SRgb,QRectF(0,0,1,1),true);
        QVERIFY(!withDiagnostic.image.isNull());
        QVERIFY(engine->lastProcessUsedCpuFallback());
        QCOMPARE(withDiagnostic.diagnostic.value("producer").toString(),
                 QString("cpu-reference-upload"));
        QCOMPARE(source,original);
        for(auto &layer:brush.brushMasks.layers) layer.enabled=false;
        auto disabled=render(source,brush,false);
        QVERIFY(!disabled.image.isNull());
        QVERIFY(!engine->lastProcessUsedCpuFallback()); // ordinary GPU compute
    }
    void radialMaskCpuGpuParityInvertedEllipseViewportAndSourceAlpha() {
        QImage source(73,91,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*911+y*107)%65536,(x*487+y*311)%65536,(x*1177+y*59)%65536,alpha[(x+y)%4]);
        for(int mode=0;mode<4;++mode) {
            AdjustmentState state;state.radialMaskEnabled=true;
            state.radialMaskExposure=mode==0?1.5:mode==1?-2.5:mode==2?2.0:-1.2;
            state.radialMaskCenterX=mode==0?.5:mode==1?.25:mode==2?.78:.4;
            state.radialMaskCenterY=mode==0?.5:mode==1?.68:mode==2?.3:.6;
            state.radialMaskRadiusX=mode==1?.12:mode==2?.7:.35;
            state.radialMaskRadiusY=mode==1?.68:mode==2?.17:.4;
            state.radialMaskFeather=mode==1?.02:mode==2?.9:.33;
            state.radialMaskInverted=mode>=2;
            state.radialMaskAngle=mode==0?0:mode==1?45:mode==2?90:-125;
            if(mode==3) {state.linearMaskEnabled=true;state.linearMaskExposure=.65;
                          state.linearMaskAngle=32;state.vignetteAmount=-.5;}
            for(const auto &frame : {QRectF(0,0,1,1),QRectF(.17,.23,.46,.4)})
                for(int encoding=0;encoding<2;++encoding)
                    for(int space=0;space<4;++space) {
                        verifyParity(source,state,ImagePipeline::InputEncoding(encoding),
                                     ColorManagement::OutputSpace(space),true,
                                     "independent radial mask",frame);
                        if(QTest::currentTestFailed())return;
                    }
        }
    }
    void linearMaskCpuGpuParityIncludesFullImageViewportAndAlpha() {
        QImage source(71,93,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for(int y=0;y<source.height();++y)for(int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(
                (x*941+y*113)%65536,(x*433+y*277)%65536,(x*1531+y*71)%65536,alpha[(x+y)%4]);
        for(double angle : {0.0,90.0,-45.0,180.0})
            for(const auto &frame : {QRectF(0,0,1,1),QRectF(.13,.18,.52,.43)})
                for(int encoding=0;encoding<2;++encoding)
                    for(const auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::ProPhotoRgb}) {
                        AdjustmentState mask;mask.linearMaskEnabled=true;mask.linearMaskExposure=-2;
                        mask.linearMaskCenter=.4;mask.linearMaskFeather=.35;mask.linearMaskAngle=angle;
                        mask.vignetteAmount=-.3;
                        verifyParity(source,mask,ImagePipeline::InputEncoding(encoding),space,true,"graduated linear mask",frame);
                        if(QTest::currentTestFailed())return;
                    }
    }
    void vignetteCpuGpuParityIncludesFullFrameAndViewportCoordinates() {
        QImage source(71,93,QImage::Format_RGBA64);
        const std::array<quint16,4> alpha{0,1,23456,65535};
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64((x*941+y*113)%65536,(x*433+y*277)%65536,(x*1531+y*71)%65536,alpha[(x+y)%4]);
        const std::array<std::array<double,3>,5> cases{{{{0,.5,1}},{{-3,0,1}},{{-1.5,.4,.7}},{{3,0,.8}},{{2,.95,.01}}}};
        for (const auto &v : cases) for (const auto &frame : {QRectF(0,0,1,1),QRectF(.13,.18,.52,.43)})
            for (int encoding=0;encoding<2;++encoding) for (int space=0;space<4;++space) for (bool sony : {false,true}) {
                AdjustmentState state; state.exposure=.3; state.vignetteAmount=v[0]; state.vignetteMidpoint=v[1]; state.vignetteFeather=v[2];
                if (sony) { state.look.mode="manual"; state.look.code="FL"; state.look.parameters={{"sharpness",3},{"clarity",2}}; }
                verifyParity(source,state,ImagePipeline::InputEncoding(encoding),ColorManagement::OutputSpace(space),true,"vignette",frame);
            }
    }
    void cpuCorrectedGeometryFeedsGpuWithoutRelaxingParity() {
        QImage source(131,97,QImage::Format_RGBA64);
        for (int y=0;y<source.height();++y) for (int x=0;x<source.width();++x)
            reinterpret_cast<QRgba64 *>(source.scanLine(y))[x]=QRgba64::fromRgba64(2000+x*301,1000+y*503,3000+x*179+y*137,65535);
        for (int mode=0;mode<5;++mode) {
            AdjustmentState state; state.exposure=.4; state.temperature=15; state.saturation=12;
            auto &g=state.geometry;
            if (mode==0) g.perspectiveHorizontal=.15;
            if (mode==1) g.perspectiveVertical=-.12;
            if (mode==2) g.distortion=.2;
            if (mode==3) { g.redCa=1.2; g.blueCa=-1.1; }
            if (mode==4) { g.perspectiveHorizontal=.1; g.perspectiveVertical=-.08; g.distortion=-.15; g.redCa=.7; g.blueCa=-.6; g.straighten=5; g.crop={.1,.2,.7,.6}; g.quarterTurns=1; }
            const auto prepared=g.apply(source); QVERIFY(!prepared.isNull());
            for (auto space : {ColorManagement::OutputSpace::SRgb,ColorManagement::OutputSpace::DisplayP3,ColorManagement::OutputSpace::AdobeRgb,ColorManagement::OutputSpace::ProPhotoRgb})
                verifyParity(prepared,state,ImagePipeline::InputEncoding::LinearProPhoto,space,true,"CPU manual geometry -> GPU color");
        }
    }
    void cleanupTestCase() { engine.reset(); rhi.reset(); surface.reset(); }
    void pixelsAndHistogramMatchCpu() {
        // Retain the historical fixture as well as the explicitly ordered new
        // fixtures below. Function-argument RNG order differs across compilers.
        QImage image(131,137,QImage::Format_RGBA64);
        std::mt19937 random(652819);
        for(int y=0;y<image.height();++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<image.width();++x) line[x]=QRgba64::fromRgba64(random()%60000,random()%60000,random()%60000,65535);
        }
        for(int mode=0;mode<4;++mode) {
            AdjustmentState state;
            if(mode==1) { state.exposure=.8;state.temperature=30;state.tint=-25;state.highlights=-20;state.shadows=10;state.contrast=12;state.highlightRecovery=30; }
            if(mode==2) { state.hue=-23;state.saturation=18;state.vibrance=22;state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8; }
            if(mode==3) { state.exposure=3.0;state.saturation=-20;state.blacks=-50;state.whites=60; }
            qInfo()<<"CPU/GPU mode"<<mode;
            verifyParity(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"historical fixture");
            if(QTest::currentTestFailed()) return;
        }
    }
    void sonyLooksDetailsAndLuts() {
        QImage image(83,95,QImage::Format_RGBA64);std::mt19937 rng(81138);
        for(int y=0;y<image.height();++y){auto *p=reinterpret_cast<QRgba64*>(image.scanLine(y));for(int x=0;x<image.width();++x){
            const quint16 r=rng()%58000,g=rng()%58000,b=rng()%58000;p[x]=QRgba64::fromRgba64(r,g,b,65535);}}
        for(const auto &v:LookProfiles::catalog()) {
            AdjustmentState s;s.look.mode="manual";s.look.code=v.toMap()["code"].toString();
            verifyParity(image,s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"Sony approximate preset");
            if(QTest::currentTestFailed())return;
            s.look.parameters={{"contrast",3},{"highlights",-2},{"shadows",2},{"fade",2},{"saturation",-1},{"sharpness",8},{"sharpnessRange",5},{"clarity",7}};
            verifyParity(image,s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"Sony fine adjustments + detail");
            if(QTest::currentTestFailed())return;
        }
        for(int n:{2,17,33})for(int space=0;space<4;++space) {
            auto lut=std::make_shared<LookLut>(*LookLut::identity(n));
            for(int i=0;i<lut->rgb.size();i+=3){lut->rgb[i]=.04f+.86f*lut->rgb[i];lut->rgb[i+2]=.02f+.92f*lut->rgb[i+2];}
            lut->updateDigest();AdjustmentState s;s.look.mode="calibrated";s.look.lut=lut;s.look.strength=.7;
            s.look.parameters={{"sharpness",6},{"clarity",4}};
            verifyParity(image,s,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"LUT and details across output spaces");
            if(QTest::currentTestFailed())return;
        }
    }
    void realSonyEmpiricalProfileParity() {
        const QString fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()){if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required real Sony fixtures unavailable");QSKIP("No real Sony dataset configured; not a real-profile pass");}
        const QString path=qEnvironmentVariable("JIXELLIGHT_TEST_SONY_PROFILE",QDir::currentPath()+"/sony-real-reports/dataset/empirical.jlook.json");
        QFile file(path);QVERIFY2(file.open(QIODevice::ReadOnly),qPrintable(path));
        const auto json=QJsonDocument::fromJson(file.readAll()).object();
        const auto look=LookState::fromJson(json["look"].toObject());QVERIFY2(look.lut,qPrintable(look.error));
        QVERIFY(look.lut->evidence["independentValidationPassed"].toBool());
        QString error;auto source=RawDecoder::decode(fixtures+"/sony_a7_iv_07.arw",&error);
        QVERIFY2(!source.isNull(),qPrintable(error));source=source.scaled(257,257,Qt::KeepAspectRatio,Qt::SmoothTransformation);
        for(int space=0;space<4;++space){AdjustmentState state;state.look=look;
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"Real Sony held-out RAW + empirical LUT");
            if(QTest::currentTestFailed())return;
            state.look.strength=.7;state.look.parameters={{"sharpness",4},{"clarity",1}};
            verifyParity(source,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace(space),true,"Real Sony LUT strength and details");
            if(QTest::currentTestFailed())return;
        }
    }
    void denseColorParity_data() {
        QTest::addColumn<int>("mode");
        QTest::newRow("mixed-hsl")<<2;
        QTest::newRow("three-stop-highlight-boundary")<<3;
    }
    void denseColorParity() {
        QFETCH(int,mode);
        QImage image(1024,1024,QImage::Format_RGBA64);
        std::mt19937 random(652819);
        for(int y=0;y<image.height();++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<image.width();++x) {
                const quint16 r=random()%60000, g=random()%60000, b=random()%60000;
                line[x]=QRgba64::fromRgba64(r,g,b,65535);
            }
        }
        AdjustmentState state;
        if(mode==2) {
            state.hue=-23;state.saturation=18;state.vibrance=22;
            state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        } else {state.exposure=3;state.saturation=-20;state.blacks=-50;state.whites=60;}
        verifyParity(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,"dense parity");
    }
    void knownBoundaryNeighborhoods() {
        constexpr quint16 seeds[][3]={{16492,36979,31231},{18743,43453,32369},{20306,54764,34044},
                                       {28853,59865,40358},{22323,46996,38591}};
        QImage image(257,15,QImage::Format_RGBA64);
        for(int y=0;y<15;++y) {
            auto *line=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<257;++x) {
                int channels[]{seeds[y/3][0],seeds[y/3][1],seeds[y/3][2]};
                channels[y%3]+=x-128;
                line[x]=QRgba64::fromRgba64(channels[0],channels[1],channels[2],65535);
            }
        }
        AdjustmentState state;state.exposure=3;state.saturation=-20;state.blacks=-50;state.whites=60;
        verifyParity(image,state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,"boundary neighborhoods");
    }
    void encodingsAndOutputSpaces_data() {
        QTest::addColumn<int>("encoding");QTest::addColumn<int>("output");
        for(int e=0;e<2;++e) for(int s=0;s<4;++s)
            QTest::newRow(qPrintable(QString("encoding-%1-output-%2").arg(e).arg(s)))<<e<<s;
    }
    void encodingsAndOutputSpaces() {
        QFETCH(int,encoding);QFETCH(int,output);
        QImage image(97,73,QImage::Format_RGBA64);
        std::mt19937 rng(993781);
        for(int y=0;y<image.height();++y) {
            auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(y));
            for(int x=0;x<image.width();++x) {
                const quint16 r=rng()%65536,g=rng()%65536,b=rng()%65536;
                row[x]=QRgba64::fromRgba64(r,g,b,65535);
            }
        }
        AdjustmentState state;state.temperature=30;state.tint=-20;state.exposure=.4;state.contrast=10;state.highlights=-20;
        const auto enc=encoding==0?ImagePipeline::InputEncoding::SRgb:ImagePipeline::InputEncoding::LinearProPhoto;
        verifyParity(image,state,enc,ColorManagement::OutputSpace(output),true,"encoding/output parity");
    }
    void displayPassHasCorrectOrientation() {
        QImage source(16,16,QImage::Format_RGBA64);
        for(int y=0;y<16;++y) {
            auto *line=reinterpret_cast<QRgba64 *>(source.scanLine(y));
            for(int x=0;x<16;++x) line[x]=QRgba64::fromRgba64(y<8?30000:2000,2000,y<8?2000:30000,65535);
        }
        auto computed=render(source,{},false);QVERIFY(!computed.image.isNull());
        std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,source.size(),1,QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));
        QVERIFY(color->create());
        std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
        std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());
        target->setRenderPassDescriptor(pass.get());QVERIFY(target->create());
        QRhiCommandBuffer *cb=nullptr;QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
        QVERIFY2(engine->draw(cb,target.get()),qPrintable(engine->error()));
        QRhiReadbackResult pixels;bool done=false;pixels.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(color.get()),&pixels);cb->resourceUpdate(updates);
        rhi->endOffscreenFrame();rhi->finish();QVERIFY(done);
        QImage actual(reinterpret_cast<const uchar *>(pixels.data.constData()),16,16,64,QImage::Format_RGBA8888);
        actual=rhi->isYUpInFramebuffer()?actual.mirrored():actual.copy();
        const auto reference=computed.image.convertToFormat(QImage::Format_RGBA8888);
        QCOMPARE(actual.pixelColor(3,3),reference.pixelColor(3,3));
        QCOMPARE(actual.pixelColor(3,12),reference.pixelColor(3,12));
    }
    void parameterChangesDoNotUploadSource() {
        QImage input(67,73,QImage::Format_RGBA32FPx4);input.fill(QColor::fromRgbF(.2,.3,.4));
        auto first=render(input,{});QVERIFY(!first.image.isNull());
        const auto uploads=engine->sourceUploads();
        AdjustmentState state;state.exposure=.5;
        auto second=render(input,state);QVERIFY(!second.image.isNull());
        QCOMPARE(engine->sourceUploads(),uploads);
        QVERIFY(first.image!=second.image);
    }
};
int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    GpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "GpuTests.moc"
