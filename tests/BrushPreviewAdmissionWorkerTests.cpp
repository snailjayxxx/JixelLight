#include <QtTest>
#include <QColorSpace>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSettings>
#include <QSaveFile>
#include <QDir>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QRgba64>
#include <bit>
#include <cmath>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/pipeline/BrushRenderRequest.h"
#include "diagnostics/PerformanceRecorder.h"

namespace {
QImage fixture(int w=144,int h=96) {
    QImage image(w,h,QImage::Format_RGBA64);
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<h;++y)for(int x=0;x<w;++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(11000+(x%101)*67,13000+(y%97)*71,19000,47000);
    return image;
}
QByteArray pixels(const QImage &image) {
    return QCryptographicHash::hash(QByteArrayView(reinterpret_cast<const char *>(image.constBits()),image.sizeInBytes()),QCryptographicHash::Sha256).toHex();
}
QByteArray fileSha(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};return QCryptographicHash::hash(f.readAll(),QCryptographicHash::Sha256).toHex();}
ProcessingPlan plan(int dabs=1,double radius=.03) {
    BrushMaskGeometry::Stroke stroke{{radius,.4,.8},{{{.5,.5},.6}}};
    stroke.dabs.resize(std::size_t(dabs),stroke.dabs.front());
    AdjustmentState state;state.brushMasks.layers={{"worker-qa",true,.75,{std::move(stroke)}}};
    return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,false,0);
}
QVector<QJsonObject> rowsFor(qint64 key) {
    QVector<QJsonObject> result;
    for(const auto &v:PerformanceRecorder::snapshot()["timings"].toArray()) {
        const auto r=v.toObject();
        if(r["stage"]=="brush_live_full_preflight"&&r["source_cache_key"]==QString::number(key))result.append(r);
    }
    return result;
}
bool workerRow(const QJsonObject &r) {
    const auto owner=QString::number(reinterpret_cast<quintptr>(QThread::currentThreadId()));
    return r["worker_thread"].toBool()&&r["owner_thread_id"].toString()==owner
        &&!r["thread_id"].toString().isEmpty()&&r["thread_id"].toString()!=owner;
}
int cursor(const PhotoController &c) {const auto h=c.editHistory();for(int i=0;i<h.size();++i)if(h[i].toMap()["current"].toBool())return i;return -1;}
QString tempPattern() {return qEnvironmentVariable("JIXELLIGHT_QA_OWNED_ROOT")+"/case-XXXXXX";}
}

