#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QDir>
#include <QFile>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QRgba64>
#include <cmath>
#include <optional>
#include "core/gpu/GpuEngine.h"
#include "core/scopes/ScopesEngine.h"
#include "core/scopes/ScopePlot.h"

class TangentialGpuTests : public QObject {
    Q_OBJECT
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    QString directory, failure;
    QJsonArray cases, plots;
    QImage lastPrepared, lastReference;
    std::optional<ProcessingPlan> lastPlan;
    quint64 revision=0, pixels=0;
    int maximum=0, failures=0;
    static QString digest(const QByteArray &bytes) {return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
    static QByteArray imageBytes(const QImage &image) {
        QByteArray bytes;
        const int width=image.width()*image.depth()/8;
        bytes.reserve(width*image.height());
        for(int y=0;y<image.height();++y) bytes.append(reinterpret_cast<const char *>(image.constScanLine(y)),width);
        return bytes;
    }
    bool write(const QString &name,const QByteArray &data) {
        const auto path=directory+"/"+name;
        if(QFile::exists(path)) {failure="Refusing evidence overwrite: "+path;return false;}
        QSaveFile file(path);
        if(!file.open(QIODevice::WriteOnly)||file.write(data)!=data.size()||!file.commit()) {failure="Evidence write: "+path;return false;}
        return true;
    }
    bool json(const QString &name,const QJsonObject &value) {return write(name,QJsonDocument(value).toJson());}
    bool stop(const QString &why) {failure=why;++failures;return false;}
    static QImage fixture(QSize size,bool scene) {
        QImage image(size,scene?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
        for(int y=0;y<size.height();++y) for(int x=0;x<size.width();++x) {
            if(scene) {
                auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
                p[0]=float(1.18+.007*x+.001*y);p[1]=float(.16+.002*x+.003*y);p[2]=float(-.085+.0013*x-.0002*y);
                p[3]=(x+3*y)%17==0?0.0f:(x+y)%11==0?1.0f/65535.0f:(2*x+y)%7==0?.375f:1.0f;
            } else {
                auto *p=reinterpret_cast<QRgba64 *>(image.scanLine(y));
                const quint16 alpha=(x+3*y)%17==0?0:(x+y)%11==0?1:(2*x+y)%7==0?23456:65535;
                p[x]=QRgba64::fromRgba64((x*367+y*119)%65536,(x*181+y*577)%65536,(x*937+y*271)%65536,alpha);
            }
        }
        return image;
    }
    static GeometryState geometry(int index) {
        GeometryState g;
        if(index==0)g.tangentialP1=.02;
        if(index==1)g.tangentialP1=-.02;
        if(index==2)g.tangentialP2=.02;
        if(index==3)g.tangentialP2=-.02;
        if(index==4){g.tangentialP1=.02;g.tangentialP2=-.02;}
        if(index==5){g.tangentialP1=-.016;g.tangentialP2=.012;g.distortion=.11;g.redCa=.6;g.blueCa=-.8;}
        if(index==6){g.tangentialP1=.009;g.tangentialP2=-.013;g.straighten=7;g.perspectiveHorizontal=.08;g.perspectiveVertical=-.06;}
        if(index==7){g.tangentialP1=-.012;g.tangentialP2=.014;g.crop={.07,.09,.84,.8};g.quarterTurns=1;g.flipHorizontal=true;g.distortion=-.07;}
        return g;
    }
    bool runCase(bool scene,int shape,int index,int output) {
        const QString label=QString("%1-%2-%3-%4").arg(scene?"float":"rgba64").arg(shape).arg(index).arg(output);
        const auto input=fixture(shape?QSize(83,117):QSize(117,83),scene);
        const auto g=geometry(index);
        const auto corrected=g.apply(input);
        if(corrected.isNull())return stop(label+": geometry refused");
        const auto prepared=ImagePipeline::floatSource(corrected);
        if(prepared.isNull()||!ImagePipeline::validFloatSource(prepared))return stop(label+": invalid staging");
        AdjustmentState state;state.exposure=.25;state.saturation=8;state.vibrance=6;
        auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output?ColorManagement::OutputSpace::DisplayP3:ColorManagement::OutputSpace::SRgb);
        const auto reference=ImagePipeline::processWithPlan(prepared,plan);
        if(reference.isNull())return stop(label+": CPU reference refused");
        QRhiCommandBuffer *cb=nullptr;
        if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return stop(label+": begin frame failed");
        QByteArray histogram;
        const quint64 requestRevision=++revision;
        quint64 returnedRevision=0,returnedPixels=0;
        const bool rendered=engine->process(cb,prepared,plan,requestRevision,[&](quint64 rev,QByteArray data,quint64 count){returnedRevision=rev;returnedPixels=count;histogram=std::move(data);},true);
        QRhiReadbackResult readback;bool done=false;
        if(rendered) {
            readback.completed=[&]{done=true;};
            auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);cb->resourceUpdate(updates);
        }
        rhi->endOffscreenFrame();rhi->finish();
        if(!rendered||!done)return stop(label+": "+engine->error());
        if(engine->lastProcessUsedCpuFallback())return stop(label+": unexpected CPU color fallback");
        const auto count=quint64(prepared.width())*prepared.height();
        if(readback.data.size()!=qint64(count)*16)return stop(label+": wrong full readback bytes");
        const auto *values=reinterpret_cast<const float *>(readback.data.constData());
        for(qsizetype i=0;i<readback.data.size()/qsizetype(sizeof(float));++i)if(!std::isfinite(values[i]))return stop(label+": nonfinite GPU channel");
        QImage floatOutput(reinterpret_cast<const uchar *>(readback.data.constData()),prepared.width(),prepared.height(),prepared.width()*16,QImage::Format_RGBA32FPx4);
        const auto actual=ImagePipeline::rgba64Source(floatOutput);
        if(actual.isNull()||actual.size()!=reference.size())return stop(label+": wrong converted dimensions");
        int maxRgb=0;quint64 alphaMismatches=0,over40=0;
        for(int y=0;y<actual.height();++y) {
            const auto *a=reinterpret_cast<const QRgba64 *>(actual.constScanLine(y));const auto *b=reinterpret_cast<const QRgba64 *>(reference.constScanLine(y));
            for(int x=0;x<actual.width();++x) {
                const int delta=std::max({std::abs(int(a[x].red())-int(b[x].red())),std::abs(int(a[x].green())-int(b[x].green())),std::abs(int(a[x].blue())-int(b[x].blue()))});
                maxRgb=std::max(maxRgb,delta);over40+=delta>40;alphaMismatches+=a[x].alpha()!=b[x].alpha();
            }
        }
        const auto stats=ScopesEngine::fromGpu(histogram,count),independent=ScopesEngine::analyze(actual);
        const bool histogramEqual=histogram.size()==HistogramCounts::GpuWords*int(sizeof(quint32))&&returnedRevision==requestRevision&&returnedPixels==count&&stats.error.isEmpty()&&stats.pixelCount==count&&stats.red==independent.red&&stats.green==independent.green&&stats.blue==independent.blue&&stats.luma==independent.luma&&stats.shadowClipPercent==independent.shadowClipPercent&&stats.highlightClipPercent==independent.highlightClipPercent;
        QJsonObject row{{"label",label},{"explicit_float_scene",scene},{"geometry",g.toJson()},{"width",actual.width()},{"height",actual.height()},{"pixels",qint64(count)},{"backend",engine->backendName()},{"cpu_colour_fallback",engine->lastProcessUsedCpuFallback()},{"GPU_bytes",readback.data.size()},{"GPU_FP32_SHA256",digest(readback.data)},{"CPU_RGBA64_SHA256",digest(imageBytes(reference))},{"prepared_FP32_SHA256",digest(imageBytes(prepared))},{"max_RGB16_error",maxRgb},{"over40",qint64(over40)},{"alpha_mismatches",qint64(alphaMismatches)},{"histogram_wire_bytes",histogram.size()},{"histogram_request_revision",qint64(requestRevision)},{"histogram_callback_revision",qint64(returnedRevision)},{"histogram_callback_pixels",qint64(returnedPixels)},{"four1024_histograms_clipping_exact",histogramEqual},{"passed",maxRgb<=40&&alphaMismatches==0&&histogramEqual}};
        if(!write(label+".gpu-fp32.bin",readback.data)||!write(label+".CPU-rgba64.bin",imageBytes(reference))||!write(label+".histogram.bin",histogram)||!json(label+".json",row))return stop(failure);
        cases.append(row);pixels+=count;maximum=std::max(maximum,maxRgb);
        if(!row["passed"].toBool())return stop(label+": parity/histogram gate refused");
        lastPrepared=prepared;lastReference=reference;lastPlan=plan;
        return true;
    }
private slots:
    void initTestCase() {
        directory=qEnvironmentVariable("JIXELLIGHT_TANGENTIAL_GPU_EVIDENCE");
        QVERIFY2(!directory.isEmpty(),"Owned evidence directory required");QVERIFY(QDir().mkpath(directory));
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
        QRhiGles2InitParams params;params.format=QSurfaceFormat::defaultFormat();surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));params.fallbackSurface=surface.get();rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        QVERIFY2(rhi&&rhi->isFeatureSupported(QRhi::Compute),"Working actual compute backend required; no skip acceptance");
        engine=std::make_unique<GpuEngine>(rhi.get());
        QVERIFY(json("actual-backend.json",{{"backend",engine->backendName()},{"QPA",QGuiApplication::platformName()},{"GPU_required",true}}));
    }
    void rgba64TangentialCompleteGpuColorAlphaAndRequestedHistogram() {
        for(int shape=0;shape<2;++shape)for(int index=0;index<8;++index)for(int space=0;space<2;++space)QVERIFY2(runCase(false,shape,index,space),qPrintable(failure));
    }
    void explicitFloatTangentialCompleteGpuColorAlphaAndRequestedHistogram() {
        for(int shape=0;shape<2;++shape)for(int index=0;index<8;++index)for(int space=0;space<2;++space)QVERIFY2(runCase(true,shape,index,space),qPrintable(failure));
    }
    void threeFullResolutionPlotImagesMatchIndependentEncodedCounts() {
        QVERIFY(lastPlan.has_value());QVERIFY(!lastPrepared.isNull());QVERIFY(!lastReference.isNull());
        for(const auto &mode:{QString("waveform"),QString("parade"),QString("vectorscope")}) {
            ScopePlotRequest request{lastPrepared,*lastPlan,GeometryState{},mode,revision,true};
            const auto actual=renderScopePlot(request,{});
            ScopePlotCounts independent(mode);QVERIFY(independent.add(lastReference));const auto reference=independent.image();
            const bool exact=actual.error.isEmpty()&&actual.pixels==quint64(lastPrepared.width())*lastPrepared.height()&&actual.image==reference;
            QJsonObject row{{"mode",mode},{"full_resolution",true},{"source_pixels",qint64(actual.pixels)},{"plot_width",actual.image.width()},{"plot_height",actual.image.height()},{"actual_plot_RGBA_SHA256",digest(imageBytes(actual.image))},{"independent_plot_RGBA_SHA256",digest(imageBytes(reference))},{"all_plot_pixels_exact",exact}};
            QVERIFY(json(mode+"-full-plot.json",row));QVERIFY(actual.image.save(directory+"/"+mode+"-full-plot.png"));plots.append(row);QVERIFY2(exact,qPrintable(actual.error));
        }
    }
    void cleanupTestCase() {
        if(engine){rhi->finish();engine.reset();}rhi.reset();surface.reset();
        const bool passed=failures==0&&cases.size()==64&&plots.size()==3;
        QVERIFY(json("complete-current-GPU-proof.json",{{"passed",passed},{"actual_case_count",cases.size()},{"pixels",qint64(pixels)},{"max_RGB16_error",maximum},{"failures",failures},{"cases",cases},{"full_plot_images",plots},{"source_scope","explicit synthetic integer/FP32 tangential geometry, not native FP32 RAW decode"}}));QVERIFY(passed);
    }
};
int main(int argc,char **argv) {
    // Same required compute context as the existing cross-platform GpuTests.
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);
    TangentialGpuTests tests;return QTest::qExec(&tests,argc,argv);
}
#include "TangentialGpuTests.moc"
