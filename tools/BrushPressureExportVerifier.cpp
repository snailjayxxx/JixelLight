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
QByteArray hashFile(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
int main(int argc,char **argv){
    QCoreApplication app(argc,argv);const auto arguments=app.arguments();if(arguments.size()!=4)return 2;const QString source=arguments[1],snapshot=arguments[2],output=arguments[3];QElapsedTimer clock;clock.start();
    QFile stateFile(snapshot);if(!stateFile.open(QIODevice::ReadOnly))return 2;const auto saved=QJsonDocument::fromJson(stateFile.readAll()).object();const auto sourceHash=hashFile(source);
    if(sourceHash!="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d"||sourceHash!=saved["source_sha256"].toString().toLatin1()||QFileInfo(source).size()!=69120000)return 3;
    const auto loaded=loadSourceFresh(source,{});if(loaded.image.size()!=QSize(7028,4688)||loaded.image.format()!=QImage::Format_RGBA64)return 4;
    const auto state=AdjustmentState::fromJson(saved["state"].toObject());const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,true,float(saved["base_exposure_stops"].toDouble()));
    const auto packed=QByteArray(reinterpret_cast<const char *>(plan.data.data()),sizeof(plan.data));const auto planHash=QCryptographicHash::hash(packed,QCryptographicHash::Sha256).toHex();if(planHash!=saved["plan_data_sha256"].toString().toLatin1())return 5;
    QString error;auto request=BrushRenderRequest::createOriginal(loaded.image,plan,state.geometry,BrushRenderRequest::Layout::Stripes128,{},&error);if(!request){std::cerr<<error.toStdString();return 6;}
    QImage expected(request->size(),QImage::Format_RGBA64);expected.setColorSpace(ColorManagement::colorSpace(plan.output));if(expected.isNull())return 7;
    if(!request->visitStrips([&](const BrushImageLease &strip,int y){for(int row=0;row<strip.image().height();++row)std::memcpy(expected.scanLine(y+row),strip.image().constScanLine(row),size_t(expected.width())*8);return true;},{},&error))return 8;request.reset();
    QFile encoded(output);if(!encoded.open(QIODevice::ReadOnly)||encoded.size()<=0||encoded.size()>qint64(32947264)*8+16*1024*1024)return 9;
    QByteArray bytes;bytes.reserve(encoded.size());while(bytes.size()<encoded.size()){const auto part=encoded.read(std::min<qint64>(1024*1024,encoded.size()-bytes.size()));if(part.isEmpty())return 10;bytes+=part;}
    QBuffer buffer(&bytes);buffer.open(QIODevice::ReadOnly);QImageReader reader(&buffer,"PNG");if(reader.size()!=QSize(7028,4688))return 11;
    std::cerr<<"VERIFY_DECODE_BEGIN bytes="<<bytes.size()<<" allocation_limit_mb="<<QImageReader::allocationLimit()<<std::endl;const auto actual=reader.read();if(actual.isNull()){std::cerr<<reader.errorString().toStdString();return 12;}
    const bool exact=actual.colorSpace()==expected.colorSpace()&&actual.convertToFormat(QImage::Format_RGBA64)==expected;const auto budget=BrushRenderBudget::sharedDefault()->snapshot();
    const QJsonObject result{{"passed",exact&&clock.elapsed()<120000&&budget.limit==3LL*1024*1024*1024&&budget.peak>0&&budget.peak<=budget.limit&&budget.charged==0},
        {"test_fixture",true},{"source_commit",JIXELLIGHT_GIT_COMMIT},{"physical_tablet_verified",false},{"product_UI_acceptance",false},{"exact_pixels_and_ICC",exact},{"source_pixels",32947264},{"decoded_pixels",actual.width()*actual.height()},
        {"plan_hash_matches_actual_UI_export",true},{"plan_data_sha256",QString::fromLatin1(planHash)},{"raw_sha256",QString::fromLatin1(sourceHash)},
        {"operation_budget_bytes",budget.limit},{"operation_peak_reserved_bytes",budget.peak},{"operation_end_charged_bytes",budget.charged},{"elapsed_ms",clock.elapsed()},{"native_gpu_brush",false},{"native_float_raw_decoder",false}};
    std::cout<<QJsonDocument(result).toJson().toStdString();return result["passed"].toBool()?0:13;
}