class BrushPreviewAdmissionWorkerTests final:public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        const auto root=qEnvironmentVariable("JIXELLIGHT_QA_OWNED_ROOT");QVERIFY(!root.isEmpty());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,root+"/settings");
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,root+"/settings");
        QStandardPaths::setTestModeEnabled(true);
    }
    void cancelableFullPreflightPreservesOriginalDecisionAndIntent() {
        const auto p=plan();const auto before=p;
        const auto bytes=[](const ProcessingPlan &v){return QByteArray(reinterpret_cast<const char *>(v.data.data()),sizeof(v.data));};
        QString oldError,newError;QVERIFY(BrushRenderRequest::preflightGesture({257,193},p,&oldError));
        QVERIFY(BrushRenderRequest::preflightGesture({257,193},p,CancelToken{},&newError));
        QCOMPARE(oldError,newError);QCOMPARE(bytes(p),bytes(before));
        auto cancelledToken=std::make_shared<std::atomic_bool>(true);newError="unchanged-before-call";
        QVERIFY(!BrushRenderRequest::preflightGesture({257,193},p,cancelledToken,&newError));
        QCOMPARE(newError,QString("Cancelled"));QCOMPARE(bytes(p),bytes(before));
        auto invalid=plan(1000,.2);QString a,b;
        QVERIFY(!BrushRenderRequest::preflightGesture({1600,1000},invalid,&a));
        QVERIFY(!BrushRenderRequest::preflightGesture({1600,1000},invalid,CancelToken{},&b));
        QCOMPARE(a,b);QVERIFY(!a.isEmpty());
    }
    void completeUniqueRawStreamPreservesSamplerAndPublishesFromWorker() {
        QTemporaryDir dir(tempPattern());QVERIFY(dir.isValid());const auto path=dir.filePath("unfiltered-owned.png");
        QVERIFY(fixture(256,160).save(path));const auto originalFile=fileSha(path);QVERIFY(!originalFile.isEmpty());
        ProcessedImageProvider provider;auto budget=std::make_shared<BrushRenderBudget>();
        PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.m_scopeJob->busy(),15000);
        const auto initial=c.gpuPlan(true).state.toJson();const auto history=cursor(c);const auto originalSource=pixels(c.m_fullSource);
        QVERIFY(c.beginBrushStrokeModePressure("",.4,.45,.015,.4,.8,.75,"paint",.6));
        QVERIFY(!c.m_brushGesture->movementStabilizer);const auto full=c.m_fullSource.size();const auto key=c.m_previewSource.cacheKey();
        BrushMaskGeometry::StrokeBuilder reference(c.m_brushGesture->frame.width(),c.m_brushGesture->frame.height(),{.015,.4,.8});
        QVERIFY(reference.append({{.4,.45},.6}));quint64 firstPublished=0;int guiTicks=0;
        connect(&c,&PhotoController::brushPreviewChanged,&c,[&]{if(!firstPublished&&c.m_brushPreviewSample)firstPublished=c.m_brushPreviewSample;});
        QTimer timer;timer.setInterval(1);connect(&timer,&QTimer::timeout,&c,[&]{++guiTicks;});timer.start();
        QElapsedTimer elapsed;elapsed.start();qint64 maximumAppendNs=0;
        for(int i=1;i<=5000;++i) {
            const double x=.4+i*1e-7,y=.45+2e-5*std::sin(i*.023);
            const double pressure=i%311==0?-0.0:.45+.35*std::sin(i*.019);
            QVERIFY(reference.append({{x,y},pressure}));QElapsedTimer sample;sample.start();
            QVERIFY(c.appendBrushStroke(x,y,pressure));maximumAppendNs=std::max(maximumAppendNs,sample.nsecsElapsed());
            if(i%40==0)QTest::qWait(1);
        }
        QCOMPARE(c.m_brushGesture->sample,quint64(5001));
        QTRY_VERIFY_WITH_TIMEOUT(c.m_brushPreviewSample==5001&&!c.m_brushPreviewJob->busy(),15000);
        timer.stop();QVERIFY(guiTicks>0);QVERIFY(firstPublished>0&&firstPublished<5001);
        QVERIFY(elapsed.elapsed()<15000);QVERIFY(maximumAppendNs<1000000000);
        auto copy=*c.m_brushGesture->builder;const auto actual=copy.finish(),expected=reference.finish();QVERIFY(actual&&expected);
        QCOMPARE(actual->dabs.size(),expected->dabs.size());
        for(std::size_t i=0;i<actual->dabs.size();++i) {
            QCOMPARE(std::bit_cast<quint64>(actual->dabs[i].center.x),std::bit_cast<quint64>(expected->dabs[i].center.x));
            QCOMPARE(std::bit_cast<quint64>(actual->dabs[i].center.y),std::bit_cast<quint64>(expected->dabs[i].center.y));
            QCOMPARE(std::bit_cast<quint64>(actual->dabs[i].pressure),std::bit_cast<quint64>(expected->dabs[i].pressure));
        }
        const auto rows=rowsFor(key);QVERIFY(!rows.isEmpty());bool accepted=false;
        for(const auto &r:rows) {QVERIFY(workerRow(r));QCOMPARE(r["full_source_width"].toInt(),full.width());QCOMPARE(r["full_source_height"].toInt(),full.height());accepted|=r["admission_passed"].toBool();}
        QVERIFY(accepted);QCOMPARE(c.gpuPlan(true).state.toJson(),initial);QCOMPARE(cursor(c),history);
        QVERIFY(!c.endBrushStroke().isEmpty());QCOMPARE(cursor(c),history+1);
        const auto committedState=c.gpuPlan(true).state;const auto &committed=committedState.brushMasks.layers.back().strokes.back();
        QCOMPARE(committed.dabs.size(),expected->dabs.size());
        for(std::size_t i=0;i<committed.dabs.size();++i)QCOMPARE(std::bit_cast<quint64>(committed.dabs[i].pressure),std::bit_cast<quint64>(expected->dabs[i].pressure));
        QCOMPARE(fileSha(path),originalFile);QCOMPARE(pixels(c.m_fullSource),originalSource);
        qInfo().noquote()<<QJsonDocument(QJsonObject{{"new_worker_raw_packets",5001},{"sampler_dabs",int(expected->dabs.size())},{"GUI_ticks",guiTicks},{"first_published_sample",QString::number(firstPublished)},{"last_sample","5001"},{"maximum_append_ms",maximumAppendNs/1.0e6},{"total_ms",double(elapsed.elapsed())},{"physical_tablet_or_full_raw_jitter_acceptance",false}}).toJson(QJsonDocument::Compact);
    }
    void fullResolutionWorkRefusalBlocksOtherwiseAdmissibleSmallPreview() {
        QTemporaryDir dir(tempPattern());QVERIFY(dir.isValid());const auto path=dir.filePath("large-owned.png");QVERIFY(fixture(1600,1000).save(path));
        ProcessedImageProvider provider;auto budget=std::make_shared<BrushRenderBudget>();PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.m_scopeJob->busy(),15000);
        QVERIFY(c.beginBrushStrokeModePressure("",.5,.5,.03,.4,.8,.75,"paint",.6));c.clearBrushPreview();QTRY_VERIFY_WITH_TIMEOUT(!c.m_brushPreviewJob->busy(),10000);
        const auto initial=c.gpuPlan(true).state.toJson();const int history=cursor(c);auto p=plan(1000,.2);
        const auto preview=c.m_previewSource.scaled(80,48,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        QString error;{auto small=BrushRenderRequest::createPrepared(preview,p,BrushRenderRequest::Layout::Whole,{},&error,budget);QVERIFY2(small,qPrintable(error));}
        QVERIFY(!BrushRenderRequest::preflightGesture(c.m_fullSource.size(),p,&error));const auto key=preview.cacheKey();
        c.m_brushPreviewJob->submit({preview,p,c.m_brushGesture->gesture,c.m_brushGesture->sample,c.m_fullSource.size(),QRectF(0,0,1,1)});
        QTRY_VERIFY_WITH_TIMEOUT(!c.m_brushPreviewJob->busy(),15000);const auto rows=rowsFor(key);QCOMPARE(rows.size(),1);
        QVERIFY(workerRow(rows[0]));QVERIFY(!rows[0]["admission_passed"].toBool());QVERIFY(!rows[0]["cancelled"].toBool());QVERIFY(!rows[0]["reason"].toString().isEmpty());
        QCOMPARE(rows[0]["full_source_width"].toInt(),1600);QCOMPARE(rows[0]["full_source_height"].toInt(),1000);
        QVERIFY(c.brushPreviewUrl().isEmpty());QCOMPARE(c.gpuPlan(true).state.toJson(),initial);QCOMPARE(cursor(c),history);c.cancelBrushStroke();
    }
    void queuedSnapshotOwnsFullSourceViewportAndPlan() {
        QTemporaryDir dir(tempPattern());QVERIFY(dir.isValid());const auto path=dir.filePath("snapshot-owned.png");QVERIFY(fixture().save(path));
        ProcessedImageProvider provider;auto budget=std::make_shared<BrushRenderBudget>();PhotoController c(&provider,nullptr,budget);c.setGpuEnabled(false);
        QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.m_scopeJob->busy(),15000);
        QVERIFY(c.beginBrushStrokeModePressure("",.5,.5,.03,.4,.8,.75,"paint",.6));c.clearBrushPreview();QTRY_VERIFY_WITH_TIMEOUT(!c.m_brushPreviewJob->busy(),10000);
        PhotoController::BrushPreviewRequest request{c.m_previewSource,plan(),c.m_brushGesture->gesture,c.m_brushGesture->sample,c.m_fullSource.size(),c.m_previewFrameRect};
        const auto expectedSource=request.source;const auto expectedPlan=request.plan;const auto frame=request.previewFrameRect;const auto full=request.fullSourceSize;const auto key=request.source.cacheKey();
        c.m_brushPreviewJob->submit(request);request.fullSourceSize={131073,1};request.previewFrameRect={-1,0,1,1};request.plan.setFrameRect({-1,0,1,1});request.source.fill(Qt::red);
        QTRY_VERIFY_WITH_TIMEOUT(c.m_brushPreviewSample==c.m_brushGesture->sample&&!c.m_brushPreviewJob->busy(),10000);
        const auto rows=rowsFor(key);QVERIFY(!rows.isEmpty());const auto row=rows.back();QVERIFY(workerRow(row));QVERIFY(row["admission_passed"].toBool());
        QCOMPARE(row["full_source_width"].toInt(),full.width());QCOMPARE(row["full_source_height"].toInt(),full.height());
        QCOMPARE(row["preview_frame_x"].toDouble(),frame.x());QCOMPARE(row["preview_frame_y"].toDouble(),frame.y());QCOMPARE(row["preview_frame_width"].toDouble(),frame.width());QCOMPARE(row["preview_frame_height"].toDouble(),frame.height());
        auto reference=expectedPlan;reference.setFrameRect(frame);const auto expected=ImagePipeline::processRegion(expectedSource,reference,QRect(QPoint{},expectedSource.size()));
        const auto actual=provider.requestImage(c.m_brushPreviewId,nullptr,{});QVERIFY(!actual.isNull()&&!expected.isNull());QCOMPARE(pixels(actual),pixels(expected));c.cancelBrushStroke();
    }
    void ownerCancellationDiscardsWorkerPublicationAndLeases() {
        QTemporaryDir dir(tempPattern());QVERIFY(dir.isValid());const auto path=dir.filePath("cancel-owned.png");QVERIFY(fixture().save(path));const auto original=fileSha(path);
        ProcessedImageProvider provider;auto budget=std::make_shared<BrushRenderBudget>();auto c=std::make_unique<PhotoController>(&provider,nullptr,budget);c->setGpuEnabled(false);
        QVERIFY(c->importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c->brushReady()&&!c->m_scopeJob->busy(),15000);
        const auto initial=c->gpuPlan(true).state.toJson();const auto history=cursor(*c);const auto key=c->m_previewSource.cacheKey();
        QVERIFY(c->beginBrushStrokeModePressure("",.4,.45,.015,.4,.8,.75,"paint",.6));
        for(int i=1;i<=200;++i)QVERIFY(c->appendBrushStroke(.4+i*1e-7,.45+1e-5*std::sin(i*.03),.45+.25*std::sin(i*.07)));
        c->cancelBrushStroke();QTRY_VERIFY_WITH_TIMEOUT(!c->m_brushPreviewJob->busy(),10000);
        QVERIFY(c->brushPreviewUrl().isEmpty());QVERIFY(!c->brushRecoveryAvailable());QCOMPARE(c->gpuPlan(true).state.toJson(),initial);QCOMPARE(cursor(*c),history);
        const auto rows=rowsFor(key);QVERIFY(!rows.isEmpty());for(const auto &row:rows)QVERIFY(workerRow(row));
        QCOMPARE(fileSha(path),original);c.reset();provider.clearBrushPreview();QCOMPARE(budget->snapshot().charged,qint64(0));
    }
    void cleanupTestCase() {
        QJsonArray rows;
        for(const auto &value:PerformanceRecorder::snapshot()["timings"].toArray()) {
            const auto row=value.toObject();if(row["stage"]!="brush_live_full_preflight")continue;
            QVERIFY(workerRow(row));rows.append(row);
        }
        QVERIFY(!rows.isEmpty());
        QJsonObject evidence{{"schema",1},{"evidence_only",true},{"qa_mode","brush_full_source_preflight_worker"},
            {"compiled_commit",QString::fromUtf8(JIXELLIGHT_GIT_COMMIT)},
            {"qpa_platform",QGuiApplication::platformName()},{"backend","CPU"},{"new_methods",5},
            {"worker_timing_rows",rows},{"physical_tablet_or_full_raw_jitter_acceptance",false}};
        const auto output=QDir::current().filePath("brush-preview-admission-worker-results.json");
        QVERIFY(!QFile::exists(output));QSaveFile file(output);QVERIFY(file.open(QIODevice::WriteOnly));
        const auto bytes=QJsonDocument(evidence).toJson(QJsonDocument::Indented);
        QCOMPARE(file.write(bytes),qint64(bytes.size()));QVERIFY(file.commit());
        qInfo().noquote()<<QJsonDocument(QJsonObject{{"worker_timing_evidence",output},{"actual_rows",rows.size()},
            {"qpa_platform",QGuiApplication::platformName()},{"backend","CPU"}}).toJson(QJsonDocument::Compact);
    }
};
QTEST_MAIN(BrushPreviewAdmissionWorkerTests)
#include "BrushPreviewAdmissionWorkerTests.moc"
