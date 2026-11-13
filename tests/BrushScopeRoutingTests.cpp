#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QScopeGuard>
#include <QSemaphore>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <QtEndian>
#include <QRgba64>
#include <cstring>
#include <stdexcept>
#include <thread>
#include "app/PhotoController.h"
#include "core/cache/AnalysisFrameCache.h"
#include "core/cache/FullScopesCache.h"
#include "core/cache/ScopePlotCache.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "core/pipeline/StageGraph.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/ActionTrace.h"

namespace {
QImage sourceImage(bool floating=false) {
    QImage image(2203,1207,floating?QImage::Format_RGBA32FPx4:QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x) {
        if(floating) {
            auto *p=reinterpret_cast<float *>(image.scanLine(y))+4*x;
            p[0]=float(x%47)/23.f-.17f;p[1]=float(y%43)/29.f;p[2]=float((x+y)%37)/31.f-.08f;p[3]=(x+y)%7?.61f:0.f;
        }else reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(4000+x*17,6000+y*23,8000+(x+y)*9,65535);
    }
    return image;
}
AdjustmentState painted() {
    AdjustmentState state;state.exposure=-.4;state.saturation=11;
    state.look.mode="manual";state.look.code="FL";state.look.parameters={{"clarity",3},{"sharpness",4}};
    BrushMaskGeometry::Stroke stroke;stroke.style={.025,.43,.71};
    for(int i=0;i<13;++i)stroke.dabs.push_back({{.21+i*.041,.32+i*.019},.6+(i%3)*.1});
    state.brushMasks.layers={{"scope",true,.8,{stroke}}};return state;
}
QImage reference(const QImage &source,const ProcessingPlan &plan,const GeometryState &geometry) {
    const auto input=geometry.apply(source);QImage result(input.size(),QImage::Format_RGBA64);
    result.setColorSpace(ColorManagement::colorSpace(plan.output));
    for(int y=0;y<input.height();y+=128) {
        const auto strip=ImagePipeline::processRegion(input,plan,{0,y,input.width(),std::min(128,input.height()-y)});
        if(strip.isNull())return {};
        for(int row=0;row<strip.height();++row)std::memcpy(result.scanLine(y+row),strip.constScanLine(row),std::size_t(input.width())*8);
    }
    return result;
}
bool sameHistogram(const ScopesResult &a,const ScopesResult &b) {
    return a.pixelCount==b.pixelCount&&a.red==b.red&&a.green==b.green&&a.blue==b.blue&&a.luma==b.luma
        &&a.shadowClipPercent==b.shadowClipPercent&&a.highlightClipPercent==b.highlightClipPercent;
}
QByteArray fileHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);
    return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
