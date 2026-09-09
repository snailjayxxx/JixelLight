#include <QtTest>
#include <QImage>
#include <QRgba64>
#include <QTemporaryDir>
#include <QImageReader>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QJsonDocument>
#include <QFile>
#include <atomic>
#include <QSignalSpy>
#include <QDataStream>
#include <QStandardPaths>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/LoggingEngine.h"
#include <random>
#include "core/pipeline/ProcessingPlan.h"
#include "core/async/LatestJob.h"
#include "core/cache/SourceCache.h"
#include "core/project/ProjectDatabase.h"
#include "core/export/JpegExporter.h"
#include "core/scopes/ScopesEngine.h"
QImage legacyProcess(const QImage &,const AdjustmentState &,ImagePipeline::InputEncoding);
QImage correctedReferenceProcess(const QImage &,const AdjustmentState &,ImagePipeline::InputEncoding);
namespace {
QImage fixture(int width=512,int height=341) {
    QImage image(width,height,QImage::Format_RGBA64);
    std::mt19937 rng(78412);
    for (int y=0;y<height;++y) {
        auto *row=reinterpret_cast<QRgba64 *>(image.scanLine(y));
        for (int x=0;x<width;++x) row[x]=QRgba64::fromRgba64(rng()%50000,rng()%50000,rng()%50000,65535);
    }
    return image;
}
ProcessingPlan nonRawLinearPlan(const AdjustmentState &state={},ColorManagement::OutputSpace output=ColorManagement::OutputSpace::SRgb) {
    return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,output,false,0.0f);
}
}
class PerformanceTests:public QObject {
    Q_OBJECT
private slots:
    void parallelIsDeterministic() {
        const QImage image=fixture(800,512);
        AdjustmentState state; state.exposure=.6; state.temperature=30;state.tint=-15;state.saturation=12;state.hslHue[0]=28;state.masterCurve[2]=.56;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto serial=ImagePipeline::processWithPlan(image,plan,{},false);
        const auto parallel=ImagePipeline::processWithPlan(image,plan);
        QCOMPARE(serial,parallel);
    }
    void legacyReferenceBounds() {
        const QImage image=fixture(127,93);
        for (int mode=0;mode<3;++mode) {
            AdjustmentState state;
            if (mode==1) { state.exposure=.4;state.temperature=25;state.tint=-13;state.highlights=-20;state.contrast=7; }
            if (mode==2) { state.hue=-10;state.saturation=25;state.vibrance=10;state.hslSaturation[5]=-35;state.redCurve[2]=.52; }
            const auto old=legacyProcess(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
            const auto expected=correctedReferenceProcess(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
            const auto now=ImagePipeline::processWithPlan(image,nonRawLinearPlan(state));
            int maximum=0, originalMaximum=0, correctedPixels=0;
            for (int y=0;y<image.height();++y) {
                auto *a=reinterpret_cast<const QRgba64 *>(expected.constScanLine(y));
                auto *b=reinterpret_cast<const QRgba64 *>(now.constScanLine(y));
                auto *o=reinterpret_cast<const QRgba64 *>(old.constScanLine(y));
                for (int x=0;x<image.width();++x) {
                    maximum=std::max({maximum,std::abs(int(a[x].red())-int(b[x].red())),std::abs(int(a[x].green())-int(b[x].green())),std::abs(int(a[x].blue())-int(b[x].blue()))});
                    const int originalDelta=std::max({std::abs(int(o[x].red())-int(a[x].red())),std::abs(int(o[x].green())-int(a[x].green())),std::abs(int(o[x].blue())-int(a[x].blue()))});
                    originalMaximum=std::max(originalMaximum,originalDelta);
                    if(originalDelta>24) ++correctedPixels;
                }
            }
            qInfo()<<"independent corrected-reference max error"<<mode<<maximum
                   <<"intentional boundary correction versus alpha6 max"<<originalMaximum<<"pixels"<<correctedPixels;
            QVERIFY2(maximum<=24,qPrintable(QString::number(maximum)));
        }
    }
    void gamutBoundaryHasNoVisibleStep() {
        QImage image(257,1,QImage::Format_RGBA64);
        auto *source=reinterpret_cast<QRgba64 *>(image.bits());
        for(int x=0;x<image.width();++x) source[x]=QRgba64::fromRgba64(18743+x-128,43453,32369,65535);
        AdjustmentState state;state.hue=-23;state.saturation=18;state.vibrance=22;
        state.hslHue[2]=30;state.hslSaturation[5]=-25;state.masterCurve[2]=.57;state.redCurve[3]=.8;
        const auto actual=ImagePipeline::process(image,state,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto *pixels=reinterpret_cast<const QRgba64 *>(actual.constBits());
        int largestStep=0;
        for(int x=1;x<image.width();++x)
            largestStep=std::max({largestStep,std::abs(int(pixels[x].red())-int(pixels[x-1].red())),
                std::abs(int(pixels[x].green())-int(pixels[x-1].green())),std::abs(int(pixels[x].blue())-int(pixels[x-1].blue()))});
        qInfo()<<"gamut boundary ramp maximum adjacent 16-bit step"<<largestStep;
        QVERIFY2(largestStep<=128,qPrintable(QString::number(largestStep)));
    }
    void cancellationDoesNotPublishPartialPixels() {
        auto token=std::make_shared<std::atomic_bool>(true);
        QVERIFY(ImagePipeline::process(fixture(),{},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,token).isNull());
        QCOMPARE(ScopesEngine::analyze(fixture(),1024,token).pixelCount,quint64(0));
    }
    void latestOnlyQueue() {
        std::atomic_int calls{0};
        QVector<int> delivered;
        LatestJob<int,int> job([&](const int &value,const CancelToken &token) {
            ++calls;
            for(int i=0;i<20 && !cancelled(token);++i) QThread::msleep(1);
            return value;
        },[&](const int &,int result) { delivered.append(result); });
        for (int i=0;i<100;++i) job.submit(i);
        QTRY_COMPARE_WITH_TIMEOUT(delivered.size(),1,3000);
        QCOMPARE(delivered.first(),99);
        QVERIFY(calls.load()<=2);
    }
    void sourceCacheBudgetAndDiskRoundtrip() {
        QTemporaryDir dir;
        SourceCache cache(1024*1024,dir.path());
        SourceData source;source.image=fixture(128,80);source.key=QString(64,'a');source.fullResolution=true;source.metadata={{"model","test"}};
        cache.put(source);QCOMPARE(cache.get(source.key).image,source.image);
        cache.storeDiskPreview(source);
        const auto disk=cache.diskPreview(source.key);
        QCOMPARE(disk.image,source.image);
        QCOMPARE(disk.metadata.value("model").toString(),QString("test"));
        QVERIFY(!disk.fullResolution);
        SourceData large=source;large.key=QString(64,'b');large.image=fixture(512,512);cache.put(large);
        QVERIFY(cache.get(large.key).image.isNull());
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
        QFile corrupt(dir.filePath(source.key+".jlpv"));QVERIFY(corrupt.open(QIODevice::WriteOnly|QIODevice::Append));corrupt.write("x");corrupt.close();
        QVERIFY(cache.diskPreview(source.key).image.isNull());
        QVERIFY(cache.memoryBytes()<=cache.budgetBytes());
    }
    void fileCacheKeyInvalidation() {
        QTemporaryDir dir;
        QFile file(dir.filePath("source.raw"));QVERIFY(file.open(QIODevice::WriteOnly));file.write("abc");file.close();
        const auto before=SourceCache::fileKey(file.fileName());
        QVERIFY(file.open(QIODevice::WriteOnly));file.write("abd");file.close();
        QVERIFY(before!=SourceCache::fileKey(file.fileName()));
    }
    void databaseBatchPersistsFinalSnapshot() {
        QTemporaryDir dir;ProjectDatabase store;QVERIFY(store.create(dir.path(),"test"));
        QHash<QString,AdjustmentState> states;
        for(int i=0;i<100;++i) { AdjustmentState state;state.exposure=double(i)/100;states.insert(QString::number(i)+".dng",state); }
        QVERIFY(store.updateBatch(states));QVERIFY(store.flush());
        const QString connection="test-read";
        {
            auto db=QSqlDatabase::addDatabase("QSQLITE",connection);db.setDatabaseName(store.projectPath()+"/Project.db");QVERIFY(db.open());
            QSqlQuery q(db);QVERIFY(q.exec("SELECT count(*) FROM photos"));QVERIFY(q.next());QCOMPARE(q.value(0).toInt(),100);
            QVERIFY(q.exec("SELECT adjustment_json FROM photos WHERE path='99.dng'"));QVERIFY(q.next());
            QCOMPARE(QJsonDocument::fromJson(q.value(0).toString().toUtf8()).object().value("exposure").toDouble(),.99);
            db.close();
        }
        QSqlDatabase::removeDatabase(connection);
    }
    void fullScopesMatchesFullRender() {
        auto image=fixture(257,273);AdjustmentState state;state.saturation=20;state.exposure=.3;
        const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto full=ScopesEngine::analyzeFull(image,plan);
        const auto reference=ScopesEngine::analyze(ImagePipeline::processWithPlan(image,plan));
        QCOMPARE(full.red,reference.red);QCOMPARE(full.green,reference.green);QCOMPARE(full.blue,reference.blue);QCOMPARE(full.luma,reference.luma);
        QCOMPARE(full.pixelCount,quint64(image.width())*image.height());
    }
    void streamingExportHasProfileAndAtomicCancellation() {
        QTemporaryDir dir;auto image=fixture(128,160);QString error;
        const auto p3=ColorManagement::OutputSpace::DisplayP3;
        QVERIFY2(exportJpegTiled(image,{},dir.filePath("p3.jpg"),p3,95,{},&error),qPrintable(error));
        QImageReader reader(dir.filePath("p3.jpg"));auto output=reader.read();QVERIFY(!output.isNull());
        QCOMPARE(output.size(),image.size());QCOMPARE(output.colorSpace(),ColorManagement::colorSpace(p3));
        auto token=std::make_shared<std::atomic_bool>(false);
        const auto path=dir.filePath("cancelled.jpg");
        QVERIFY(!exportJpegTiled(image,{},path,p3,90,token,&error,[&](int){ token->store(true); }));
        QVERIFY(!QFile::exists(path));
    }

    void cacheHeaderTamperingIsRejected() {
        QTemporaryDir dir; SourceCache cache(1024*1024,dir.path());
        SourceData source;source.image=fixture(128,80);source.key=QString(64,'c');source.fullResolution=true;
        cache.storeDiskPreview(source);
        QFile file(dir.filePath(source.key+".jlpv"));QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.seek(4));QDataStream data(&file);data<<quint32(80)<<quint32(128);file.close();
        QVERIFY(cache.diskPreview(source.key).image.isNull());
    }
    void controllerLatestSelectionAndExportSnapshot() {
        QTemporaryDir dir;ProcessedImageProvider provider;PhotoController controller(&provider);
        controller.setGpuEnabled(false);
        const auto redPath=dir.filePath("red.jpg"),bluePath=dir.filePath("blue.jpg");
        QImage red(96,80,QImage::Format_RGB32);red.fill(QColor(160,20,20));QVERIFY(red.save(redPath));
        QImage blue(96,80,QImage::Format_RGB32);blue.fill(QColor(20,20,160));QVERIFY(blue.save(bluePath));
        controller.importFile(QUrl::fromLocalFile(redPath));controller.importFile(QUrl::fromLocalFile(bluePath));
        for(int i=0;i<40;++i) {controller.selectPhoto(i%2);controller.setExposure((i%7)/10.0);}
        controller.selectPhoto(1);controller.setExposure(.25);controller.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady()&&!controller.loading()&&!controller.rendering(),10000);
        QCOMPARE(controller.currentFile(),bluePath);QCOMPARE(controller.exposure(),.25);
        QSize size;auto result=provider.requestImage("current",&size,{});
        QVERIFY(!result.isNull());QVERIFY(result.pixelColor(0,0).blue()>result.pixelColor(0,0).red());
        QVERIFY(!controller.exportCurrent(QUrl::fromLocalFile(redPath)));
        QVERIFY(!controller.exportCurrent(QUrl::fromLocalFile(bluePath)));
        QSignalSpy exported(&controller,&PhotoController::exportFinished);
        const auto destination=dir.filePath("snapshot.jpg");
        QVERIFY(controller.exportCurrent(QUrl::fromLocalFile(destination),"display-p3",95));
        controller.selectPhoto(0);controller.setExposure(-4);controller.finishInteraction();
        QTRY_COMPARE_WITH_TIMEOUT(exported.size(),1,10000);
        QCOMPARE(exported.first().at(0).toInt(),1);QCOMPARE(exported.first().at(1).toInt(),0);
        QImageReader reader(destination);auto exportedImage=reader.read();QVERIFY(!exportedImage.isNull());
        QVERIFY(exportedImage.pixelColor(0,0).blue()>exportedImage.pixelColor(0,0).red());
        QCOMPARE(exportedImage.colorSpace(),ColorManagement::colorSpace(ColorManagement::OutputSpace::DisplayP3));
        QVERIFY(QImage(redPath).pixelColor(0,0).red()>100);
    }
    void loggingFlushRetainsLastRecord() {
        QStandardPaths::setTestModeEnabled(true);
        LoggingEngine::install();
        for(int i=0;i<100;++i) qInfo()<<"performance-test-log"<<i;
        qInfo()<<"final-record-668342";
        QVERIFY(LoggingEngine::flush());
        QFile file(LoggingEngine::currentLogPath());QVERIFY(file.open(QIODevice::ReadOnly));
        QVERIFY(file.readAll().contains("final-record-668342"));
        LoggingEngine::shutdown();
    }

    void wideGamutExportDoesNotPassThroughSrgb() {
        QImage image(1,1,QImage::Format_RGBA64); image.fill(QColor::fromRgbF(.94,.12,.12));
        image.setColorSpace(QColorSpace(QColorSpace::DisplayP3));
        auto linear=image.convertedToColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear),QImage::Format_RGBA64);
        const auto direct=ImagePipeline::processWithPlan(linear,nonRawLinearPlan({},ColorManagement::OutputSpace::DisplayP3));
        const auto limitedSrgb=ImagePipeline::processWithPlan(linear,nonRawLinearPlan());
        const auto limited=ColorManagement::convertFromSrgb(limitedSrgb,ColorManagement::OutputSpace::DisplayP3);
        QVERIFY(std::abs(direct.pixelColor(0,0).greenF()-limited.pixelColor(0,0).greenF())>.02);
    }
};
QTEST_MAIN(PerformanceTests)
#include "PerformanceTests.moc"
