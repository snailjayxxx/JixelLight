#include "core/preview/PreviewTasks.h"
#include "core/pipeline/BrushRenderRequest.h"
#include <QCoreApplication>
#include <QImageReader>
#include <QBuffer>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <iostream>
#include <cstring>
#include <QtEndian>
QByteArray hashFile(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QByteArray rgba16Hash(const QImage &image) {
    QCryptographicHash hash(QCryptographicHash::Sha256);
    for(int y=0;y<image.height();++y) {
        const auto *row=reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        if(QSysInfo::ByteOrder==QSysInfo::LittleEndian)hash.addData(QByteArrayView(reinterpret_cast<const char *>(row),image.width()*8));
        else {QByteArray little(image.width()*8,Qt::Uninitialized);for(int x=0;x<image.width();++x){auto *p=little.data()+8*x;qToLittleEndian(row[x].red(),p);qToLittleEndian(row[x].green(),p+2);qToLittleEndian(row[x].blue(),p+4);qToLittleEndian(row[x].alpha(),p+6);}hash.addData(little);}
    }
    return hash.result().toHex();
}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto arguments=app.arguments();if(arguments.size()!=4)return 2;const QString source=arguments[1],snapshot=arguments[2],output=arguments[3];QElapsedTimer clock;clock.start();
    QFile stateFile(snapshot);if(!stateFile.open(QIODevice::ReadOnly))return 2;const auto saved=QJsonDocument::fromJson(stateFile.readAll()).object();const auto sourceHash=hashFile(source);
    if(sourceHash!="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"||sourceHash!=saved["source_sha256"].toString().toLatin1()||QFileInfo(source).size()!=69120000)return 3;
    const auto decodeStart=clock.nsecsElapsed();const auto loaded=loadSourceFresh(source,{});if(loaded.image.size()!=QSize(7028,4688)||loaded.image.format()!=QImage::Format_RGBA64)return 4;
    const double rawDecodeMs=(clock.nsecsElapsed()-decodeStart)/1.e6;const auto referenceStart=clock.nsecsElapsed();
    const auto state=AdjustmentState::fromJson(saved["state"].toObject());const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,float(saved["base_exposure_stops"].toDouble()));
    const auto packed=QByteArray(reinterpret_cast<const char *>(plan.data.data()),sizeof(plan.data));const auto planHash=QCryptographicHash::hash(packed,QCryptographicHash::Sha256).toHex();if(planHash!=saved["plan_data_sha256"].toString().toLatin1())return 5;
    QString error;auto request=BrushRenderRequest::createOriginal(loaded.image,plan,state.geometry,BrushRenderRequest::Layout::Stripes128,{},&error);if(!request){std::cerr<<error.toStdString();return 6;}
    QImage expected(request->size(),QImage::Format_RGBA64);expected.setColorSpace(ColorManagement::colorSpace(plan.output));if(expected.isNull())return 7;
    if(!request->visitStrips([&](const BrushImageLease &strip,int y){for(int row=0;row<strip.image().height();++row)std::memcpy(expected.scanLine(y+row),strip.image().constScanLine(row),size_t(expected.width())*8);return true;},{},&error))return 8;request.reset();const double referenceMs=(clock.nsecsElapsed()-referenceStart)/1.e6;const auto pngStart=clock.nsecsElapsed();
    QFile encoded(output);if(!encoded.open(QIODevice::ReadOnly)||encoded.size()<=0||encoded.size()>qint64(32947264)*8+16*1024*1024)return 9;
    QByteArray bytes;bytes.reserve(encoded.size());while(bytes.size()<encoded.size()){const auto part=encoded.read(std::min<qint64>(1024*1024,encoded.size()-bytes.size()));if(part.isEmpty())return 10;bytes+=part;}
    QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");if(reader.size()!=QSize(7028,4688))return 11;
    std::cerr<<"VERIFY_DECODE_BEGIN bytes="<<bytes.size()<<" allocation_limit_mb="<<QImageReader::allocationLimit()<<std::endl;const auto actual=reader.read();if(actual.isNull()){std::cerr<<reader.errorString().toStdString();return 12;}
    const double pngDecodeMs=(clock.nsecsElapsed()-pngStart)/1.e6;const auto normalized=actual.convertToFormat(QImage::Format_RGBA64);
    const auto expectedPixelHash=rgba16Hash(expected),actualPixelHash=rgba16Hash(normalized);
    const auto expectedIccHash=QCryptographicHash::hash(expected.colorSpace().iccProfile(),QCryptographicHash::Sha256).toHex(),actualIccHash=QCryptographicHash::hash(actual.colorSpace().iccProfile(),QCryptographicHash::Sha256).toHex();
    const bool exact=actual.colorSpace()==expected.colorSpace()&&normalized==expected&&actualPixelHash==expectedPixelHash&&actualIccHash==expectedIccHash;const auto budget=BrushRenderBudget::sharedDefault()->snapshot();
    const QJsonObject result{{"passed",exact&&clock.elapsed()<120000&&budget.limit==3LL*1024*1024*1024&&budget.peak>0&&budget.peak<=budget.limit&&budget.charged==0},
        {"raw_decode_ms",rawDecodeMs},{"complete_reference_ms",referenceMs},{"png_read_decode_ms",pngDecodeMs},
        {"rgba16_hash_format","row-major unassociated RGBA16 little-endian; no row padding"},
        {"expected_rgba16_sha256",QString::fromLatin1(expectedPixelHash)},{"decoded_rgba16_sha256",QString::fromLatin1(actualPixelHash)},
        {"expected_icc_sha256",QString::fromLatin1(expectedIccHash)},{"decoded_icc_sha256",QString::fromLatin1(actualIccHash)},
        {"test_fixture",true},{"source_commit",JIXELLIGHT_GIT_COMMIT},{"physical_tablet_verified",false},{"product_UI_acceptance",false},{"exact_pixels_and_ICC",exact},{"source_pixels",32947264},{"decoded_pixels",actual.width()*actual.height()},
        {"plan_hash_matches_actual_UI_export",true},{"plan_data_sha256",QString::fromLatin1(planHash)},{"raw_sha256",QString::fromLatin1(sourceHash)},
        {"operation_budget_bytes",budget.limit},{"operation_peak_reserved_bytes",budget.peak},{"operation_end_charged_bytes",budget.charged},{"elapsed_ms",clock.elapsed()},{"native_gpu_brush",false},{"native_float_raw_decoder",false}};
    std::cout<<QJsonDocument(result).toJson().toStdString();return result["passed"].toBool()?0:13;
}
