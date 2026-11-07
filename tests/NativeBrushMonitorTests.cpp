#include <QtTest>
#include <QtConcurrent>
#include <QApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QScreen>
#include <QWindow>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <rhi/qrhi.h>
#include "core/color/ColorManagement.h"
#include "core/color/MonitorColorTransform.h"
#include "core/gpu/GpuEngine.h"
#include "core/gpu/brush/NativeBrushFrame.h"
#include <cmath>
#include <cstring>
#ifdef Q_OS_MACOS
QByteArray nativeScreenIccWitness(QWindow *);
#endif
namespace N=NativeBrush;
namespace {
QByteArray digest(const QByteArray &b){return QCryptographicHash::hash(b,QCryptographicHash::Sha256).toHex();}
struct Prepared {
    N::CanonicalSource::Ptr source;N::UploadAdmission::Ptr upload;N::FrameAdmission::Ptr frame;
    BrushImageLease reference;QString error;Qt::HANDLE worker=nullptr;
};
QImage monitorOracle(const QByteArray &working,QSize input,QSize output,const QImage &lut) {
    QImage interpolated(output,QImage::Format_RGBA32FPx4);if(interpolated.isNull())return {};
    const auto fetch=[&](int x,int y,int c){x=std::clamp(x,0,input.width()-1);y=std::clamp(y,0,input.height()-1);
        float v;std::memcpy(&v,working.constData()+((y*input.width()+x)*4+c)*4,4);return v;};
    for(int y=0;y<output.height();++y)for(int x=0;x<output.width();++x){
        const float px=(float(x)+.5f)/float(output.width())*float(input.width())-.5f;
        const float py=(float(y)+.5f)/float(output.height())*float(input.height())-.5f;
        const int ax=int(std::floor(px)),ay=int(std::floor(py));const float fx=px-float(ax),fy=py-float(ay);
        auto *p=reinterpret_cast<float *>(interpolated.scanLine(y))+x*4;
        for(int c=0;c<4;++c){const float a=fetch(ax,ay,c)*(1-fx)+fetch(ax+1,ay,c)*fx;
            const float b=fetch(ax,ay+1,c)*(1-fx)+fetch(ax+1,ay+1,c)*fx;p[c]=a*(1-fy)+b*fy;}
    }
    interpolated.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return MonitorColorTransform::applyLut(ImagePipeline::rgba64Source(interpolated),lut);
}
}
class NativeBrushMonitorTests final:public QObject {
    Q_OBJECT
    std::unique_ptr<QRhi> rhi;std::unique_ptr<QOffscreenSurface> surface;
    QJsonArray records;quint64 explicitDiagnosticFinishes=0;QElapsedTimer clock;
    void endFrame(){rhi->endOffscreenFrame();rhi->finish();++explicitDiagnosticFinishes;}
    QByteArray readWorking(QRhiTexture *texture) {
        QRhiCommandBuffer *cb=nullptr;if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
        QRhiReadbackResult result;bool done=false;result.completed=[&]{done=true;};
        auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(texture),&result);cb->resourceUpdate(updates);
        endFrame();return done?result.data:QByteArray{};
    }
    void nativeCase(bool fp32) {
        const QSize size(79,53);QImage image(size,fp32?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
        const float alphas[]{0.f,1.f/65535.f,.25f,.64f,1.f};
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){
            if(fp32){auto *p=reinterpret_cast<float *>(image.scanLine(y))+x*4;p[0]=-.04f+1.7f*x/78.f;p[1]=.03f+1.2f*y/52.f;
                p[2]=.01f+1.4f*((x*17+y*11)%89)/88.f;p[3]=alphas[x%5];}
            else {auto *p=reinterpret_cast<QRgba64 *>(image.scanLine(y));p[x]=QRgba64::fromRgba64(quint16(8000+(x*731+y*59)%54000),
                quint16(5000+(y*997+x*31)%56000),quint16(3000+(x*313+y*419)%58000),quint16(std::lround(alphas[x%5]*65535.f)));}
        }
        auto budget=std::make_shared<BrushRenderBudget>();auto engine=std::make_unique<GpuEngine>(rhi.get());
        auto domain=std::make_unique<N::RenderDomain>(rhi.get(),engine->nativeBrushShaders());const auto caps=domain->capabilities();QVERIFY(caps.active());
        AdjustmentState state;BrushMaskGeometry::Stroke paint;paint.style={.12,.42,.8};paint.dabs={{{.28,.37},.5},{{.69,.62},.9}};
        auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.3,.38},.3}};
        state.brushMasks.layers.push_back({"monitor-native",true,.85,{paint,erase}});
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
        Prepared prepared;
        {
            QFutureWatcher<Prepared> watcher;
            watcher.setFuture(QtConcurrent::run([=] {
                Prepared p;p.worker=QThread::currentThreadId();N::SourceFailure sourceFailure;N::FrameFailure frameFailure;
                p.source=N::CanonicalSource::capture(image,{fp32?"owned-monitor-fp32":"owned-monitor-rgba64",plan.encoding,false,0,{0,0,1,1}},caps.threads(),budget,&sourceFailure);
                if(!p.source){p.error="canonical capture";return p;}
                p.upload=N::UploadAdmission::prepare(p.source,caps,&sourceFailure);
                p.frame=N::FrameAdmission::prepare(p.source,caps,plan,{"owned-monitor-catalog","owned-input","owned-version",1},1,&frameFailure);
                auto request=p.source->exactRequest(plan);if(request)p.reference=request->render({},&p.error);
                if(!p.upload||!p.frame||!p.reference)p.error="CPU admission/reference failed";return p;
            }));
            // Event-driven completion; never wait/result on a pending future.
            QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),10000);prepared=watcher.result();
        }
        QVERIFY2(prepared.error.isEmpty(),qPrintable(prepared.error));QVERIFY(prepared.worker!=QThread::currentThreadId());
        auto input=domain->attach(prepared.upload);QVERIFY(bool(input));QRhiCommandBuffer *cb=nullptr;
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->upload(input,cb));endFrame();domain->collect();QVERIFY(domain->ready(input));
        QVERIFY(domain->activate(prepared.frame->stamp()));auto frame=domain->attachFrame(input,prepared.frame);QVERIFY(bool(frame));
        QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);QVERIFY(domain->compute(frame,cb));endFrame();domain->collect();QVERIFY(domain->ready(frame));
        const auto before=readWorking(domain->texture(frame));QCOMPARE(before.size(),size.width()*size.height()*16);
        int rgb=0,alpha=0;quint64 nonfinite=0;
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x){
            const auto expected=reinterpret_cast<const QRgba64 *>(prepared.reference.image().constScanLine(y))[x];const int reference[]{expected.red(),expected.green(),expected.blue(),expected.alpha()};
            for(int c=0;c<4;++c){float actual;std::memcpy(&actual,before.constData()+((y*size.width()+x)*4+c)*4,4);
                if(!std::isfinite(actual)){++nonfinite;continue;}const int error=std::abs(int(std::lround(std::clamp(actual,0.f,1.f)*65535))-reference[c]);
                if(c<3)rgb=std::max(rgb,error);else alpha=std::max(alpha,error);}
        }
        QVERIFY2(rgb<=40,qPrintable(QString::number(rgb)));QCOMPARE(alpha,0);QCOMPARE(nonfinite,quint64(0));
        const auto icc=ColorManagement::iccProfile(ColorManagement::OutputSpace::DisplayP3);
        const auto profile=MonitorColorTransform::profileFromColorSpace(QColorSpace::fromIccProfile(icc),"owned Display P3");QVERIFY(profile.valid);QCOMPARE(profile.icc,icc);
        QString error;const auto p3=MonitorColorTransform::srgbToMonitorLut(profile.icc,33,&error);QVERIFY2(!p3.isNull(),qPrintable(error));
        const QImage identity=MonitorColorTransform::identityLut();QVERIFY(identity!=p3);
        const auto traceBefore=domain->trace();QJsonArray draws;int maxDisplay=0;bool nonidentityObserved=false;
        for(const auto targetSize:{size,QSize(113,71),QSize(47,31)}) {
            std::unique_ptr<QRhiTexture> color(rhi->newTexture(QRhiTexture::RGBA8,targetSize,1,QRhiTexture::RenderTarget|QRhiTexture::UsedAsTransferSource));QVERIFY(color->create());
            std::unique_ptr<QRhiTextureRenderTarget> target(rhi->newTextureRenderTarget({QRhiColorAttachment(color.get())}));
            std::unique_ptr<QRhiRenderPassDescriptor> pass(target->newCompatibleRenderPassDescriptor());target->setRenderPassDescriptor(pass.get());QVERIFY(target->create());
            QByteArray identityPixels;
            for(int mode:{0,1,0}) {
                const auto &lut=mode?p3:identity;engine->setDisplayColorLut(lut,mode?profile.key:"owned-identity","controlled monitor profile");
                QCOMPARE(rhi->beginOffscreenFrame(&cb),QRhi::FrameOpSuccess);
                QVERIFY(domain->present(frame,cb,[&](QRhiTexture *working){return engine->drawExternalWorkingTexture(cb,target.get(),working);}));
                QRhiReadbackResult read;bool done=false;read.completed=[&]{done=true;};auto *updates=rhi->nextResourceUpdateBatch();
                updates->readBackTexture(QRhiReadbackDescription(color.get()),&read);cb->resourceUpdate(updates);endFrame();domain->collect();QVERIFY(done);
                QCOMPARE(read.data.size(),targetSize.width()*targetSize.height()*4);
                QImage actual(reinterpret_cast<const uchar*>(read.data.constData()),targetSize.width(),targetSize.height(),targetSize.width()*4,QImage::Format_RGBA8888);
                actual=rhi->isYUpInFramebuffer()?actual.mirrored():actual.copy();
                const auto expected=monitorOracle(before,size,targetSize,lut);QVERIFY(!expected.isNull());int maximum=0;
                for(int y=0;y<targetSize.height();++y)for(int x=0;x<targetSize.width();++x){
                    const auto p=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y))[x];const auto got=actual.pixelColor(x,y);
                    const double a=p.alpha()/65535.;const int reference[]{int(std::lround(p.red()/65535.*a*255)),int(std::lround(p.green()/65535.*a*255)),int(std::lround(p.blue()/65535.*a*255)),int(std::lround(a*255))};
                    const int observed[]{got.red(),got.green(),got.blue(),got.alpha()};for(int c=0;c<4;++c)maximum=std::max(maximum,std::abs(observed[c]-reference[c]));
                }
                QVERIFY2(maximum<=1,qPrintable(QStringLiteral("display error%1 mode%2 %3x%4").arg(maximum).arg(mode).arg(targetSize.width()).arg(targetSize.height())));
                maxDisplay=std::max(maxDisplay,maximum);const auto bytes=QByteArray(reinterpret_cast<const char*>(actual.constBits()),actual.sizeInBytes());
                if(mode==0&&identityPixels.isEmpty())identityPixels=bytes;else if(mode==1)nonidentityObserved=nonidentityObserved||bytes!=identityPixels;else QCOMPARE(bytes,identityPixels);
                QCOMPARE(readWorking(domain->texture(frame)),before);
                draws.append(QJsonObject{{"width",targetSize.width()},{"height",targetSize.height()},{"profile",mode?"DisplayP3":"identity"},{"max_RGBA8_error",maximum},{"original_working_bytes_unchanged",true}});
            }
        }
        QVERIFY(nonidentityObserved);QCOMPARE(domain->trace().coverageDispatches,traceBefore.coverageDispatches);QCOMPARE(domain->trace().colourDispatches,traceBefore.colourDispatches);
        QCOMPARE(engine->sourceUploads(),quint64(0));QCOMPARE(engine->renderedRevision(),quint64(0));
        QCOMPARE(domain->normalFinishCalls(),quint64(0));const auto peak=budget->snapshot().peak;
        domain->release(frame);domain->release(input);domain->collect();domain->shutdown();domain.reset();engine.reset();prepared={};
        QCOMPARE(budget->snapshot().charged,qint64(0));QVERIFY(peak<=3LL*1024*1024*1024);
        records.append(QJsonObject{{"name",fp32?"signedFP32NativeMonitor":"rgba64NativeMonitor"},{"passed",true},{"owned_input",true},{"real_RAW",false},
            {"native_pixels",size.width()*size.height()},{"all_working_pixels_CPU_compared",true},{"max_RGB16_error",rgb},{"max_alpha16_error",alpha},{"nonfinite",qint64(nonfinite)},
            {"coverage_dispatches",qint64(traceBefore.coverageDispatches)},{"colour_dispatches",qint64(traceBefore.colourDispatches)},{"source_uploads",qint64(traceBefore.sourceUploads)},
            {"draws",draws},{"max_display_RGBA8_error",maxDisplay},{"nonidentity_DisplayP3_observed",nonidentityObserved},{"ICC_sha256",profile.key},
            {"monitor_oracle_uses_actual_GPU_output_not_CPU_colour_bin_identity",true},{"original_engine_colour_process_or_CPU_upload_used",false},{"working_SHA256",QString::fromLatin1(digest(before))},
            {"peak_operation_reserved_bytes",peak},{"final_charged_bytes",0},{"normal_domain_finish_calls",0}});
    }
