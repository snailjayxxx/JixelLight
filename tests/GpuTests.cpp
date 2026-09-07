#include <QtTest>
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QRgba64>
#include <random>
#include <cstring>
#include <cmath>
#include "core/gpu/GpuEngine.h"
#include "core/scopes/ScopesEngine.h"
#include "core/look/LookProfiles.h"

class GpuTests : public QObject {
    Q_OBJECT
private:
    std::unique_ptr<QOffscreenSurface> surface;
    std::unique_ptr<QRhi> rhi;
    std::unique_ptr<GpuEngine> engine;
    quint64 revision=0;
    struct Result { QImage image; ScopesResult histogram; };
    Result render(const QImage &input,const AdjustmentState &state,bool statistics=true,
                  ImagePipeline::InputEncoding encoding=ImagePipeline::InputEncoding::LinearProPhoto,
                  ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb) {
        QRhiCommandBuffer *cb=nullptr;
        if (rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess) return {};
        QByteArray counts;
        const auto floatImage=input.format()==QImage::Format_RGBA32FPx4 ? input : input.convertToFormat(QImage::Format_RGBA32FPx4);
        const auto callback=statistics ? GpuEngine::HistogramReady([&](quint64,QByteArray data,quint64){ counts=std::move(data); }) : GpuEngine::HistogramReady{};
        const bool ok=engine->process(cb,floatImage,ProcessingPlan::compile(state,encoding,output),++revision,callback,true);
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
        return {image.copy().convertToFormat(QImage::Format_RGBA64),ScopesEngine::fromGpu(counts,quint64(input.width())*input.height())};
    }
    void verifyParity(const QImage &input,const AdjustmentState &state,
                      ImagePipeline::InputEncoding encoding,ColorManagement::OutputSpace space,
                      bool statistics,const char *label) {
        const auto actual=render(input,state,statistics,encoding,space);
        QVERIFY2(!actual.image.isNull(),qPrintable(engine->error()));
        const auto reference=ImagePipeline::process(input,state,encoding,space);
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
        constexpr quint16 seeds[][3]={{16492,36979,31231},{18743,43453,32369},{20306,54764,34044}};
        QImage image(257,9,QImage::Format_RGBA64);
        for(int y=0;y<9;++y) {
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
