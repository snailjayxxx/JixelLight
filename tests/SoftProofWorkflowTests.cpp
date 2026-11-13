#include <QtTest>
#include <QApplication>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QPointer>
#include <QThread>
#include <thread>
#include "app/PhotoController.h"
#include "core/cache/RenderedPreviewCache.h"
#include "core/color/MonitorColorTransform.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/preview/SoftProofPreview.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/ActionTrace.h"

class SoftProofWorkflowTests:public QObject {
    Q_OBJECT
    static QByteArray pixels(const QImage &image){QCryptographicHash h(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)h.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*image.depth()/8));return h.result();}
    static QImage scene(){QImage image(83,79,QImage::Format_RGBA32FPx4);for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x){auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;p[0]=.02f+.35f*x/82;p[1]=.01f+.3f*y/78;p[2]=.13f;p[3]=(x+y)%5?.7f:0;}return image;}
    static AdjustmentState edits(){AdjustmentState s;s.denoiseLuma=61;s.denoiseChroma=73;s.dehazeAmount=31;s.textureAmount=29;s.clarityAmount=17;s.sharpenAmount=31;s.colorMaskEnabled=true;s.colorMaskExposure=.4;s.colorMaskHueRange=180;s.colorMaskChromaRange=1;s.colorMaskLightnessRange=1;s.luminanceMaskEnabled=true;s.luminanceMaskExposure=-.3;return s;}
    static ProcessingPlan plan(const AdjustmentState &state){return ProcessingPlan::compileRetainingIntent(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::ProPhotoRgb,false,0);}
    static QString fixture(QTemporaryDir &dir,const QString &name="source.png",int shift=0){QImage image(307,211,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::SRgb));for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64((x*199+y*13+shift)%65536,(y*291+x*23+shift)%65536,(x*71+y*113)%65536,(x+y)%7?47000:0);const auto path=dir.filePath(name);return image.save(path)?path:QString{};}
    static void defaults(PhotoController &c){c.setGpuEnabled(false);c.setSoftProofTarget("srgb");c.setSoftProofPaper(false);c.setSoftProofGamutWarning(false);c.setSoftProofBlackPoint(true);c.setSoftProofIntent(1);c.setViewport(317,211,1,0,.5,.5);}
    static QImage provided(ProcessedImageProvider &p,const QString &url,QSize requested={}){return p.requestImage(url.mid(QString("image://processed/").size()),nullptr,requested);}