qint64 counter(const QString &name){return PerformanceRecorder::snapshot()["counters"].toObject()[name].toInteger();}
QByteArray storedZipEntry(const QString &path,const QByteArray &entry) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};const auto bytes=file.readAll();qsizetype offset=0;
    while(offset+30<=bytes.size()&&qFromLittleEndian<quint32>(bytes.constData()+offset)==0x04034b50) {
        const auto size=qFromLittleEndian<quint32>(bytes.constData()+offset+18);
        const auto nameSize=qFromLittleEndian<quint16>(bytes.constData()+offset+26),extra=qFromLittleEndian<quint16>(bytes.constData()+offset+28);
        const auto start=offset+30+nameSize+extra;if(start>bytes.size()||size>bytes.size()-start)return {};
        if(bytes.mid(offset+30,nameSize)==entry)return bytes.mid(start,size);offset=start+size;
    }
    return {};
}
}
class BrushScopeRoutingTests final:public QObject {
    Q_OBJECT
private slots:
    void previewCompletionSignalsMayCloseOrDestroyOwner_data() {
        QTest::addColumn<QString>("context");QTest::addColumn<bool>("closeOnly");
        for(const auto &context:QStringList{"prepare-geometry","prepare-activity","render-preview","render-activity","render-plot","exact-request"})
            for(bool closeOnly:{false,true})QTest::newRow(qPrintable(context+(closeOnly?"-close":"-destroy")))<<context<<closeOnly;
    }
    void previewCompletionSignalsMayCloseOrDestroyOwner() {
        QFETCH(QString,context);QFETCH(bool,closeOnly);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("preview-owner.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);
        QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&owner->scopesPixelCount()>0&&!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy(),15000);
        QVERIFY(owner->flushEdits());owner->m_saveTimer.stop();owner->m_saveMaxTimer.stop();owner->m_refineTimer.stop();owner->m_exactTimer.stop();owner->m_plotTimer.stop();owner->setExactScopes(false);
        const auto generation=owner->m_prepareGeneration,preview=owner->m_previewRevision;
        QPointer<PhotoController> alive(owner.get());bool terminal=false,closed=false;QList<quint64> stopped;
        const auto workState=[&]{return QList<quint64>{owner->m_requestedRevision,owner->m_prepare->sequence(),owner->m_render->sequence(),owner->m_scopeJob->sequence(),owner->m_inputScopeJob->sequence(),owner->m_fullScopeJob->sequence(),owner->m_plotJob->sequence(),quint64(owner->m_exactTimer.isActive())+2*quint64(owner->m_plotTimer.isActive())};};
        const auto completed=[&]{
            if(terminal||!owner)return;
            const bool ready=context.startsWith("prepare-")?!owner->m_preparing&&owner->m_prepareGeneration>generation
                :context=="exact-request"?owner->m_scopesUpdating:owner->m_previewRevision>preview&&(context=="render-preview"||!owner->rendering());
            if(!ready)return;
            terminal=true;
            if(closeOnly){closed=owner->prepareToClose();if(owner)stopped=workState();}
            else owner.reset();
        };
        QMetaObject::Connection connection;
        if(context=="prepare-geometry")connection=QObject::connect(owner.get(),&PhotoController::previewGeometryChanged,owner.get(),completed);
        else if(context=="prepare-activity"||context=="render-activity")connection=QObject::connect(owner.get(),&PhotoController::activityChanged,owner.get(),completed);
        else if(context=="render-preview")connection=QObject::connect(owner.get(),&PhotoController::previewUrlChanged,owner.get(),completed);
        else if(context=="render-plot")connection=QObject::connect(owner.get(),&PhotoController::scopePlotChanged,owner.get(),completed);
        else connection=QObject::connect(owner.get(),&PhotoController::scopesChanged,owner.get(),completed);
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});
        if(context.startsWith("prepare-")){owner->m_viewport=QSize(177,123);owner->prepareCurrent();}
        else if(context=="exact-request"){owner->m_exactScopes=true;owner->requestFullScopes();}
        else owner->scheduleRender(false);
        QTRY_VERIFY_WITH_TIMEOUT(terminal,15000);
        if(closeOnly){
            QVERIFY(closed);QVERIFY(owner&&alive);QVERIFY(owner->m_closing);
            QTRY_VERIFY_WITH_TIMEOUT(!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy()&&!owner->m_inputScopeJob->busy()&&!owner->m_fullScopeJob->busy()&&!owner->m_plotJob->busy(),5000);
            QCOMPARE(workState(),stopped);
        }else{QVERIFY(!owner);QVERIFY(!alive);}
        QCOMPARE(fileHash(path),original);
    }
    void asynchronousLinearScopeDeliveryMayDestroyOwner_data() {
        QTest::addColumn<QString>("domain");QTest::addColumn<bool>("plot");
        for(const auto &domain:QStringList{"source","working"}) {
            QTest::newRow(qPrintable(domain+"-histogram"))<<domain<<false;
            QTest::newRow(qPrintable(domain+"-plot"))<<domain<<true;
        }
    }
    void asynchronousLinearScopeDeliveryMayDestroyOwner() {
        QFETCH(QString,domain);QFETCH(bool,plot);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("asynchronous-owner.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);
        QVERIFY(owner->importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&owner->scopesPixelCount()>0&&!owner->m_scopeJob->busy(),15000);
        QVERIFY(owner->flushEdits());owner->m_saveTimer.stop();owner->m_saveMaxTimer.stop();owner->m_refineTimer.stop();owner->setExactScopes(false);
        if(plot){owner->setScopeDomain(domain);QTRY_VERIFY_WITH_TIMEOUT(owner->scopesPixelCount()>0&&!owner->m_inputScopeJob->busy(),15000);}
        QPointer<PhotoController> alive(owner.get());bool deleted=false;
        const auto completed=[&]{
            if(!owner)return;
            const bool current=plot?owner->scopePlotCurrent():owner->scopesPixelCount()>0&&owner->scopeDomain()==domain;
            if(current){owner.reset();deleted=true;}
        };
        if(plot)QObject::connect(owner.get(),&PhotoController::scopePlotChanged,owner.get(),completed);
        else QObject::connect(owner.get(),&PhotoController::scopesChanged,owner.get(),completed);
        if(plot)owner->setScopeMode("waveform");else owner->setScopeDomain(domain);
        QTRY_VERIFY_WITH_TIMEOUT(deleted,15000);QVERIFY(!alive);QVERIFY(!owner);QCOMPARE(fileHash(path),original);
    }
    void controllerFullLinearPlotsReuseCompletedFramesWhileBrushBudgetIsBusy() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("linear-frame-reuse.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        auto budget=std::make_shared<BrushRenderBudget>(512LL*1024*1024);ProcessedImageProvider provider;PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto saved=c.m_photos[c.m_currentIndex].state;const auto restore=qScopeGuard([&]{c.m_photos[c.m_currentIndex].state=saved;c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_exactTimer.stop();c.m_plotTimer.stop();});c.m_photos[c.m_currentIndex].state=painted();++c.m_requestedRevision;c.m_scopesRank=-1;c.scheduleRender(false);c.setExactScopes(true);c.setScopeDomain("working");
        QTRY_VERIFY_WITH_TIMEOUT(c.m_scopesRank==1&&c.scopesPixelCount()>0&&!c.rendering()&&!c.m_fullScopeJob->busy()&&!c.m_scopeJob->busy()&&!c.m_inputScopeJob->busy(),15000);
        const auto key=StageGraph::linearAnalysisFrameKey(c.m_fullSource,c.gpuPlan(true),c.previewGeometry(),AnalysisDomain::Working);const auto completed=c.m_analysisFrameCache->findLinear(key,AnalysisDomain::Working);QVERIFY(completed);QCOMPARE(completed.image().format(),QImage::Format_RGBA32FPx4);QCOMPARE(c.m_analysisFrameCache->snapshot()["cached_domain"].toString(),QString("working"));
        const auto scopes=c.m_scopes;const auto intent=c.gpuPlan(true).state.toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision;const auto preview=StageGraph::outputFingerprint(c.m_processedPreview);QVERIFY(sameHistogram(scopes,ScopesEngine::analyzeLinear(completed.image())));
        std::vector<std::unique_ptr<BrushAnalysisRequest>> held;QString error;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
        for(int i=0;i<64;++i){auto request=BrushAnalysisRequest::createPrepared(c.m_previewSource,c.gpuPlan(),BrushRenderRequest::Layout::Stripes128,{},&error,budget,&reason);if(!request)break;held.push_back(std::move(request));}
        QVERIFY(!held.empty());QCOMPARE(reason,BrushAdmissionFailure::BudgetUnavailable);const auto charge=budget->snapshot().charged;const auto hits=c.m_analysisFrameCache->snapshot()["hits"].toInteger();
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);PerformanceRecorder::sample("linear-frame-controller-boundary",0,{{"marker",marker}});
        for(const auto &mode:QStringList{"waveform","parade","vectorscope"}){c.setScopeMode(mode);QTRY_VERIFY_WITH_TIMEOUT(c.scopePlotCurrent(),15000);QCOMPARE(c.scopePlotPixels(),c.scopesPixelCount());ScopePlotCounts expected(mode);QVERIFY(expected.addLinear(completed.image()));QCOMPARE(c.m_scopePlot.image,expected.image());QVERIFY(!c.m_deferredBudgetJobs[2].pending);}
        bool seen=false;int renders=0,geometry=0;for(const auto &value:PerformanceRecorder::snapshot()["timings"].toArray()){const auto row=value.toObject();if(row["stage"]=="linear-frame-controller-boundary"&&row["marker"]==marker){seen=true;renders=geometry=0;}else if(seen){renders+=row["stage"]=="cpu_pipeline";geometry+=row["stage"]=="geometry_straighten"||row["stage"]=="geometry_corrections";}}
        QVERIFY(seen);QCOMPARE(renders,0);QCOMPARE(geometry,0);QVERIFY(c.m_analysisFrameCache->snapshot()["hits"].toInteger()>hits);QCOMPARE(budget->snapshot().charged,charge);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.gpuPlan(true).state.toJson(),intent);QCOMPARE(c.editHistory(),history);QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),preview);QVERIFY(sameHistogram(c.m_scopes,scopes));QCOMPARE(fileHash(path),original);held.clear();
    }
    void domainSelectionKeepsPixelsHistoryAndRevisionsAndRefusesStaleDomains() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("domains.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&c.scopesPixelCount()>0,15000);
        c.setExposure(1);c.finishInteraction();QTRY_VERIFY_WITH_TIMEOUT(!c.rendering()&&c.m_scopesRevision==c.m_requestedRevision&&c.scopesPixelCount()>0,15000);
        QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto original=fileHash(path);const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision,photo=c.m_photoEpoch,prepare=c.m_prepareGeneration;const auto frame=StageGraph::outputFingerprint(c.m_processedPreview);
        QSignalSpy changed(&c,&PhotoController::scopeDomainChanged);QCOMPARE(c.scopeDomain(),QString("display"));c.setScopeDomain("Source");QCOMPARE(changed.count(),0);
        std::thread foreign([&]{c.setScopeDomain("source");});foreign.join();QCOMPARE(changed.count(),0);QCOMPARE(c.scopeDomain(),QString("display"));
        for(const auto domain:{AnalysisDomain::Working,AnalysisDomain::Source,AnalysisDomain::Display}) {
            c.setScopeDomain(analysisDomainKey(domain));QCOMPARE(c.scopesPixelCount(),qulonglong(0));QVERIFY(!c.scopePlotCurrent());
            QTRY_VERIFY_WITH_TIMEOUT(c.m_scopesDomain==domain&&c.scopesPixelCount()>0&&!c.m_inputScopeJob->busy(),15000);
            const auto expected=domain==AnalysisDomain::Display?ScopesEngine::analyze(c.m_processedPreview):ScopesEngine::analyzeDomain(c.m_previewSource,c.gpuPlan(),domain);
            QVERIFY(sameHistogram(c.m_scopes,expected));const auto current=c.m_scopes;
            c.acceptScopes(revision,expected,1,"stale",domain==AnalysisDomain::Display?AnalysisDomain::Working:AnalysisDomain::Display);QVERIFY(sameHistogram(c.m_scopes,current));QCOMPARE(c.m_scopesRank,0);
            if(domain!=AnalysisDomain::Display) {
                std::array<quint32,HistogramCounts::GpuWords> words{};for(int i=0;i<4;++i)words[i*1024]=1;words[4098]=1;
                const QByteArray packet(reinterpret_cast<const char *>(words.data()),sizeof(words));const bool enabled=c.m_gpuEnabled;c.m_gpuEnabled=true;c.gpuScopes(revision,packet,1);c.m_gpuEnabled=enabled;QVERIFY(sameHistogram(c.m_scopes,current));
            }
            for(const auto &mode:QStringList{"waveform","vectorscope"}) {c.setScopeMode(mode);QTRY_VERIFY_WITH_TIMEOUT(c.scopePlotCurrent(),15000);QCOMPARE(c.m_plotDomain,domain);QCOMPARE(c.scopePlotPixels(),c.scopesPixelCount());}
            c.setScopeMode("histogram");QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.m_photoEpoch,photo);QCOMPARE(c.m_prepareGeneration,prepare);QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),frame);QCOMPARE(fileHash(path),original);
        }
        QCOMPARE(changed.count(),3);c.m_closing=true;c.setScopeDomain("source");QCOMPARE(changed.count(),3);QCOMPARE(c.scopeDomain(),QString("display"));c.m_closing=false;
    }
    void domainChangesCancelBlockedExactWorkWithoutPublishingOldCounts() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("blocked-domains.png");QVERIFY(sourceImage().scaled(321,197).save(path));
        QSemaphore entered,release;ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto revision=c.m_requestedRevision;const auto history=c.editHistory();auto cache=std::make_shared<FullScopesCache>();std::atomic_int calls{0};
        const auto cleanup=qScopeGuard([&]{release.release();c.m_exactTimer.stop();c.m_plotTimer.stop();});
        c.m_fullScopeJob=std::make_unique<LatestJob<ScopeRequest,ScopesResult>>([&](const ScopeRequest &r,const CancelToken &){if(calls.fetch_add(1)==0){entered.release();release.acquire();}return cache->analyze(r,{});},[&](const ScopeRequest &r,ScopesResult result){c.acceptScopes(r.revision,result,1,"Full-resolution statistics",r.domain);});
        c.m_exactScopes=true;c.setScopeDomain("working");QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,5000);entered.acquire();QVERIFY(c.m_fullScopeJob->busy());
        c.setScopeDomain("source");QCOMPARE(c.scopesPixelCount(),qulonglong(0));ScopesResult stale;stale.pixelCount=1;c.acceptScopes(revision,stale,1,"old working",AnalysisDomain::Working);QCOMPARE(c.scopesPixelCount(),qulonglong(0));
        bool responsive=false;QTimer::singleShot(0,&c,[&]{responsive=true;});QTRY_VERIFY_WITH_TIMEOUT(responsive,1000);release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!c.m_fullScopeJob->busy()&&c.scopesPixelCount()>0,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Source);QCOMPARE(c.m_scopesRank,1);QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.editHistory(),history);
        const auto corrected=c.previewGeometry().apply(c.m_fullSource);QVERIFY(sameHistogram(c.m_scopes,ScopesEngine::analyzeDomain(corrected,c.gpuPlan(true),AnalysisDomain::Source)));
        c.setExactScopes(false);QTRY_VERIFY_WITH_TIMEOUT(c.m_scopesRank==0&&c.scopesPixelCount()>0,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Source);
        c.m_scopePlot={QImage(3,3,QImage::Format_ARGB32),1,{}};c.m_plotRevision=revision;c.m_plotFull=false;c.m_plotDomain=AnalysisDomain::Working;QVERIFY(!c.scopePlotCurrent());
    }
    void domainSignalCanDestroyItsOwnerBeforeAnyNewWorkStarts() {
        for(const auto &command:QList<QJsonObject>{{{"command","scopes.domain"},{"domain","source"}},{{"command","scopes.mode"},{"mode","waveform"}},{{"command","scopes.full-resolution"},{"enabled",true}}}) {
            ProcessedImageProvider provider;QPointer<PhotoController> owner=new PhotoController(&provider);const auto name=command["command"].toString();
            if(name=="scopes.domain")QObject::connect(owner,&PhotoController::scopeDomainChanged,owner,[&]{delete owner.data();});
            else if(name=="scopes.mode")QObject::connect(owner,&PhotoController::scopePlotChanged,owner,[&]{delete owner.data();});
            else QObject::connect(owner,&PhotoController::scopesChanged,owner,[&]{delete owner.data();});
            QVERIFY(owner->executeCommand(command.toVariantMap()));QVERIFY(owner.isNull());const auto row=ActionTrace::instance().snapshot().last().toObject();QCOMPARE(row["action"].toString(),QString("scopes_command"));QCOMPARE(row["command"].toObject(),command);QVERIFY(row["presentation_only"].toBool());
        }
    }
    void scopePresentationCommandsAndLegacyAdaptersShareStrictAcceptedTraces() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("scope-commands.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&c.scopesPixelCount()>0,15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto state=c.currentState().toJson();const auto history=c.editHistory();const auto revision=c.m_requestedRevision;const auto pixels=StageGraph::outputFingerprint(c.m_processedPreview);const auto expected=c.m_scopes;
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("scopes_test_boundary",{{"marker",marker}});
        const auto captured=[&]{QJsonArray result;bool seen=false;for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="scopes_test_boundary"&&row["marker"]==marker){seen=true;result={};}else if(seen&&row["action"]=="scopes_command")result.append(row["command"]);}return result;};
        const QJsonArray commands{QJsonObject{{"command","scopes.domain"},{"domain","source"}},QJsonObject{{"command","scopes.domain"},{"domain","working"}},QJsonObject{{"command","scopes.domain"},{"domain","display"}},
            QJsonObject{{"command","scopes.mode"},{"mode","waveform"}},QJsonObject{{"command","scopes.mode"},{"mode","parade"}},QJsonObject{{"command","scopes.mode"},{"mode","vectorscope"}},QJsonObject{{"command","scopes.mode"},{"mode","histogram"}},
            QJsonObject{{"command","scopes.full-resolution"},{"enabled",false}},QJsonObject{{"command","scopes.full-resolution"},{"enabled",true}},QJsonObject{{"command","scopes.full-resolution"},{"enabled",false}}};
        for(const auto &value:commands)QVERIFY(c.executeCommand(value.toObject().toVariantMap()));
        // The final resolution toggle cancels both earlier producers. Even
        // before Display's restored preview arrives, a replacement is required.
        QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0&&!c.m_inputScopeJob->busy(),15000);QVERIFY(sameHistogram(c.m_scopes,expected));QCOMPARE(captured(),commands);
        const auto reject=[&](QVariantMap command){QVERIFY(!c.executeCommand(command));QCOMPARE(c.scopeDomain(),QString("display"));QCOMPARE(c.scopeMode(),QString("histogram"));QVERIFY(!c.exactScopes());QCOMPARE(captured(),commands);};
        reject({{"command","scopes.domain"},{"domain","Source"}});reject({{"command","scopes.mode"},{"mode",true}});reject({{"command","scopes.full-resolution"},{"enabled",1}});reject({{"command","scopes.mode"},{"mode","waveform"},{"extra",true}});reject({{"command","scopes.unknown"}});
        bool foreign=true;std::thread worker([&]{foreign=c.executeCommand({{"command","scopes.domain"},{"domain","source"}});});worker.join();QVERIFY(!foreign);QCOMPARE(captured(),commands);
        c.m_closing=true;QVERIFY(!c.executeCommand({{"command","scopes.domain"},{"domain","source"}}));c.m_closing=false;
        c.setScopeDomain("display");c.setScopeMode("histogram");c.setExactScopes(false);auto adapters=commands;adapters.append(QJsonObject{{"command","scopes.domain"},{"domain","display"}});adapters.append(QJsonObject{{"command","scopes.mode"},{"mode","histogram"}});adapters.append(QJsonObject{{"command","scopes.full-resolution"},{"enabled",false}});QCOMPARE(captured(),adapters);
        QCOMPARE(c.m_requestedRevision,revision);QCOMPARE(c.currentState().toJson(),state);QCOMPARE(c.editHistory(),history);QCOMPARE(StageGraph::outputFingerprint(c.m_processedPreview),pixels);QCOMPARE(fileHash(path),original);
    }
    void linearPreviewBudgetRefusalRetriesAndSwitchingClearsDeferredWork() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto path=dir.filePath("budget-domains.png");QVERIFY(sourceImage().scaled(321,197).save(path));const auto original=fileHash(path);
        auto budget=std::make_shared<BrushRenderBudget>(512LL*1024*1024);ProcessedImageProvider provider;PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering(),15000);QVERIFY(c.flushEdits());c.m_saveTimer.stop();c.m_saveMaxTimer.stop();c.m_refineTimer.stop();
        const auto saved=c.m_photos[c.m_currentIndex].state;const auto restore=qScopeGuard([&]{c.m_photos[c.m_currentIndex].state=saved;c.m_saveTimer.stop();c.m_saveMaxTimer.stop();});c.m_photos[c.m_currentIndex].state=painted();++c.m_requestedRevision;c.m_scopesRank=-1;
        std::vector<std::unique_ptr<BrushAnalysisRequest>> held;QString error;BrushAdmissionFailure reason=BrushAdmissionFailure::None;
        const auto fill=[&]{for(int i=0;i<64;++i){auto request=BrushAnalysisRequest::createPrepared(c.m_previewSource,c.gpuPlan(),BrushRenderRequest::Layout::Stripes128,{},&error,budget,&reason);if(!request)break;held.push_back(std::move(request));}};
        fill();QVERIFY(!held.empty());QCOMPARE(reason,BrushAdmissionFailure::BudgetUnavailable);c.setScopeDomain("working");QTRY_VERIFY_WITH_TIMEOUT(c.m_deferredBudgetJobs[3].pending,5000);QCOMPARE(c.scopesPixelCount(),qulonglong(0));QVERIFY(c.scopesStatus().contains("memory")||c.scopesStatus().contains("内存"));
        held.clear();c.retryBudgetJobs(true);QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0&&!c.m_deferredBudgetJobs[3].pending,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Working);QCOMPARE(budget->snapshot().charged,qint64(0));
        c.setScopeDomain("source");QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0,15000);fill();c.setScopeDomain("working");
        // The completed result cache may satisfy the same Working intent. Use
        // a different exposure so this second request must be admitted again.
        c.m_photos[c.m_currentIndex].state.exposure=.7;++c.m_requestedRevision;c.m_inputScopeSubmittedRevision=0;c.requestPreviewScopes();QTRY_VERIFY_WITH_TIMEOUT(c.m_deferredBudgetJobs[3].pending,5000);
        c.setScopeDomain("source");QVERIFY(!c.m_deferredBudgetJobs[3].pending);QTRY_VERIFY_WITH_TIMEOUT(c.scopesPixelCount()>0,15000);QCOMPARE(c.m_scopesDomain,AnalysisDomain::Source);QCOMPARE(fileHash(path),original);held.clear();
    }
    void controllerStartsPendingExactAfterFinalFrame_data() {
        QTest::addColumn<QString>("context");
        for (const auto *name : {"final-current", "stale-frame", "interactive-frame",
                                "preparing", "preview-statistics", "no-pending"})
            QTest::newRow(name) << QString::fromLatin1(name);
    }
    void controllerStartsPendingExactAfterFinalFrame() {
        QFETCH(QString, context);
        QStandardPaths::setTestModeEnabled(true); QTemporaryDir dir; QVERIFY(dir.isValid());
        const auto path=dir.filePath("final-preview-scopes.png");
        const auto image=sourceImage().scaled(321,197); QVERIFY(image.save(path)); const auto hash=fileHash(path);
        QSemaphore entered,release;
        ProcessedImageProvider provider; PhotoController controller(&provider); controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.brushReady() && controller.m_sourceIsFull
            && !controller.m_prepare->busy() && !controller.m_render->busy(),15000);
        QVERIFY(controller.flushEdits());
        controller.m_saveTimer.stop(); controller.m_saveMaxTimer.stop(); controller.m_refineTimer.stop();
        controller.m_exactTimer.stop(); controller.setScopeMode("histogram");
        const auto saved=controller.m_photos[controller.m_currentIndex].state;
        const auto restore=qScopeGuard([&] {
            release.release(); controller.m_fullScopeJob->cancel();
            controller.m_photos[controller.m_currentIndex].state=saved;
            controller.m_preparing=false; controller.m_interacting=false;
            controller.m_saveTimer.stop(); controller.m_saveMaxTimer.stop();
            controller.m_exactTimer.stop(); controller.m_plotTimer.stop();
        });
        controller.m_photos[controller.m_currentIndex].state=painted();
        ++controller.m_requestedRevision; controller.m_scopesRank=-1;
        const auto intent=controller.gpuPlan(true).state.toJson(); const auto history=QJsonArray::fromVariantList(controller.editHistory());
        const auto histograms=std::make_shared<FullScopesCache>(4*1024*1024,controller.m_analysisFrameCache,controller.m_brushBudget);
        controller.m_fullScopeJob=std::make_unique<LatestJob<ScopeRequest,ScopesResult>>(
            [&,histograms](const ScopeRequest &request,const CancelToken &cancel) {
                entered.release();
                while (!release.tryAcquire(1,20)) if (cancelled(cancel)) return ScopesResult{};
                return histograms->analyze(request,cancel);
            },[&](const ScopeRequest &request,ScopesResult result) {
                controller.acceptScopes(request.revision,result,1,"Full-resolution statistics");
            });
        controller.m_preparing=context=="preparing";
        controller.m_interacting=context=="interactive-frame";
        controller.m_exactScopes=context!="preview-statistics";
        if (context!="no-pending") controller.m_exactTimer.start();
        controller.m_rendering=true;
        const auto sequence=controller.m_fullScopeJob->sequence();
        controller.completeFrame(controller.m_requestedRevision-(context=="stale-frame" ? 1 : 0));
        const bool starts=context=="final-current";
        QCOMPARE(controller.m_fullScopeJob->sequence(),sequence+(starts ? 1 : 0));
        QCOMPARE(controller.m_exactTimer.isActive(),!starts && context!="no-pending");
        if (starts) {
            // Repeated presentation of this revision cannot resubmit the work.
            controller.completeFrame(controller.m_requestedRevision);
            QCOMPARE(controller.m_fullScopeJob->sequence(),sequence+1);
        }
        controller.m_exactTimer.stop();
        bool responsive=false; QTimer::singleShot(0,&controller,[&] {responsive=true;});
        QTRY_VERIFY_WITH_TIMEOUT(responsive,1000);
        if (starts) {
            QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,5000); entered.acquire();
            QVERIFY(controller.m_fullScopeJob->busy());
            release.release();
            QTRY_VERIFY_WITH_TIMEOUT(!controller.m_fullScopeJob->busy(),15000);
            const auto expected=reference(controller.m_fullSource,controller.gpuPlan(true),controller.previewGeometry());
            QVERIFY(!expected.isNull()); QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            QCOMPARE(controller.scopesPixelCount(),qulonglong(321*197));
        } else {
            QCOMPARE(entered.available(),0); QVERIFY(!controller.m_fullScopeJob->busy());
        }
        QCOMPARE(controller.gpuPlan(true).state.toJson(),intent);
        QCOMPARE(QJsonArray::fromVariantList(controller.editHistory()),history); QCOMPARE(fileHash(path),hash);
    }

    void controllerDefersExactPlotsWithoutBlockingFallback_data() {
        QTest::addColumn<int>("completion");
        QTest::newRow("completed-frame-reuse")<<0;
        QTest::newRow("uncached-streamed-fallback")<<1;
        QTest::newRow("failed-histogram-fallback")<<2;
        QTest::newRow("cancelled-exact-preview")<<3;
    }
    void controllerDefersExactPlotsWithoutBlockingFallback() {
        QFETCH(int,completion);
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto path=dir.filePath("scheduled-scopes.png");
        const auto image=sourceImage().scaled(321,197);QVERIFY(image.save(path));const auto hash=fileHash(path);
        QSemaphore entered,release;
        ProcessedImageProvider provider;PhotoController controller(&provider);controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(controller.brushReady()&&controller.m_sourceIsFull,15000);
        QVERIFY(controller.flushEdits());controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_refineTimer.stop();
        const auto saved=controller.m_photos[controller.m_currentIndex].state;
        const auto restore=qScopeGuard([&]{
            release.release();controller.m_photos[controller.m_currentIndex].state=saved;
            controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_exactTimer.stop();controller.m_plotTimer.stop();
        });
        controller.m_photos[controller.m_currentIndex].state=painted();++controller.m_requestedRevision;controller.m_scopesRank=-1;
        const auto intent=controller.gpuPlan(true).state.toJson();const auto history=controller.editHistory();
        const auto frames=completion==0?controller.m_analysisFrameCache:std::shared_ptr<AnalysisFrameCache>{};
        const auto histograms=std::make_shared<FullScopesCache>(4*1024*1024,frames,controller.m_brushBudget);
        controller.m_fullScopeJob=std::make_unique<LatestJob<ScopeRequest,ScopesResult>>(
            [&,histograms](const ScopeRequest &request,const CancelToken &cancel) {
                entered.release();
                while(!release.tryAcquire(1,20))if(cancelled(cancel))return ScopesResult{};
                if(completion==2){ScopesResult failed;failed.error="Controlled histogram failure";return failed;}
                return histograms->analyze(request,cancel);
            },[&](const ScopeRequest &request,ScopesResult result){controller.acceptScopes(request.revision,result,1,"Full-resolution statistics");});
        const auto deferred=counter("scope_plot_deferred_for_full_histogram"),encoded=counter("scope_plot_encoded_passes");
        controller.m_exactScopes=true;controller.m_exactTimer.start();controller.setScopeMode("waveform");
        const auto pendingSequence=controller.m_plotJob->sequence();controller.requestScopePlot();
        QCOMPARE(controller.m_plotJob->sequence(),pendingSequence);QVERIFY(!controller.m_plotJob->busy());QVERIFY(controller.m_plotTimer.isActive());
        QCOMPARE(counter("scope_plot_deferred_for_full_histogram"),deferred+1);
        controller.m_exactTimer.stop();controller.m_exactScopes=false;controller.setExactScopes(true);
        QTRY_VERIFY_WITH_TIMEOUT(entered.available()>0,5000);entered.acquire();
        const auto activeSequence=controller.m_plotJob->sequence();controller.requestScopePlot();
        QCOMPARE(controller.m_plotJob->sequence(),activeSequence);QVERIFY(!controller.m_plotJob->busy());QVERIFY(!controller.scopePlotCurrent());
        QVERIFY(counter("scope_plot_deferred_for_full_histogram")>=deferred+2);
        // A GUI turn runs while the producer is held; this is submission
        // ordering, never a blocking wait inside the controller or plot worker.
        bool responsive=false;QTimer::singleShot(0,&controller,[&]{responsive=true;});QTRY_VERIFY_WITH_TIMEOUT(responsive,1000);
        if(completion==3)controller.setExactScopes(false);
        release.release();
        QTRY_VERIFY_WITH_TIMEOUT(!controller.m_fullScopeJob->busy()&&controller.scopePlotCurrent(),15000);
        const auto source=completion==3?controller.m_previewSource:controller.m_fullSource;
        const auto expected=reference(source,controller.gpuPlan(completion!=3),completion==3?GeometryState{}:controller.previewGeometry());
        QVERIFY(!expected.isNull());const auto plot=renderEncodedScopePlot(expected,"waveform");
        QCOMPARE(provider.requestImage("scopes/current",nullptr,{}),plot.image);QCOMPARE(controller.scopePlotPixels(),plot.pixels);
        QCOMPARE(controller.m_plotFull,completion!=3);
        if(completion==0) {
            QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            QCOMPARE(counter("scope_plot_encoded_passes"),encoded+1);
            QVERIFY(controller.m_analysisFrameCache->snapshot()["hits"].toInteger()>=1);
        }else if(completion==2)QVERIFY(controller.scopesStatus().contains("Controlled histogram failure"));
        for(const auto &mode:{"parade","vectorscope"}) {
            controller.setScopeMode(mode);QTRY_VERIFY_WITH_TIMEOUT(controller.scopePlotCurrent(),10000);
            QCOMPARE(provider.requestImage("scopes/current",nullptr,{}),renderEncodedScopePlot(expected,mode).image);
        }
        QCOMPARE(controller.gpuPlan(true).state.toJson(),intent);QCOMPARE(controller.editHistory(),history);QCOMPARE(fileHash(path),hash);
        qInfo().noquote()<<"BRUSH_SCOPE_SCHEDULING_EVIDENCE "<<QJsonDocument(QJsonObject{
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case",QString::fromLatin1(QTest::currentDataTag())},
            {"deferred_while_scheduled",true},{"deferred_while_running",true},{"gui_responsive",responsive},
            {"exact_plot_modes",3},{"completed_frame_reused",completion==0},{"preview_after_cancel",completion==3},
            {"source_pixels",qint64(plot.pixels)},{"source_unchanged",true},{"intent_and_history_unchanged",true}
        }).toJson(QJsonDocument::Compact);
    }
    void originalGeometryAndCompletedFrameReuse() {
        const auto source=sourceImage(true);const auto before=StageGraph::outputFingerprint(source);
        auto state=painted();state.geometry.crop={.1,.08,.53,.61};state.geometry.straighten=.7;state.geometry.quarterTurns=1;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,.3f);
        auto geometry=state.geometry;geometry.crop={0,0,1,1}; // The actual uncropped crop-editor view.
        const auto expected=reference(source,plan,geometry);QVERIFY(!expected.isNull());
        const auto counts=ScopesEngine::analyze(expected);QVERIFY(counts.pixelCount>2*1024*1024);
        auto budget=std::make_shared<BrushRenderBudget>();auto frames=std::make_shared<AnalysisFrameCache>(64*1024*1024);
        FullScopesCache histograms(4*1024*1024,frames,budget);ScopeRequest request{source,plan,7,true,geometry};
        const auto actual=histograms.analyze(request);QVERIFY2(actual.pixelCount,qPrintable(actual.error));QVERIFY(sameHistogram(actual,counts));
        QCOMPARE(budget->snapshot().charged,0);QCOMPARE(frames->snapshot()["entries"].toInt(),1);
        const auto key=StageGraph::analysisFrameKey(source,plan,geometry);auto lease=frames->find(key);QVERIFY(lease);QCOMPARE(lease.image(),expected);
        auto denied=std::make_shared<BrushRenderBudget>(1);ScopePlotCache plots(16*1024*1024,frames,denied);
        const auto beforeEncoded=counter("scope_plot_encoded_passes");
        for(const auto &mode:{"waveform","parade","vectorscope"}) {
            const auto plot=plots.render({source,plan,geometry,mode,7,true});const auto oracle=renderEncodedScopePlot(expected,mode);
            QVERIFY2(!plot.image.isNull(),qPrintable(plot.error));QCOMPARE(plot.image,oracle.image);QCOMPARE(plot.pixels,counts.pixelCount);
        }
        QCOMPARE(counter("scope_plot_encoded_passes"),beforeEncoded+1);QCOMPARE(plots.snapshot()["hits"].toInteger(),2);
        QCOMPARE(denied->snapshot().charged,0);
        auto changed=request;changed.plan.state.geometry.flipHorizontal=true;
        const auto invalid=histograms.analyze(changed);QVERIFY(!invalid.error.isEmpty());QVERIFY(!invalid.retryableBudget);QCOMPARE(invalid.pixelCount,0);
        const auto badPlot=plots.render({source,changed.plan,geometry,"waveform",7,true});QVERIFY(!badPlot.error.isEmpty());QCOMPARE(badPlot.pixels,0);
        auto different=geometry;different.flipHorizontal=true;
        const auto miss=plots.render({source,plan,different,"waveform",7,true});QVERIFY(miss.retryableBudget);QCOMPARE(miss.pixels,0);
        QCOMPARE(StageGraph::outputFingerprint(source),before);
        ScopePlotCache streamed(16*1024*1024,{},budget);ScopePlotCache single(0,{},budget);
        const auto oracle=renderEncodedScopePlot(expected,"parade");
        QCOMPARE(streamed.render({source,plan,geometry,"parade",7,true}).image,oracle.image);
        QCOMPARE(single.render({source,plan,geometry,"parade",7,true}).image,oracle.image);QCOMPARE(budget->snapshot().charged,0);
    }
    void admissionFailuresAndCollectorCancellation() {
        const auto source=sourceImage();const auto state=painted();const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        QVERIFY(!ScopesEngine::analyzeFull(source,plan).pixelCount); // Keep the old research boundary.
        auto denied=std::make_shared<BrushRenderBudget>(1);auto frames=std::make_shared<AnalysisFrameCache>();
        FullScopesCache cache(4*1024*1024,frames,denied);ScopeRequest request{source,plan,9,true,{}};
        const auto rejected=cache.analyze(request);QVERIFY(rejected.retryableBudget);QVERIFY(rejected.error.contains("memory budget"));
        QCOMPARE(cache.snapshot()["entries"].toInt(),0);QCOMPARE(frames->snapshot()["entries"].toInt(),0);
        ScopePlotCache plots(16*1024*1024,frames,denied);QVERIFY(plots.render({source,plan,{},"waveform",9,true}).retryableBudget);
        BrushAdmissionFailure failure=BrushAdmissionFailure::None;QString error;auto budget=std::make_shared<BrushRenderBudget>();
        auto malformed=plan;malformed.data[ProcessingPlan::Tonal].x+=.1f;
        QVERIFY(!BrushRenderRequest::createOriginal(source,malformed,{},BrushRenderRequest::Layout::Stripes128,{},&error,budget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::Invalid);QCOMPARE(budget->snapshot().charged,0);
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(!BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,token,&error,budget,&failure));
        QCOMPARE(failure,BrushAdmissionFailure::Cancelled);token->store(false);
        auto admitted=BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,{},&error,budget,&failure);
        QVERIFY2(admitted,qPrintable(error));QCOMPARE(failure,BrushAdmissionFailure::None);
        int observers=0;const auto before=counter("full_scopes_collector_failures");
        const auto survived=ScopesEngine::analyzeAdmitted(*admitted,{},&error,nullptr,[&](const QImage &,int){++observers;throw std::runtime_error("optional collector fixture");});
        QVERIFY2(survived.pixelCount,qPrintable(error));QCOMPARE(observers,1);QCOMPARE(counter("full_scopes_collector_failures"),before+1);
        admitted.reset();QCOMPARE(budget->snapshot().charged,0);
        admitted=BrushRenderRequest::createOriginal(source,plan,{},BrushRenderRequest::Layout::Stripes128,token,&error,budget);
        QVERIFY2(admitted,qPrintable(error));auto writer=frames->begin("cancelled-admitted",source.size());QVERIFY(writer);
        std::array<ScopePlotResult,3> unpublished;unpublished[0].error="sentinel";int completed=0;
        const auto cancelledResult=ScopesEngine::analyzeAdmitted(*admitted,token,&error,&unpublished,[&](const QImage &strip,int y){
            if(!writer.append(strip,y,token))throw std::runtime_error("append failed");if(++completed==2)token->store(true);
        });
        QCOMPARE(completed,2);QCOMPARE(cancelledResult.pixelCount,0);QCOMPARE(unpublished[0].error,QString("sentinel"));
        QVERIFY(!writer.publish(token));writer={};QCOMPARE(frames->snapshot()["entries"].toInt(),0);
        admitted.reset();QCOMPARE(budget->snapshot().charged,0);token->store(false);
        FullScopesCache healthy(4*1024*1024,{},budget);QVERIFY(sameHistogram(healthy.analyze(request),survived));QCOMPARE(budget->snapshot().charged,0);
    }
    void nativeSonyFullStatisticsReuse() {
        const auto fixtures=qEnvironmentVariable("JIXELLIGHT_SONY_FIXTURES");
        if(fixtures.isEmpty()) {if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_SONY_REAL"))QFAIL("Required public Sony RAW unavailable");QSKIP("No public Sony dataset configured");}
        const auto path=QDir(fixtures).filePath("sony_a7_iv_07.arw");const QByteArray hash="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d";
        QCOMPARE(fileHash(path),hash);const auto source=loadSourceFresh(path,{});QVERIFY2(!source.image.isNull(),qPrintable(source.error));
        QCOMPARE(source.image.size(),QSize(7028,4688));QCOMPARE(source.image.format(),QImage::Format_RGBA64);
        const auto before=StageGraph::outputFingerprint(source.image);auto state=painted();state.look={};state.geometry.crop={.2,.1,.4,.6};
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,0);
        const GeometryState view;const auto expected=reference(source.image,plan,view);QVERIFY(!expected.isNull());
        const auto counts=ScopesEngine::analyze(expected);QCOMPARE(counts.pixelCount,32947264);
        auto budget=std::make_shared<BrushRenderBudget>();auto frames=std::make_shared<AnalysisFrameCache>();
        FullScopesCache histograms(4*1024*1024,frames,budget);const auto actual=histograms.analyze({source.image,plan,11,true,view});
        QVERIFY2(actual.pixelCount,qPrintable(actual.error));QVERIFY(sameHistogram(actual,counts));QCOMPARE(budget->snapshot().charged,0);
        auto denied=std::make_shared<BrushRenderBudget>(1);ScopePlotCache plots(16*1024*1024,frames,denied);
        const auto beforeEncoded=counter("scope_plot_encoded_passes");
        const auto beforeFrameHits=frames->snapshot()["hits"].toInteger();
        int requestedModes=0;
        for(const auto &mode:{"waveform","parade","vectorscope"}) {
            const auto actualPlot=plots.render({source.image,plan,view,mode,11,true});const auto expectedPlot=renderEncodedScopePlot(expected,mode);
            QVERIFY2(!actualPlot.image.isNull(),qPrintable(actualPlot.error));QCOMPARE(actualPlot.pixels,32947264);QCOMPARE(actualPlot.image,expectedPlot.image);
            ++requestedModes;
            QCOMPARE(plots.snapshot()["misses"].toInteger(),qint64(requestedModes));
            QCOMPARE(plots.snapshot()["hits"].toInteger(),qint64(requestedModes-1));
            QCOMPARE(plots.snapshot()["entries"].toInteger(),qint64(1));
            const auto hit=plots.render({source.image,plan,view,mode,12,true});
            QCOMPARE(hit.image.cacheKey(),actualPlot.image.cacheKey());QCOMPARE(hit.pixels,actualPlot.pixels);
            QCOMPARE(plots.snapshot()["hits"].toInteger(),qint64(requestedModes));
            QCOMPARE(plots.snapshot()["misses"].toInteger(),qint64(requestedModes));
        }
        // Large completed frames count each requested mode on demand. A second
        // visit hits that exact ink; no color-render admission or batched
        // speculative three-mode pass is needed.
        QCOMPARE(counter("scope_plot_encoded_passes"),beforeEncoded);
        QCOMPARE(plots.snapshot()["hits"].toInteger(),qint64(3));QCOMPARE(plots.snapshot()["misses"].toInteger(),qint64(3));
        QCOMPARE(frames->snapshot()["hits"].toInteger(),beforeFrameHits+3);
        QVERIFY(plots.snapshot()["charged_bytes"].toInteger()<=16*1024*1024);QCOMPARE(denied->snapshot().charged,0);
        QCOMPARE(StageGraph::outputFingerprint(source.image),before);QCOMPARE(fileHash(path),hash);
        qInfo().noquote()<<"BRUSH_SCOPE_ROUTING_EVIDENCE "<<QJsonDocument(QJsonObject{
            {"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"case","native-sony-full-statistics"},{"raw_sha256",QString::fromLatin1(hash)},
            {"source_pixels",32947264},{"statistics_pixels",qint64(actual.pixelCount)},{"exact_histogram_bins",4096},{"exact_plot_modes",3},
            {"uncropped_view_geometry",true},{"completed_frame_plot_reuse",true},{"requested_full_frame_modes",3},{"same_mode_cache_hits",3},{"speculative_batch_passes",0},{"operation_peak_reserved_bytes",budget->snapshot().peak},
            {"operation_end_charged_bytes",budget->snapshot().charged},{"source_unchanged",true},{"native_float_raw_decoder",false},{"native_gpu_brush",false}}).toJson(QJsonDocument::Compact);
    }
    void controllerFailuresRetryAndDiagnostic() {
        QStandardPaths::setTestModeEnabled(true);QTemporaryDir dir;QVERIFY(dir.isValid());const auto original=dir.filePath("source.png");
        const auto source=sourceImage();QVERIFY(source.save(original));const auto hash=fileHash(original);const auto state=painted();QString error;
        BrushRenderRequest::Spec spec;spec.layout=BrushRenderRequest::Layout::Stripes128;
        auto measured=BrushRenderRequest::create(source,state,spec,{},&error);QVERIFY2(measured,qPrintable(error));const auto needed=measured->reservedBytes();measured.reset();
        auto budget=std::make_shared<BrushRenderBudget>(needed+source.sizeInBytes());
        auto blocker=BrushRenderRequest::create(source,state,spec,{},&error,budget);QVERIFY2(blocker,qPrintable(error));
        ProcessedImageProvider provider;
        {
            PhotoController controller(&provider,nullptr,budget);controller.setGpuEnabled(false);
            QVERIFY(controller.importFile(QUrl::fromLocalFile(original)));
            QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading()&&!controller.rendering()&&controller.m_sourceIsFull,15000);
            QVERIFY(controller.flushEdits());controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();controller.m_refineTimer.stop();
            const auto saved=controller.m_photos[controller.m_currentIndex].state;
            const auto restore=qScopeGuard([&]{controller.m_photos[controller.m_currentIndex].state=saved;controller.m_saveTimer.stop();controller.m_saveMaxTimer.stop();});
            controller.m_photos[controller.m_currentIndex].state=state;++controller.m_requestedRevision;controller.m_scopesRank=-1;controller.m_scopesUpdating=true;
            controller.scheduleRender(false);controller.setExactScopes(true);controller.setScopeMode("waveform");
            QTRY_VERIFY_WITH_TIMEOUT(controller.m_deferredBudgetJobs[0].pending&&controller.m_deferredBudgetJobs[1].pending&&!controller.rendering(),10000);
            QVERIFY(!controller.m_scopesUpdating);QVERIFY(controller.scopesStatus().contains("内存")||controller.scopesStatus().contains("memory"));QVERIFY(!controller.scopePlotCurrent());
            QTRY_VERIFY_WITH_TIMEOUT(controller.m_deferredBudgetJobs[2].pending,10000);
            blocker.reset();
            // An actual completed independent export wakes deferred work immediately.
            QImage small(31,23,QImage::Format_RGBA64);small.fill(QColor(70,90,110));
            QSignalSpy finished(controller.m_exportQueue.get(),&ExportQueue::finished);
            QVERIFY(controller.m_exportQueue->start({{dir.filePath("small.png"),dir.filePath("small-output.png"),small,{},ColorManagement::OutputSpace::SRgb,100,true}}));
            QTRY_COMPARE_WITH_TIMEOUT(finished.count(),1,10000);QCOMPARE(finished[0][0].toInt(),1);
            QTRY_VERIFY_WITH_TIMEOUT(!controller.rendering()&&controller.m_scopesRank==1&&controller.scopePlotCurrent(),30000);
            for(const auto &pending:controller.m_deferredBudgetJobs)QVERIFY(!pending.pending);
            QCOMPARE(controller.scopesPixelCount(),qulonglong(source.width())*source.height());QVERIFY(controller.m_scopesError.isEmpty());
            const auto expected=reference(controller.m_fullSource,controller.gpuPlan(true),controller.previewGeometry());
            QVERIFY(sameHistogram(controller.m_scopes,ScopesEngine::analyze(expected)));
            const auto report=controller.reportBug();QVERIFY2(!report.isEmpty(),qPrintable(controller.statusMessage()));
            const auto cleanup=qScopeGuard([&]{QFile::remove(report);});
            const auto trace=QJsonDocument::fromJson(storedZipEntry(report,"stage_outputs.json")).object();QVERIFY(!trace.isEmpty());
            QVERIFY(trace["cpu_srgb_output"].toObject()["available"].toBool());
            const auto stages=trace["color_stages"].toObject();QVERIFY(!stages["available"].toBool());QVERIFY2(stages["final_frame_admitted"].toBool(),qPrintable(stages["error"].toString()));
            QCOMPARE(fileHash(original),hash);
        }
        provider.setImage({});provider.setScopePlot({});QCOMPARE(budget->snapshot().charged,0);
    }
};
QTEST_MAIN(BrushScopeRoutingTests)
#include "BrushScopeRoutingTests.moc"
