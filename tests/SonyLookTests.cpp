#include <QtTest>
#include <QApplication>
#include <QTemporaryDir>
#include <QColorSpace>
#include <QJsonDocument>
#include <QSaveFile>
#include <QImageReader>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QRgba64>
#include <random>
#include "core/look/SonyLookMetadata.h"
#include "core/look/LookProfiles.h"
#include "core/look/LookCalibration.h"
#include "core/look/CameraReference.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/export/JpegExporter.h"
#include "core/metadata/MetadataReader.h"
#include "core/raw/RawDecoder.h"
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"

namespace {
QImage fixture(int w=127,int h=143) {
    QImage image(w,h,QImage::Format_RGBA64);std::mt19937 rng(47281);
    for(int y=0;y<h;++y){auto *p=reinterpret_cast<QRgba64*>(image.scanLine(y));for(int x=0;x<w;++x){
        const quint16 r=5000+rng()%50000,g=5000+rng()%50000,b=5000+rng()%50000;p[x]=QRgba64::fromRgba64(r,g,b,65535);}}
    image.setColorSpace(QColorSpace::SRgb);return image;
}
void styleTag(Exiv2::ExifData &e,const char *style,const char *group="Sony1") {
    auto value=Exiv2::Value::create(Exiv2::asciiString);value->read(style);e.add(Exiv2::ExifKey(0xb020,group),value.get());
}
Exiv2::ExifData sony(const char *style="FL") {
    Exiv2::ExifData e;e["Exif.Image.Make"]="SONY";e["Exif.Image.Model"]="ILCE-7M4";styleTag(e,style);return e;
}
void signedTag(Exiv2::ExifData &e,int tag,int value,const char *group="Sony1") {
    const Exiv2::ExifKey key(uint16_t(tag),group);auto v=Exiv2::Value::create(Exiv2::signedLong);v->read(std::to_string(value));e.add(key,v.get());
}
AdjustmentState look(const char *code="FL") {AdjustmentState s;s.look.mode="manual";s.look.code=code;return s;}
int maxDiff(const QImage &a,const QImage &b){if(a.size()!=b.size())return 65535;int m=0;
    for(int y=0;y<a.height();++y){auto *p=reinterpret_cast<const QRgba64*>(a.constScanLine(y));auto *q=reinterpret_cast<const QRgba64*>(b.constScanLine(y));
        for(int x=0;x<a.width();++x)m=std::max({m,std::abs(int(p[x].red())-int(q[x].red())),std::abs(int(p[x].green())-int(q[x].green())),std::abs(int(p[x].blue())-int(q[x].blue()))});}return m;}
}
class SonyLookTests: public QObject {
    Q_OBJECT
private slots:
    void makerNotesNameAndEightValues() {
        auto e=sony();const int tags[]{0x2004,0x2033,0x2032,0x2034,0x2005,0x2006,0x2035,0x2036};
        const int values[]{3,-2,-3,1,-1,3,3,1};const auto names=SonyLookMetadata::parameterNames();
        for(int i=0;i<8;++i)signedTag(e,tags[i],values[i]);
        const auto m=SonyLookMetadata::read(e);QCOMPARE(m["code"].toString(),QString("FL"));QVERIFY(m["autoEligible"].toBool());
        QCOMPARE(m["generation"].toString(),QString("creative-look"));
        for(int i=0;i<8;++i){QCOMPARE(m["parameters"].toMap()[names[i]].toInt(),values[i]);QVERIFY(!m["parameterSources"].toMap()[names[i]].toString().isEmpty());}
        QCOMPARE(m["customSlotStatus"].toString(),QString("not-recorded"));
    }
    void numericIdsAlsoWorkInSony2() {
        auto e=sony("Standard");e.erase(e.findKey(Exiv2::ExifKey(0xb020,"Sony1")));
        styleTag(e,"VV2","Sony2");signedTag(e,0x2036,5,"Sony2");
        const auto m=SonyLookMetadata::read(e);QCOMPARE(m["code"].toString(),QString("VV2"));QCOMPARE(m["parameters"].toMap()["clarity"].toInt(),5);
    }
    void missingInvalidConflictingAndLegacy() {
        auto e=sony();auto m=SonyLookMetadata::read(e);QVERIFY(m["parameters"].toMap().isEmpty());
        signedTag(e,0x2035,0);signedTag(e,0x2032,32767);m=SonyLookMetadata::read(e);
        QVERIFY(m["parameters"].toMap().isEmpty());QCOMPARE(m["invalidParameters"].toMap().size(),2);
        styleTag(e,"VV","Sony2");m=SonyLookMetadata::read(e);QCOMPARE(m["status"].toString(),QString("conflict"));QVERIFY(!m["autoEligible"].toBool());
        e=sony("Standard");e["Exif.Image.Model"]="ILCE-7M2";m=SonyLookMetadata::read(e);QVERIFY(!m["autoEligible"].toBool());QCOMPARE(m["generation"].toString(),QString("unknown"));
        e=sony("FUTURE_LOOK");m=SonyLookMetadata::read(e);QCOMPARE(m["status"].toString(),QString("unsupported"));QVERIFY(!m["autoEligible"].toBool());
        signedTag(e,0xb029,18);m=SonyLookMetadata::read(e);QCOMPARE(m["status"].toString(),QString("unsupported"));QVERIFY(!m["autoEligible"].toBool());
        QVERIFY(m["code"].toString().isEmpty());QVERIFY(!m["rawFields"].toMap().isEmpty());
        e["Exif.Image.Make"]="CANON";QCOMPARE(SonyLookMetadata::read(e)["status"].toString(),QString("not-sony"));
    }
    void metadataFromActualJpegContainer() {
        QTemporaryDir dir;const auto path=dir.filePath("sony.jpg");QVERIFY(fixture().save(path,"JPEG"));
        auto image=Exiv2::ImageFactory::open(path.toStdString());image->readMetadata();auto e=sony();signedTag(e,0x2034,2);image->setExifData(e);image->writeMetadata();
        QString error;const auto m=MetadataReader::read(path,&error)["sonyLook"].toMap();QVERIFY2(error.isEmpty(),qPrintable(error));
        QCOMPARE(m["code"].toString(),QString("FL"));QCOMPARE(m["parameters"].toMap()["fade"].toInt(),2);
        // This is a synthetic EXIF fixture, not evidence of real Sony camera output.
    }
    void asShotIsPerPhotoAndNeverAutomaticOnJpeg() {
        AdjustmentState s;s.look.mode="as-shot";s.look.strength=.5;
        const QVariantMap first{{"sonyLook",SonyLookMetadata::read(sony("FL"))}},second{{"sonyLook",SonyLookMetadata::read(sony("VV2"))}};
        auto a=LookProfiles::resolveAsShot(s,first,true),b=LookProfiles::resolveAsShot(a,second,true);
        QCOMPARE(a.look.code,QString("FL"));QCOMPARE(b.look.code,QString("VV2"));QCOMPARE(b.look.strength,.5);
        QVERIFY(!LookProfiles::active(LookProfiles::resolveAsShot(a,second,false).look));
        QVERIFY(!LookProfiles::active(LookProfiles::resolveAsShot(a,{},true).look));
        QCOMPARE(AdjustmentState::fromJson(QJsonObject{{"exposure",.3}}).look.mode,QString("off"));
    }
    void stateAndLutIntegrity() {
        auto s=look();s.look.parameters={{"fade",2},{"clarity",4}};s.look.lut=LookLut::identity();s.look.mode="calibrated";
        auto copy=AdjustmentState::fromJson(s.toJson());QCOMPARE(copy.toJson(),s.toJson());QVERIFY(copy.look.lut);
        auto json=s.look.lut->toJson();json["size"]=18;QString error;QVERIFY(!LookLut::fromJson(json,&error));QVERIFY(!error.isEmpty());
        auto bad=s.toJson();auto l=bad["look"].toObject();l["lut"]=json;bad["look"]=l;
        auto invalid=AdjustmentState::fromJson(bad);QVERIFY(!LookProfiles::active(invalid.look));QVERIFY(!invalid.look.error.isEmpty());
        for(int n:{2,17,33}){auto lut=LookLut::identity(n);QVERIFY(lut->validate());for(auto p: {std::array<float,3>{0,0,0},{1,1,1},{.21f,.56f,.92f}}){auto out=lut->sample(p[0],p[1],p[2]);for(int c=0;c<3;++c)QVERIFY(std::abs(out[c]-p[c])<1e-6);}}
    }
    void cubeParsingBounds() {
        QByteArray cube="TITLE \"Test\"\nLUT_3D_SIZE 2\nDOMAIN_MIN 0 0 0\nDOMAIN_MAX 1 1 1\n";
        for(int b=0;b<2;++b)for(int g=0;g<2;++g)for(int r=0;r<2;++r)cube+=QByteArray::number(r)+' '+QByteArray::number(g)+' '+QByteArray::number(b)+'\n';
        QString error;QVERIFY(LookLut::fromCube(cube,&error));QVERIFY(!LookLut::fromCube(cube+"0 0 0\n",&error));
        QVERIFY(!LookLut::fromCube("LUT_3D_SIZE 65\n",&error));QVERIFY(!LookLut::fromCube(cube+"DOMAIN_MIN -1 0 0\n",&error));
        QVERIFY(!LookLut::fromCube("LUT_3D_SIZE 2\nnan 0 1\n",&error));
    }
    void disabledIdentityAndAllPresets() {
        auto image=fixture();auto baseline=ImagePipeline::process(image,{});
        for(const auto &v:LookProfiles::catalog()){auto s=look();s.look.code=v.toMap()["code"].toString();
            const auto out=ImagePipeline::process(image,s);QVERIFY(!out.isNull());
            if(s.look.code!="ST")QVERIFY(maxDiff(out,baseline)>20);
            s.look.strength=0;QCOMPARE(ImagePipeline::process(image,s),baseline);
            s.look.strength=1;s.look.mode="off";QCOMPARE(ImagePipeline::process(image,s),baseline);
        }
        auto s=look("ST");s.look.lut=LookLut::identity();s.look.mode="calibrated";
        QVERIFY(maxDiff(ImagePipeline::process(image,s),baseline)<=1);
        auto mono=ImagePipeline::process(image,look("BW"));
        for(int y=0;y<mono.height();++y){auto *p=reinterpret_cast<const QRgba64*>(mono.constScanLine(y));for(int x=0;x<mono.width();++x){QCOMPARE(p[x].red(),p[x].green());QCOMPARE(p[x].green(),p[x].blue());}}
    }
    void detailTilesAndStatisticsHaveNoSeams() {
        auto image=fixture(147,291);auto s=look("FL");s.look.parameters={{"sharpness",9},{"sharpnessRange",2},{"clarity",9},{"fade",4}};
        auto plan=ProcessingPlan::compile(s,ImagePipeline::InputEncoding::LinearProPhoto);
        const auto full=ImagePipeline::processWithPlan(image,plan);QVERIFY(!full.isNull());
        for(int y=0;y<image.height();y+=128){QRect roi(0,y,image.width(),std::min(128,image.height()-y));QCOMPARE(ImagePipeline::processRegion(image,plan,roi),full.copy(roi));}
        QCOMPARE(ImagePipeline::processRegion(image,plan,QRect(25,36,81,95)),full.copy(QRect(25,36,81,95)));
        auto a=ScopesEngine::analyzeFull(image,plan),b=ScopesEngine::analyze(full);QCOMPARE(a.red,b.red);QCOMPARE(a.green,b.green);QCOMPARE(a.blue,b.blue);QCOMPARE(a.luma,b.luma);
        const auto token=std::make_shared<std::atomic_bool>(true);QVERIFY(ImagePipeline::processWithPlan(image,plan,token).isNull());
    }
    void eightFineControlsAreRendered() {
        const auto image=fixture();auto s=look("ST");const auto baseline=ImagePipeline::process(image,s);
        for(const auto &field:SonyLookMetadata::parameterNames()){
            auto f=s;f.look.parameters[field]=field=="sharpnessRange"?5:6;
            if(field=="sharpnessRange")f.look.parameters["sharpness"]=5;
            QVERIFY2(maxDiff(ImagePipeline::process(image,f),baseline)>0,qPrintable(field));
        }
    }
    void referencePairingAndIndependentScopes() {
        QVariantMap a{{"make","SONY"},{"model","ILCE-7M4"},{"captureTime","2026:09:07 10:11:12"},{"iso","100"}};QString why;
        QVERIFY(CameraReference::matches(a,a,&why));auto b=a;b["model"]="ILCE-7M2";QVERIFY(!CameraReference::matches(a,b));
        b=a;b["iso"]="200";QVERIFY(!CameraReference::matches(a,b));b=a;b["software"]="Adobe Lightroom";QVERIFY(!CameraReference::matches(a,b));
        b=a;b.remove("captureTime");QVERIFY(!CameraReference::matches(a,b));
        QTemporaryDir dir;const auto path=dir.filePath("reference.jpg");QVERIFY(fixture().save(path,"JPEG"));
        auto result=CameraReference::load({"missing.arw",path,a,1});QVERIFY2(!result.image.isNull(),qPrintable(result.error));
        QCOMPARE(result.info["kind"].toString(),QString("manual-reference"));QCOMPARE(result.image.size(),QSize(127,143));QCOMPARE(result.scopes.pixelCount,quint64(127*143));
        ProcessedImageProvider provider;provider.setReference(result.image);provider.setImage(fixture(25,35));
        QCOMPARE(provider.requestImage("current",nullptr,{}).size(),QSize(25,35));QCOMPARE(provider.requestImage("reference/1",nullptr,{}).size(),result.image.size());
    }
    void referenceFitHeldOutValidation() {
        const auto base=fixture(160,160);auto target=base.copy();
        for(int y=0;y<target.height();++y){auto *p=reinterpret_cast<QRgba64*>(target.scanLine(y));for(int x=0;x<target.width();++x)p[x]=QRgba64::fromRgba64(p[x].red()*.85+2500,p[x].green()*.94+1300,p[x].blue()*.8+3000,65535);}
        const auto fit=fitLookImages(base,target);QVERIFY2(fit.lut,qPrintable(fit.error));
        qInfo()<<"synthetic held-out RMSE"<<fit.report;QVERIFY(fit.report["heldoutRmseAfter"].toDouble()<fit.report["heldoutRmseBefore"].toDouble()*.5);
        QVERIFY(!fitLookImages(base,target.mirrored(true,false)).lut);
        QVERIFY(!fitLookImages(base,target.scaled(100,60)).lut);
        QVERIFY(!fitLookImages(base,target,std::make_shared<std::atomic_bool>(true)).lut);
        QVERIFY(!fitLookImages(base.scaled(8,8),target.scaled(8,8)).lut);
    }
    void realRawReferenceFitKeepsEditsAndCancelsStaleWork() {
        const auto path=qEnvironmentVariable("JIXELLIGHT_TEST_RAW");if(path.isEmpty())QSKIP("No real RAW fixture configured");
        QString error;auto raw=RawDecoder::decode(path,&error);QVERIFY2(!raw.isNull(),qPrintable(error));
        auto ref=ImagePipeline::process(raw.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation),{},ImagePipeline::InputEncoding::LinearProPhoto);
        for(int y=0;y<ref.height();++y){auto *p=reinterpret_cast<QRgba64*>(ref.scanLine(y));for(int x=0;x<ref.width();++x)p[x]=QRgba64::fromRgba64(p[x].red()*.88+2200,p[x].green()*.95+1000,p[x].blue()*.86+2800,65535);}
        QTemporaryDir dir;const auto jpeg=dir.filePath("reference.jpg");QVERIFY(ref.save(jpeg,"JPEG",100));
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.previewReady()&&!c.loading(),15000);c.setExposure(.3);c.finishInteraction();
        QVERIFY(c.loadReference(QUrl::fromLocalFile(jpeg)));QTRY_VERIFY_WITH_TIMEOUT(!c.referenceBusy(),5000);QVERIFY(!c.cameraReferenceUrl().isEmpty());
        // Let refinement/viewport changes settle so the frozen revision is stable.
        QTest::qWait(400);QTRY_VERIFY_WITH_TIMEOUT(!c.rendering(),5000);
        QVERIFY(c.calibrateFromReference());QTRY_VERIFY_WITH_TIMEOUT(!c.calibrationBusy(),10000);
        QVERIFY2(c.lookState()["mode"].toString()=="calibrated",qPrintable(QJsonDocument::fromVariant(c.calibrationReport()).toJson()));
        QCOMPARE(c.exposure(),.3);const auto digest=c.lookState()["lutDigest"].toString();QVERIFY(!digest.isEmpty());
        c.finishInteraction();QTest::qWait(300);QVERIFY(c.calibrateFromReference());c.setExposure(.6);
        QTest::qWait(600);QVERIFY(!c.calibrationBusy());QCOMPARE(c.exposure(),.6);QCOMPARE(c.lookState()["lutDigest"].toString(),digest);
    }
    void controllerReferenceProfileAndProtectedSource() {
        QTemporaryDir dir;const auto first=dir.filePath("first.png"),second=dir.filePath("second.png"),ref=dir.filePath("reference.jpg");
        QVERIFY(fixture().save(first));QVERIFY(fixture(147,141).save(second));QVERIFY(fixture().save(ref,"JPEG"));
        ProcessedImageProvider provider;PhotoController controller(&provider);controller.setGpuEnabled(false);
        QVERIFY(controller.importFile(QUrl::fromLocalFile(first)));QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady(),10000);
        QCOMPARE(controller.lookState()["mode"].toString(),QString("off"));controller.setLookMode("as-shot");QCOMPARE(controller.lookState()["mode"].toString(),QString("off"));
        controller.setLookCode("FL");controller.setLookParameter("fade",2);controller.setLookStrength(.7);
        const auto profile=dir.filePath("test.jlook.json");QVERIFY(controller.saveLookProfile(QUrl::fromLocalFile(profile)));
        controller.setLookCode("BW");QVERIFY(controller.loadLookProfile(QUrl::fromLocalFile(profile)));QCOMPARE(controller.lookState()["code"].toString(),QString("FL"));
        QVERIFY(controller.loadReference(QUrl::fromLocalFile(ref)));QTRY_VERIFY_WITH_TIMEOUT(!controller.referenceBusy(),10000);QVERIFY(!controller.cameraReferenceUrl().isEmpty());
        QFile f(ref);QVERIFY(f.open(QIODevice::ReadOnly));const auto before=f.readAll();f.close();
        QVERIFY(!controller.exportCurrent(QUrl::fromLocalFile(ref)));QVERIFY(f.open(QIODevice::ReadOnly));QCOMPARE(f.readAll(),before);f.close();
        QVERIFY(controller.importFile(QUrl::fromLocalFile(second)));controller.selectPhoto(1);QVERIFY(controller.cameraReferenceUrl().isEmpty());
        QTRY_VERIFY_WITH_TIMEOUT(controller.previewReady(),10000);
        const auto reference=controller.currentMetadata()["fileName"].toString();QCOMPARE(reference,QString("second.png"));
    }
};
int main(int argc,char **argv){QApplication app(argc,argv);QCoreApplication::setOrganizationName("JixelLightTests");QCoreApplication::setApplicationName("SonyLookTests");QStandardPaths::setTestModeEnabled(true);SonyLookTests tests;return QTest::qExec(&tests,argc,argv);}
#include "SonyLookTests.moc"