private slots:
    void rendererMatchesWideReferenceAndKeepsSpatialCachesAcrossExposureEdits(){
        auto budget=std::make_shared<BrushRenderBudget>(256*1024*1024);auto cache=std::make_shared<RenderedPreviewCache>(8*1024*1024,budget);SoftProofPreviewRenderer renderer(budget,cache);
        const auto source=scene();const auto original=pixels(source);auto state=edits();SoftProofPreviewRequest request{source,plan(state),SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace::SRgb),{}};
        auto first=renderer.render(request,{});QVERIFY2(!first.image.isNull(),qPrintable(first.error));auto wide=ImagePipeline::processWithPlan(source,request.plan);wide.setColorSpace(SoftProof::workingColorSpace());const auto expected=SoftProof::transform(wide,request.profile,request.settings);QCOMPARE(pixels(first.image),pixels(expected.image));
        const auto before=cache->snapshot();state.exposure=.3;request.plan=plan(state);auto next=renderer.render(request,{});QVERIFY2(!next.image.isNull(),qPrintable(next.error));const auto after=cache->snapshot();for(const auto key:{"denoise_stage_hits","dehaze_stage_hits","texture_stage_hits","clarity_stage_hits"})QVERIFY2(after[key].toDouble()>before[key].toDouble(),key);
        QCOMPARE(pixels(source),original);QVERIFY(budget->snapshot().charged>0);first.image={};next.image={};renderer.clearCachedSource();QCOMPARE(budget->snapshot().charged,qint64(0));
    }
    void committedPaintEraseProofUsesStrictBrushAdmissionAndReleasesAllLeases(){
        auto budget=std::make_shared<BrushRenderBudget>(256*1024*1024);SoftProofPreviewRenderer renderer(budget,{});
        const auto source=scene();const auto sourceHash=pixels(source);auto state=edits();
        BrushMaskGeometry::Stroke paint;paint.style={.08,.6,.8};paint.dabs={{{.2,.3},.8},{{.7,.6},.5}};
        auto erase=paint;erase.mode=BrushMaskGeometry::StrokeMode::Erase;erase.dabs={{{.2,.3},.4}};
        state.brushMasks.layers={{"owned-proof-brush",true,.8,{paint,erase}}};
        SoftProofPreviewRequest request{source,plan(state),SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace::AdobeRgb),{}};
        auto result=renderer.render(request,{});QVERIFY2(!result.image.isNull(),qPrintable(result.error));
        QString error;auto admitted=BrushRenderRequest::createPrepared(source,request.plan,BrushRenderRequest::Layout::Whole,{},&error,budget);
        QVERIFY2(admitted,qPrintable(error));auto wide=admitted->render({},&error).sharedImage();QVERIFY2(!wide.isNull(),qPrintable(error));wide.setColorSpace(SoftProof::workingColorSpace());
        const auto expected=SoftProof::transform(wide,request.profile,request.settings);QCOMPARE(pixels(result.image),pixels(expected.image));QCOMPARE(pixels(source),sourceHash);
        QVERIFY(result.facts["cpu_operation_reserved_bytes"].toDouble()>0);wide={};admitted.reset();result.image={};renderer.clearCachedSource();QCOMPARE(budget->snapshot().charged,qint64(0));
    }
    void boundedResizeIsReusedAndItsChargeCanBeReleased(){
        auto budget=std::make_shared<BrushRenderBudget>(128*1024*1024);auto cache=std::make_shared<RenderedPreviewCache>(1024*1024,budget);SoftProofPreviewRenderer renderer(budget,cache,512);const auto source=scene();const auto original=pixels(source);
        SoftProofPreviewRequest request{source,plan({}),SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace::SRgb),{}};auto first=renderer.render(request,{});QVERIFY2(!first.image.isNull(),qPrintable(first.error));QVERIFY(qint64(first.image.width())*first.image.height()<=512);QVERIFY(first.facts["source_scaled_for_proof"].toBool());
        const auto before=PerformanceRecorder::snapshot()["counters"].toObject()["soft_proof_scaled_source_hits"].toDouble();request.settings.gamutWarning=true;auto second=renderer.render(request,{});QVERIFY(!second.image.isNull());QVERIFY(PerformanceRecorder::snapshot()["counters"].toObject()["soft_proof_scaled_source_hits"].toDouble()>before);QCOMPARE(pixels(source),original);
        first.image={};second.image={};QVERIFY(budget->snapshot().charged>0);renderer.clearCachedSource();QCOMPARE(budget->snapshot().charged,qint64(0));
    }
    void memoryRefusalAndCancellationNeverPublishUnadmittedFrames(){
        auto budget=std::make_shared<BrushRenderBudget>(1);SoftProofPreviewRenderer renderer(budget,{});SoftProofPreviewRequest request{scene(),plan({}),SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace::SRgb),{}};const auto refused=renderer.render(request,{});QVERIFY(refused.image.isNull());QVERIFY(refused.retryableBudget);QCOMPARE(budget->snapshot().charged,qint64(0));auto cancel=std::make_shared<std::atomic_bool>(true);const auto stopped=renderer.render(request,cancel);QVERIFY(stopped.cancelled);QVERIFY(stopped.image.isNull());
    }
    void providerKeepsProofSeparateAndAliasesRetainChargeWithoutUnboundedResize(){
        auto budget=std::make_shared<BrushRenderBudget>(128*1024*1024);SoftProofPreviewRenderer renderer(budget,{});SoftProofPreviewRequest request{scene(),plan({}),SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace::SRgb),{}};auto result=renderer.render(request,{});QVERIFY(!result.image.isNull());ProcessedImageProvider provider;QImage normal(11,13,QImage::Format_RGBA64);normal.fill(Qt::red);provider.setImage(normal);provider.setReference(normal);const auto original=pixels(provider.requestImage("normal",nullptr,{}));
        QVERIFY(provider.setProofPreview("proof-live/one",result.image));auto held=provider.requestImage("proof-live/one",nullptr,{99999,99999});QCOMPARE(held.size(),result.image.size());QCOMPARE(pixels(provider.requestImage("normal",nullptr,{})),original);QCOMPARE(pixels(provider.requestImage("reference/one",nullptr,{})),original);QVERIFY(provider.renameProofPreview("proof-live/two"));QVERIFY(provider.requestImage("proof-live/one",nullptr,{}).isNull());
        result.image={};provider.clearProofPreview();QVERIFY(budget->snapshot().charged>0);held={};renderer.clearCachedSource();QCOMPARE(budget->snapshot().charged,qint64(0));
    }
    void changedPackedPlansAndBrushlessBrushRequestsRemainRefused(){
        auto budget=std::make_shared<BrushRenderBudget>(128*1024*1024);SoftProofPreviewRenderer renderer(budget,{});SoftProofPreviewRequest request{scene(),plan({}),SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace::SRgb),{}};
        QString error;QVERIFY(!BrushRenderRequest::validPreparedPlan(request.plan,&error));QVERIFY(!BrushRenderRequest::createPrepared(request.source,request.plan,BrushRenderRequest::Layout::Whole,{},&error,budget));
        request.plan.data[0].x+=.1f;const auto invalid=renderer.render(request,{});QVERIFY(invalid.image.isNull());QVERIFY(!invalid.retryableBudget);QCOMPARE(budget->snapshot().charged,qint64(0));
    }
    void sharedCommandsPreserveAllPhotoStateAndNeverOpenAnUnloadedCustomProfile(){
        {QSettings saved;saved.remove("softProof/profilePath");saved.remove("softProof/profileKey");}
        QTemporaryDir dir;const auto source=fixture(dir),profile=dir.filePath("owned.icc");QVERIFY(!source.isEmpty());
        const auto icc=QColorSpace(QColorSpace::SRgb).iccProfile();QFile file(profile);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(file.write(icc),icc.size());file.close();
        ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);
        const auto settingsBefore=c.softProofDiagnostics();QVERIFY(!c.executeCommand({{"command","proof.target"},{"target","custom"}}));QCOMPARE(c.softProofDiagnostics(),settingsBefore);
        QVERIFY(c.importFile(QUrl::fromLocalFile(source)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering()&&c.scopesPixelCount()==307*211,10000);
        c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QFile original(source);QVERIFY(original.open(QIODevice::ReadOnly));const auto originalBytes=original.readAll();original.close();
        const auto state=c.gpuPlan(true).state.toJson();const auto history=c.editHistory();const auto revision=c.renderRevision();
        const auto red=c.redHistogram(),green=c.greenHistogram(),blue=c.blueHistogram(),luma=c.lumaHistogram();
        for(const auto &command:QList<QVariantMap>{
            {{"command","proof.profile"},{"source",profile}},{{"command","proof.target"},{"target","adobe-rgb"}},{{"command","proof.target"},{"target","custom"}},
            {{"command","proof.intent"},{"intent",3}},{{"command","proof.intent"},{"intent",0}},{{"command","proof.intent"},{"intent",1}},
            {{"command","proof.enable"},{"enabled",true}},{{"command","proof.paper"},{"enabled",true}},{{"command","proof.gamut-warning"},{"enabled",true}},
            {{"command","proof.black-point"},{"enabled",false}},{{"command","proof.refresh"}}})QVERIFY(c.executeCommand(command));
        QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);QVERIFY(!provided(provider,c.softProofUrl()).isNull());
        const auto url=c.softProofUrl();QVERIFY(!c.executeCommand({{"command","proof.enable"},{"enabled",1}}));
        QVERIFY(!c.executeCommand({{"command","proof.profile"},{"source",profile},{"overwrite",true}}));
        QVERIFY(!c.executeCommand({{"command","edit.batch"},{"commands",QJsonArray{QJsonObject{{"command","develop.set"},{"parameter","exposure"},{"value",2}},QJsonObject{{"command","proof.paper"},{"enabled",false}}}}}));
        QCOMPARE(c.softProofUrl(),url);QVERIFY(c.softProofPaper());QCOMPARE(c.gpuPlan(true).state.toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.renderRevision(),revision);
        QCOMPARE(c.redHistogram(),red);QCOMPARE(c.greenHistogram(),green);QCOMPARE(c.blueHistogram(),blue);QCOMPARE(c.lumaHistogram(),luma);
        QVERIFY(c.executeCommand({{"command","proof.enable"},{"enabled",false}}));QVERIFY(c.softProofUrl().isEmpty());QVERIFY(provided(provider,url).isNull());QVERIFY(c.projectPath().isEmpty());
        QVERIFY(original.open(QIODevice::ReadOnly));QCOMPARE(original.readAll(),originalBytes);QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),icc);
    }
    void foreignProofEntriesPreservePreferencesAndAcceptedCallbacksCanDestroyTheOwner(){
        QTemporaryDir dir;const auto profile=dir.filePath("owned.icc");const auto icc=QColorSpace(QColorSpace::SRgb).iccProfile();QFile file(profile);QVERIFY(file.open(QIODevice::WriteOnly|QIODevice::NewOnly));QCOMPARE(file.write(icc),icc.size());file.close();
        PhotoController c(nullptr);const auto before=c.softProofDiagnostics();const auto preferences=[] {QSettings saved;QVariantMap result;for(const auto &key:saved.allKeys())result.insert(key,saved.value(key));return result;};const auto stored=preferences();QSignalSpy changed(&c,&PhotoController::softProofChanged);
        const QList<QVariantMap> commands{
            {{"command","proof.profile"},{"source",profile}},{{"command","proof.target"},{"target","adobe-rgb"}},{{"command","proof.intent"},{"intent",2}},
            {{"command","proof.enable"},{"enabled",true}},{{"command","proof.paper"},{"enabled",false}},{{"command","proof.gamut-warning"},{"enabled",false}},
            {{"command","proof.black-point"},{"enabled",true}},{{"command","proof.refresh"}}};
        bool foreign=false,accepted=false,loaded=true,targeted=true,intent=true;
        std::thread worker([&]{foreign=QThread::currentThread()!=c.thread();for(const auto &command:commands)accepted=c.executeCommand(command)||accepted;
            c.setSoftProofEnabled(true);targeted=c.setSoftProofTarget("custom");loaded=c.loadSoftProofProfile(QUrl::fromLocalFile(profile));intent=c.setSoftProofIntent(2);
            c.setSoftProofPaper(false);c.setSoftProofGamutWarning(false);c.setSoftProofBlackPoint(true);c.refreshSoftProof();c.chooseSoftProofProfile();});worker.join();
        QVERIFY(foreign);QVERIFY(!accepted);QVERIFY(!loaded);QVERIFY(!targeted);QVERIFY(!intent);QCOMPARE(changed.count(),0);QCOMPARE(c.softProofDiagnostics(),before);QCOMPARE(preferences(),stored);
        QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),icc);
        auto owner=std::make_unique<PhotoController>(nullptr);QPointer<PhotoController> live(owner.get());bool destroyed=false;
        connect(owner.get(),&PhotoController::softProofChanged,this,[&]{if(!destroyed){destroyed=true;owner.reset();}});
        QVERIFY(owner->executeCommand({{"command","proof.enable"},{"enabled",true}}));QVERIFY(destroyed);QVERIFY(!owner);QVERIFY(!live);
        const auto row=ActionTrace::instance().snapshot().last().toObject();QCOMPARE(row["action"].toString(),QString("proof_command"));QVERIFY(row["presentation_only"].toBool());QCOMPARE(row["completion"].toString(),QString("presentation-request-accepted"));
    }
    void controllerProofDoesNotChangeEditsHistoryRevisionScopesOrSource(){
        QTemporaryDir dir;const auto path=fixture(dir);QVERIFY(!path.isEmpty());QFile file(path);QVERIFY(file.open(QIODevice::ReadOnly));const auto original=file.readAll();file.close();ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()==307*211&&c.redHistogram().size()==1024,10000);
        const auto state=c.gpuPlan(true).state.toJson();const auto history=c.editHistory();const auto revision=c.renderRevision();const auto bins=c.redHistogram();c.setSoftProofEnabled(true);QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);QCOMPARE(c.gpuPlan(true).state.toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(c.renderRevision(),revision);QCOMPARE(c.redHistogram(),bins);QVERIFY(!provided(provider,c.softProofUrl()).isNull());
        c.setSoftProofPaper(true);c.setSoftProofGamutWarning(true);QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);QCOMPARE(c.renderRevision(),revision);QCOMPARE(c.gpuPlan(true).state.toJson(),state);c.setSoftProofEnabled(false);QVERIFY(!c.softProofReady());QVERIFY(c.softProofUrl().isEmpty());QVERIFY(file.open(QIODevice::ReadOnly));QCOMPARE(file.readAll(),original);
    }
    void editPhotoAndViewportChangesInvalidatePreviousProofImmediately(){
        QTemporaryDir dir;const auto a=fixture(dir,"first.png"),b=fixture(dir,"second.png",9000);ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);QVERIFY(c.importFile(QUrl::fromLocalFile(a)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);c.setSoftProofEnabled(true);QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);const auto old=c.softProofUrl();
        c.setExposure(.7);QVERIFY(!c.softProofReady());QVERIFY(provided(provider,old).isNull());c.setSoftProofTarget("adobe-rgb");c.setSoftProofPaper(true);c.setExposure(-.3);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);QCOMPARE(c.softProofDiagnostics()["parameter_revision"].toString(),QString::number(c.renderRevision()));
        QVERIFY(c.importFile(QUrl::fromLocalFile(b)));c.selectPhoto(1);QVERIFY(!c.softProofReady());QTRY_VERIFY_WITH_TIMEOUT(c.currentFile()==b&&c.softProofReady(),10000);c.setViewport(211,150,1,0,.5,.5);QVERIFY(!c.softProofReady());QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);QCOMPARE(c.softProofDiagnostics()["parameter_revision"].toString(),QString::number(c.renderRevision()));
    }
    void monitorChangesOnlyPresentationAndOldProviderUrlsExpire(){
        QTemporaryDir dir;const auto path=fixture(dir);ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);c.setSoftProofEnabled(true);QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);const auto old=c.softProofUrl();const auto image=provided(provider,old);const auto revision=c.renderRevision();const auto state=c.gpuPlan(true).state.toJson();const auto bins=c.redHistogram();auto lut=MonitorColorTransform::identityLut(33);for(int y=0;y<lut.height();++y)for(int x=0;x<lut.width();++x){auto *v=reinterpret_cast<float *>(lut.scanLine(y))+4*x;v[0]=1-v[0];v[1]=1-v[1];v[2]=1-v[2];}const auto expected=MonitorColorTransform::applyLut(image,lut,33);c.setDisplayColorLut(lut,"owned-proof-monitor");QVERIFY(c.softProofUrl()!=old);QVERIFY(provided(provider,old).isNull());QCOMPARE(pixels(provided(provider,c.softProofUrl())),pixels(expected));QCOMPARE(c.renderRevision(),revision);QCOMPARE(c.gpuPlan(true).state.toJson(),state);QCOMPARE(c.redHistogram(),bins);
    }
    void invalidProfilesAndIntentsAreAtomicAndNeverEnterPhotoState(){
        QTemporaryDir dir;const auto path=fixture(dir);ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);c.setSoftProofEnabled(true);QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);const auto target=c.softProofTarget(),url=c.softProofUrl();const auto revision=c.renderRevision();const auto state=c.gpuPlan(true).state.toJson();QFile bad(dir.filePath("bad.icc"));QVERIFY(bad.open(QIODevice::WriteOnly));bad.write("owned malformed ICC");bad.close();QVERIFY(!c.loadSoftProofProfile(QUrl::fromLocalFile(bad.fileName())));QVERIFY(!c.loadSoftProofProfile(QUrl("https://example.invalid/profile.icc")));QVERIFY(!c.setSoftProofTarget("unknown"));QVERIFY(!c.setSoftProofIntent(4));QCOMPARE(c.softProofTarget(),target);QCOMPARE(c.softProofUrl(),url);QCOMPARE(c.renderRevision(),revision);QCOMPARE(c.gpuPlan(true).state.toJson(),state);
    }
    void proofPausesDuringBrushAndResumesAfterGestureEnds(){
        QTemporaryDir dir;const auto path=fixture(dir);ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);c.setSoftProofEnabled(true);QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);QVERIFY(c.beginBrushStroke("",.5,.5,.1,.5,.8,.5));QVERIFY(!c.softProofReady());c.cancelBrushStroke();QTRY_VERIFY_WITH_TIMEOUT(c.softProofReady(),10000);
    }
    void compactBilingualPanelKeepsControlsAndPlainProfileTextAccessible(){
        QTemporaryDir dir;const auto path=fixture(dir);ProcessedImageProvider provider;PhotoController c(&provider);defaults(c);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.rendering(),10000);
        for(const auto language:{"zh_CN","en_US"})for(int width:{216,280,360}){QQmlEngine engine;QQmlComponent component(&engine,QUrl::fromLocalFile(QFINDTESTDATA("../ui/SoftProofPanel.qml")));QVERIFY2(component.isReady(),qPrintable(component.errorString()));std::unique_ptr<QObject> object(component.createWithInitialProperties({{"controller",QVariant::fromValue<QObject *>(&c)},{"language",language},{"expanded",true}}));QVERIFY2(object,qPrintable(component.errorString()));auto *item=qobject_cast<QQuickItem *>(object.get());QVERIFY(item);QQuickWindow window;window.resize(width,800);item->setParentItem(window.contentItem());item->setWidth(width);window.show();QTest::qWait(80);for(const auto name:{"softProofEnable","softProofTarget","softProofChooseProfile","softProofIntent","softProofPaper","softProofGamut","softProofBlackPoint","softProofRefresh"}){auto *control=object->findChild<QQuickItem *>(name);QVERIFY2(control,name);QVERIFY2(control->isVisible(),name);QVERIFY2(control->width()<=width+1,name);}const auto label=object->findChild<QQuickItem *>("softProofProfileName");QVERIFY(label);QCOMPARE(label->property("textFormat").toInt(),int(Qt::PlainText));item->setParentItem(nullptr);}
    }
};
int main(int argc,char **argv){QQuickStyle::setStyle("Fusion");QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightOwnedTests");QCoreApplication::setApplicationName("SoftProofWorkflow");QTemporaryDir settings;QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,settings.path());QStandardPaths::setTestModeEnabled(true);SoftProofWorkflowTests tests;return QTest::qExec(&tests,argc,argv);}
#include "SoftProofWorkflowTests.moc"