private slots:
    void initTestCase() {
        clock.start();
#ifdef Q_OS_MACOS
        QRhiMetalInitParams params;rhi.reset(QRhi::create(QRhi::Metal,&params));
#elif defined(Q_OS_WIN)
        QRhiD3D11InitParams params;rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#else
        QRhiGles2InitParams params;params.format=QSurfaceFormat::defaultFormat();surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));params.fallbackSurface=surface.get();rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        QVERIFY2(rhi&&rhi->isFeatureSupported(QRhi::Compute),"A real compute backend is mandatory for these new checks");
    }
    void actualScreenProfileAndInvalidValueSafety() {
        QVERIFY(!MonitorColorTransform::profileFromColorSpace(QColorSpace{}).valid);QVERIFY(!MonitorColorTransform::profileForScreen(nullptr).valid);
#ifdef Q_OS_MACOS
        QCOMPARE(QGuiApplication::platformName(),QStringLiteral("cocoa"));
#endif
        QWindow window;window.resize(240,160);window.show();QTRY_VERIFY_WITH_TIMEOUT(window.isExposed(),5000);
        const auto profile=MonitorColorTransform::profileForScreen(window.screen());
#ifdef Q_OS_MACOS
        const auto native=nativeScreenIccWitness(&window);QVERIFY(!native.isEmpty());QVERIFY(profile.valid);QCOMPARE(profile.icc,native);QCOMPARE(profile.key,QString::fromLatin1(digest(native)));
        QFutureWatcher<MonitorColorProfile> watcher;auto *screen=window.screen();watcher.setFuture(QtConcurrent::run([screen]{return MonitorColorTransform::profileForScreen(screen);}));
        QTRY_VERIFY_WITH_TIMEOUT(watcher.isFinished(),5000);QVERIFY(!watcher.result().valid);
#endif
        records.append(QJsonObject{{"name","actualScreenProfileAndInvalidValueSafety"},{"passed",true},{"system_profile_valid",profile.valid},
            {"system_profile_key",profile.key},{"system_profile_bytes",profile.icc.size()},{"system_profile_description",profile.description},
            {"actual_screen",window.screen()->name()},{"read_only_OS_profile",true},{"OS_profile_or_preferences_changed",false}});
    }
    void rgba64NativeMonitor(){nativeCase(false);}
    void signedFP32NativeMonitor(){nativeCase(true);}
    void cleanupTestCase() {
        const auto backend=rhi?int(rhi->backend()):-1;rhi.reset();surface.reset();
        const QJsonObject report{{"passed",records.size()==3},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"actual_qpa",QGuiApplication::platformName()},
            {"backend_enum",backend},{"new_cases",records},{"elapsed_ms",clock.elapsed()},{"explicit_offscreen_diagnostic_finish_calls",qint64(explicitDiagnosticFinishes)},
            {"production_frame_by_frame_finish_claim",false},{"physical_monitor_reconfiguration_or_device_loss",false},{"FP32_RAW_decode",false},
            {"full_Sony_or_all_backend_or_package_acceptance",false},{"Root_modified",false}};
        const auto path=qEnvironmentVariable("JIXELLIGHT_NATIVE_MONITOR_REPORT");if(!path.isEmpty()){
            QSaveFile f(path);const auto bytes=QJsonDocument(report).toJson();QVERIFY(f.open(QIODevice::WriteOnly));QCOMPARE(f.write(bytes),bytes.size());QVERIFY(f.commit());
        }
        qInfo().noquote()<<"NATIVE_MONITOR_EVIDENCE"<<QJsonDocument(report).toJson(QJsonDocument::Compact);
    }
};
int main(int argc,char **argv){QSurfaceFormat f;f.setVersion(4,3);f.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(f);QApplication app(argc,argv);NativeBrushMonitorTests test;return QTest::qExec(&test,argc,argv);}
#include "NativeBrushMonitorTests.moc"
