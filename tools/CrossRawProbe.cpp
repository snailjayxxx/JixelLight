#include "core/raw/RawDecoder.h"
#include "core/gpu/GpuEngine.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/export/PngExporter.h"
#include <QGuiApplication>
#include <QOffscreenSurface>
#include <QSurfaceFormat>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonArray>
#include <QRgba64>
#include <QtEndian>
#include <QRegularExpression>
#include <QSet>
#include <cmath>

namespace {
QString fileHash(const QString &path){QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&file)?QString::fromLatin1(hash.result().toHex()):QString();}
QString imageHash(const QImage &image){QCryptographicHash hash(QCryptographicHash::Sha256);for(int y=0;y<image.height();++y)hash.addData(QByteArrayView(reinterpret_cast<const char *>(image.constScanLine(y)),image.width()*8));return QString::fromLatin1(hash.result().toHex());}
bool write(const QString &path,const QByteArray &bytes){QFile file(path);return file.open(QIODevice::WriteOnly|QIODevice::NewOnly)&&file.write(bytes)==bytes.size()&&file.flush();}
QByteArray rgba16(const QImage &image){QByteArray out(image.width()*image.height()*8,Qt::Uninitialized);auto *target=reinterpret_cast<quint16 *>(out.data());for(int y=0;y<image.height();++y){const auto *row=reinterpret_cast<const QRgba64 *>(image.constScanLine(y));for(int x=0;x<image.width();++x){*target++=qToLittleEndian(row[x].red());*target++=qToLittleEndian(row[x].green());*target++=qToLittleEndian(row[x].blue());*target++=qToLittleEndian(row[x].alpha());}}return out;}
class Gpu {
    std::unique_ptr<QOffscreenSurface> surface;std::unique_ptr<QRhi> rhi;std::unique_ptr<GpuEngine> engine;quint64 revision=0;
public:
    bool initialize(){
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
        QRhiGles2InitParams params;params.format=QSurfaceFormat::defaultFormat();surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));params.fallbackSurface=surface.get();rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        if(!rhi||!rhi->isFeatureSupported(QRhi::Compute))return false;engine=std::make_unique<GpuEngine>(rhi.get());return true;
    }
    QString backend()const{return engine?engine->backendName():QStringLiteral("unavailable");}
    QImage render(const QImage &source,const ProcessingPlan &plan){
        QRhiCommandBuffer *cb=nullptr;if(rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
        const auto floats=ImagePipeline::floatSource(source);const bool ok=engine->process(cb,floats,plan,++revision,{},false);
        QRhiReadbackResult readback;bool done=false;
        if(ok){readback.completed=[&]{done=true;};auto *updates=rhi->nextResourceUpdateBatch();updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);cb->resourceUpdate(updates);}
        rhi->endOffscreenFrame();rhi->finish();
        if(!ok||!done||engine->lastProcessUsedCpuFallback()||readback.data.size()!=source.width()*source.height()*16)return {};
        const auto *values=reinterpret_cast<const float *>(readback.data.constData());for(qsizetype i=0;i<readback.data.size()/4;++i)if(!std::isfinite(values[i]))return {};
        const QImage frame(reinterpret_cast<const uchar *>(readback.data.constData()),source.width(),source.height(),source.width()*16,QImage::Format_RGBA32FPx4);return ImagePipeline::rgba64Source(frame);
    }
};
}
int main(int argc,char **argv){
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);QGuiApplication app(argc,argv);
    const auto args=app.arguments();if(args.size()!=4){qCritical("Usage: JixelLightCrossRawProbe manifest.json fixture-directory NEW-output-directory");return 2;}
    QFile manifest(args[1]);if(!manifest.open(QIODevice::ReadOnly)||manifest.size()>64*1024)return 2;const auto bytes=manifest.read(64*1024+1);if(bytes.size()>64*1024)return 2;
    QJsonParseError parse;const auto document=QJsonDocument::fromJson(bytes,&parse);const auto items=document.object()["items"].toArray();
    if(parse.error!=QJsonParseError::NoError||!document.isObject()||document.object()["schema"].toInt()!=1||items.size()!=3)return 2;
    const QDir fixtures(args[2]),out(args[3]);if(QFileInfo::exists(out.absolutePath())||QFileInfo(out.absolutePath()).isSymLink())return 2;
    QSet<QString> names,ids;const QRegularExpression digest("^[0-9a-f]{64}$"),identifier("^[0-9]{1,8}$");
    for(const auto &value:items){const auto row=value.toObject();const auto name=row["file"].toString(),id=row["id"].toString();const QFileInfo file(fixtures.filePath(name));
        if(name.isEmpty()||QFileInfo(name).fileName()!=name||names.contains(name)||!identifier.match(id).hasMatch()||ids.contains(id)||row["model"].toString().isEmpty()
            ||row["license"].toString()!="CC0-1.0"||!digest.match(row["sha256"].toString()).hasMatch()||!file.isFile()||file.isSymLink()||file.size()!=row["bytes"].toInteger()||file.size()>64*1024*1024
            ||fileHash(file.absoluteFilePath())!=row["sha256"].toString())return 3;names.insert(name);ids.insert(id);}
    if(!QDir().mkpath(out.absolutePath()))return 3;
    Gpu gpu;const bool available=gpu.initialize();QJsonArray results;QString error;
    const auto finish=[&](const QString &why){const QJsonObject result{{"schema",1},{"passed",why.isEmpty()},{"error",why},{"source_commit",JIXELLIGHT_GIT_COMMIT},{"engine",ProcessingPlan::EngineVersion},
        {"manifest_sha256",QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex())},{"GPU_required",true},{"backend",gpu.backend()},{"cases",results},
        {"camera_color_calibration_accepted",false},{"native_float_RAW_decoder",false},{"photographs_published",false}};const bool saved=write(out.filePath("report.json"),QJsonDocument(result).toJson(QJsonDocument::Indented));return why.isEmpty()&&saved?0:1;};
    if(!available)return finish("Required compute GPU unavailable");
    for(const auto &value:items){
        const auto item=value.toObject();const auto name=item["file"].toString(),path=fixtures.filePath(name);QElapsedTimer clock;clock.start();RawMetadata metadata;metadata.captureSensorAudit=true;
        const auto full=RawDecoder::decode(path,&error,&metadata);const auto decodeMs=clock.elapsed();
        if(full.isNull()||full.format()!=QImage::Format_RGBA64||full.text("JixelLightWorkingSpace")!="Linear ProPhoto RGB"||metadata.workingSpace!="Linear ProPhoto RGB")return finish("RAW decode did not return declared linear RGBA64: "+error);
        if(!metadata.model.contains(item["model"].toString(),Qt::CaseInsensitive)||metadata.bitsPerChannel!=16||!metadata.cameraMatrixEnabled||!metadata.cameraWhiteBalanceEnabled||metadata.highlightMode!=1||metadata.highlightBlendEnabled)return finish("Decoded camera/policy facts differ");
        if(metadata.sensorAudit["cfa_kind"].toString()!=item["cfa"].toString()||!metadata.sensorAudit["available"].toBool())return finish("Unpacked CFA audit missing/wrong");
        for(int y=0;y<full.height();++y){const auto *row=reinterpret_cast<const QRgba64 *>(full.constScanLine(y));for(int x=0;x<full.width();++x)if(row[x].alpha()!=65535)return finish("RAW alpha is not opaque");}
        const auto sample=full.copy(QRect(full.width()/2-64,full.height()/2-48,128,96));const auto prefix=item["id"].toString();
        if(!write(out.filePath(prefix+".linear.rgba16"),rgba16(sample)))return finish("Cannot save owned input sample");
        QJsonArray renders;
        for(const double exposure:{0.,.75}){
            AdjustmentState state;state.exposure=exposure;const auto plan=ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::ProPhotoRgb,true,0);
            const auto cpu=ImagePipeline::processWithPlan(sample,plan);const auto actual=gpu.render(sample,plan);if(cpu.isNull()||actual.isNull()||cpu.size()!=actual.size())return finish("CPU/compute render failed");
            int maximum=0;for(int y=0;y<cpu.height();++y){const auto *a=reinterpret_cast<const QRgba64 *>(actual.constScanLine(y)),*b=reinterpret_cast<const QRgba64 *>(cpu.constScanLine(y));for(int x=0;x<cpu.width();++x){if(a[x].alpha()!=b[x].alpha())return finish("GPU alpha differs");maximum=std::max({maximum,std::abs(int(a[x].red())-b[x].red()),std::abs(int(a[x].green())-b[x].green()),std::abs(int(a[x].blue())-b[x].blue())});}}
            if(maximum>40)return finish("GPU parity exceeds40/65535");const auto stem=prefix+(exposure==0?".zero":".ev075");
            if(!write(out.filePath(stem+".cpu.rgba16"),rgba16(cpu))||!write(out.filePath(stem+".gpu.rgba16"),rgba16(actual))||!exportPng16(sample,state,out.filePath(stem+".png"),ColorManagement::OutputSpace::ProPhotoRgb,{},&error,true,0))return finish("PNG16 export failed: "+error);
            renders.append(QJsonObject{{"exposure",exposure},{"raw_base_exposure_stops",plan.baseExposureStops},{"CPU_pixels_sha256",imageHash(cpu)},{"GPU_pixels_sha256",imageHash(actual)},
                {"ICC_sha256",QString::fromLatin1(QCryptographicHash::hash(ColorManagement::colorSpace(ColorManagement::OutputSpace::ProPhotoRgb).iccProfile(),QCryptographicHash::Sha256).toHex())},
                {"maximum_GPU_error_U16",maximum},{"actual_compute",true},{"CPU_reference",stem+".cpu.rgba16"},{"GPU_readback",stem+".gpu.rgba16"},{"PNG16",stem+".png"}});
        }
        if(fileHash(path)!=item["sha256"].toString())return finish("Original RAW changed");
        results.append(QJsonObject{{"id",prefix},{"source_sha256",item["sha256"]},{"make",metadata.make},{"model",metadata.model},{"width",full.width()},{"height",full.height()},
            {"decoded_format","RGBA64"},{"decoded_pixels_sha256",imageHash(full)},{"demosaic",metadata.demosaic},{"linear_working_space",metadata.workingSpace},{"source_alpha_exact",true},{"source_unchanged",true},{"decode_ms",decodeMs},
            {"sensor_audit",metadata.sensorAudit},{"sample_width",sample.width()},{"sample_height",sample.height()},{"linear_sample",prefix+".linear.rgba16"},{"renders",renders}});
    }
    return finish({});
}
