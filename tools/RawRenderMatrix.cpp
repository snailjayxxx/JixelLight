#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QOffscreenSurface>
#include <QSaveFile>
#include <QSurfaceFormat>
#include <QRgba64>
#include <algorithm>
#include <array>
#include <cmath>
#include <memory>
#include <numeric>
#include <vector>

#include "core/gpu/GpuEngine.h"
#include "core/look/CameraReference.h"
#include "core/look/LookCalibration.h"
#include "core/look/LookProfiles.h"
#include "core/metadata/MetadataReader.h"
#include "core/pipeline/ImagePipeline.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/raw/RawDecoder.h"

namespace {
struct Vec3 { double r=0,g=0,b=0; };

QString sha256(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);if(!hash.addData(&file))return {};
    return QString::fromLatin1(hash.result().toHex());
}

bool writeJson(const QString &path,const QJsonObject &object) {
    QSaveFile file(path);if(!file.open(QIODevice::WriteOnly))return false;
    const QByteArray bytes=QJsonDocument(object).toJson(QJsonDocument::Indented);
    return file.write(bytes)==bytes.size()&&file.commit();
}

inline double srgbToLinear(double v) {
    v=std::clamp(v,0.0,1.0);
    return v<=0.04045?v/12.92:std::pow((v+0.055)/1.055,2.4);
}

std::array<double,3> labD65(Vec3 linear) {
    const double X=.4124564*linear.r+.3575761*linear.g+.1804375*linear.b;
    const double Y=.2126729*linear.r+.7151522*linear.g+.0721750*linear.b;
    const double Z=.0193339*linear.r+.1191920*linear.g+.9503041*linear.b;
    constexpr double Xn=.95047,Yn=1.0,Zn=1.08883,delta=6.0/29.0;
    const auto f=[](double t) {
        constexpr double d=6.0/29.0,d3=d*d*d;
        return t>d3?std::cbrt(t):t/(3*d*d)+4.0/29.0;
    };
    const double fx=f(X/Xn),fy=f(Y/Yn),fz=f(Z/Zn);
    return {116*fy-16,500*(fx-fy),200*(fy-fz)};
}

QJsonArray triple(double a,double b,double c) {return QJsonArray{a,b,c};}

QJsonObject imageStats(const QImage &source) {
    if(source.isNull())return {{"valid",false}};
    const QImage image=source.convertToFormat(QImage::Format_RGBA64);
    const qsizetype count=qsizetype(image.width())*image.height();
    if(count<=0)return {{"valid",false}};
    Vec3 encoded,linearSum,labSum;
    std::vector<double> lumas; lumas.reserve(static_cast<std::size_t>(count));
    quint64 shadowClip=0,highlightClip=0;
    for(int y=0;y<image.height();++y) {
        const auto *row=reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for(int x=0;x<image.width();++x) {
            const auto p=row[x];
            const double er=p.red()/65535.0,eg=p.green()/65535.0,eb=p.blue()/65535.0;
            encoded.r+=er;encoded.g+=eg;encoded.b+=eb;
            const Vec3 lin{srgbToLinear(er),srgbToLinear(eg),srgbToLinear(eb)};
            linearSum.r+=lin.r;linearSum.g+=lin.g;linearSum.b+=lin.b;
            const double luma=.2126*lin.r+.7152*lin.g+.0722*lin.b;lumas.push_back(luma);
            const auto lab=labD65(lin);labSum.r+=lab[0];labSum.g+=lab[1];labSum.b+=lab[2];
            if(p.red()==0&&p.green()==0&&p.blue()==0)++shadowClip;
            if(p.red()>=65534||p.green()>=65534||p.blue()>=65534)++highlightClip;
        }
    }
    const auto percentile=[&](double q) {
        const std::size_t index=std::min(lumas.size()-1,static_cast<std::size_t>(q*double(lumas.size()-1)));
        std::nth_element(lumas.begin(),lumas.begin()+static_cast<std::ptrdiff_t>(index),lumas.end());
        return lumas[index];
    };
    const double median=percentile(.50),p95=percentile(.95),p99=percentile(.99);

    double hueSin=0,hueCos=0,hueWeight=0;Vec3 white;quint64 whiteCount=0;
    for(int y=0;y<image.height();++y) {
        const auto *row=reinterpret_cast<const QRgba64 *>(image.constScanLine(y));
        for(int x=0;x<image.width();++x) {
            const auto p=row[x];
            const double er=p.red()/65535.0,eg=p.green()/65535.0,eb=p.blue()/65535.0;
            const Vec3 lin{srgbToLinear(er),srgbToLinear(eg),srgbToLinear(eb)};
            const double luma=.2126*lin.r+.7152*lin.g+.0722*lin.b;
            if(luma>=p95) {
                const double mx=std::max({er,eg,eb}),mn=std::min({er,eg,eb}),d=mx-mn;
                if(d>.02) {
                    double h=0;
                    if(mx==er)h=60*std::fmod((eg-eb)/d,6.0);
                    else if(mx==eg)h=60*((eb-er)/d+2.0);
                    else h=60*((er-eg)/d+4.0);
                    if(h<0)h+=360;
                    const double rad=h*3.14159265358979323846/180.0;
                    hueSin+=std::sin(rad)*d;hueCos+=std::cos(rad)*d;hueWeight+=d;
                }
            }
            if(luma>=p99) {white.r+=lin.r;white.g+=lin.g;white.b+=lin.b;++whiteCount;}
        }
    }
    const double n=double(count),meanLuma=.2126*(linearSum.r/n)+.7152*(linearSum.g/n)+.0722*(linearSum.b/n);
    QJsonObject result{{"valid",true},{"width",image.width()},{"height",image.height()},
        {"meanEncodedRgb",triple(encoded.r/n,encoded.g/n,encoded.b/n)},
        {"meanLinearRgb",triple(linearSum.r/n,linearSum.g/n,linearSum.b/n)},
        {"meanLinearLuma",meanLuma},{"medianLinearLuma",median},{"p95LinearLuma",p95},{"p99LinearLuma",p99},
        {"meanLabD65",triple(labSum.r/n,labSum.g/n,labSum.b/n)},
        {"shadowClipPercent",100.0*shadowClip/n},{"highlightClipPercent",100.0*highlightClip/n}};
    if(hueWeight>0) {
        double hue=std::atan2(hueSin,hueCos)*180.0/3.14159265358979323846;if(hue<0)hue+=360;
        result["highlightHueP95Degrees"]=hue;
    }
    if(whiteCount) {
        const double wr=white.r/whiteCount,wg=white.g/whiteCount,wb=white.b/whiteCount,sum=wr+wg+wb;
        if(sum>1e-12)result["p99WhiteRgbChromaticity"]=triple(wr/sum,wg/sum,wb/sum);
    }
    return result;
}

QJsonObject deltaMetrics(const QImage &a,const QImage &b) {
    if(a.isNull()||b.isNull()||a.size()!=b.size())return {{"valid",false}};
    const QImage x=a.convertToFormat(QImage::Format_RGBA64),y=b.convertToFormat(QImage::Format_RGBA64);
    long double squared=0,absolute=0;quint16 maximum=0;quint64 channels=0;
    for(int row=0;row<x.height();++row) {
        const auto *pa=reinterpret_cast<const QRgba64 *>(x.constScanLine(row));
        const auto *pb=reinterpret_cast<const QRgba64 *>(y.constScanLine(row));
        for(int col=0;col<x.width();++col)for(int c=0;c<3;++c) {
            const int va=c==0?pa[col].red():(c==1?pa[col].green():pa[col].blue());
            const int vb=c==0?pb[col].red():(c==1?pb[col].green():pb[col].blue());
            const quint16 d=quint16(std::abs(va-vb));maximum=std::max(maximum,d);
            const long double normalized=static_cast<long double>(d)/65535.0L;
            absolute+=normalized;squared+=normalized*normalized;++channels;
        }
    }
    return {{"valid",true},{"rmse",double(std::sqrt(squared/channels))},
            {"meanAbsolute",double(absolute/channels)},{"maxChannelCode16",int(maximum)}};
}

class GpuContext {
public:
    bool initialize() {
#ifdef Q_OS_WIN
        QRhiD3D11InitParams params;rhi.reset(QRhi::create(QRhi::D3D11,&params,QRhi::PreferSoftwareRenderer));
#elif defined(Q_OS_MACOS)
        QRhiMetalInitParams params;rhi.reset(QRhi::create(QRhi::Metal,&params));
#else
        QRhiGles2InitParams params;params.format=QSurfaceFormat::defaultFormat();
        surface.reset(QRhiGles2InitParams::newFallbackSurface(params.format));params.fallbackSurface=surface.get();
        rhi.reset(QRhi::create(QRhi::OpenGLES2,&params));
#endif
        if(!rhi||!rhi->isFeatureSupported(QRhi::Compute))return false;
        engine=std::make_unique<GpuEngine>(rhi.get());return true;
    }
    QImage render(const QImage &input,const ProcessingPlan &plan) {
        QRhiCommandBuffer *cb=nullptr;if(!rhi||rhi->beginOffscreenFrame(&cb)!=QRhi::FrameOpSuccess)return {};
        const QImage source=input.convertToFormat(QImage::Format_RGBA32FPx4);
        const bool ok=engine->process(cb,source,plan,++revision,{},false);
        QRhiReadbackResult readback;bool done=false;
        if(ok) {
            readback.completed=[&]{done=true;};auto *updates=rhi->nextResourceUpdateBatch();
            updates->readBackTexture(QRhiReadbackDescription(engine->outputTexture()),&readback);cb->resourceUpdate(updates);
        }
        rhi->endOffscreenFrame();rhi->finish();
        if(!ok||!done||readback.data.size()!=input.width()*input.height()*16)return {};
        QImage image(reinterpret_cast<const uchar *>(readback.data.constData()),input.width(),input.height(),input.width()*16,QImage::Format_RGBA32FPx4);
        QImage out=image.copy().convertToFormat(QImage::Format_RGBA64);out.setColorSpace(QColorSpace::SRgb);return out;
    }
    QString backend() const{return engine?engine->backendName():QStringLiteral("unavailable");}
private:
    std::unique_ptr<QOffscreenSurface> surface;std::unique_ptr<QRhi> rhi;std::unique_ptr<GpuEngine> engine;quint64 revision=0;
};

QImage normalizedReference(QImage image,QSize size) {
    if(image.isNull())return {};
    if(image.colorSpace().isValid())image=image.convertedToColorSpace(QColorSpace::SRgb);else image.setColorSpace(QColorSpace::SRgb);
    return image.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA64);
}

QJsonObject route(const QString &name,const QImage &image,const QString &fileName,const QImage &reference={}) {
    QJsonObject object{{"name",name},{"stats",imageStats(image)}};
    if(!fileName.isEmpty())object["file"]=fileName;
    if(!reference.isNull())object["vsPairedCameraJpeg"]=deltaMetrics(image,reference);
    return object;
}
}

int main(int argc,char **argv) {
    QSurfaceFormat format;format.setVersion(4,3);format.setProfile(QSurfaceFormat::CoreProfile);QSurfaceFormat::setDefaultFormat(format);
    QGuiApplication app(argc,argv);const QStringList args=app.arguments();
    if(args.size()!=3){qCritical("Usage: JixelLightRawRenderMatrix fixture-directory output-directory");return 2;}
    const QDir fixtures(QFileInfo(args[1]).absoluteFilePath());const QString output=QFileInfo(args[2]).absoluteFilePath();
    QFile manifest(fixtures.absoluteFilePath("dataset.json"));if(!manifest.open(QIODevice::ReadOnly))return 2;
    QJsonParseError parse;const QJsonDocument doc=QJsonDocument::fromJson(manifest.readAll(),&parse);
    const QJsonArray pairs=doc.object()["pairs"].toArray();if(parse.error!=QJsonParseError::NoError||pairs.isEmpty())return 2;
    if(!QDir().mkpath(output))return 2;

    GpuContext gpu;const bool hasGpu=gpu.initialize();
    if(!hasGpu&&qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")){qCritical("Required GPU unavailable");return 3;}
    QJsonArray scenes;bool failed=false;
    for(const auto &value:pairs) {
        const QJsonObject item=value.toObject();const QString id=item["id"].toString();
        const QString rawPath=fixtures.absoluteFilePath(item["raw"].toString()),jpegPath=fixtures.absoluteFilePath(item["jpeg"].toString());
        QString error;QVariantMap metadata=MetadataReader::read(rawPath,&error);
        QJsonObject scene{{"id",id},{"role",item["role"].toString()},{"rawSha256",sha256(rawPath)},{"jpegSha256",sha256(jpegPath)},
                          {"metadataError",error},{"recordedLook",QJsonObject::fromVariantMap(metadata.value("sonyLook").toMap())}};
        const auto paired=CameraReference::load({rawPath,jpegPath,metadata,1});
        QImage linear=RawDecoder::decode(rawPath,&error);scene["decodeError"]=error;
        if(linear.isNull()||paired.image.isNull()){scene["error"]=linear.isNull()?error:paired.error;scenes.append(scene);failed=true;continue;}
        QVariantMap geometry;linear=calibrationSource(linear,paired.image,&geometry);scene["geometry"]=QJsonObject::fromVariantMap(geometry);
        const QSize size=linear.size().scaled(QSize(512,512),Qt::KeepAspectRatio);
        const QImage rawSmall=linear.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation);
        const QImage reference=normalizedReference(paired.image,size);
        QImage embedded=RawDecoder::thumbnail(rawPath);const double ea=embedded.isNull()?0.0:double(embedded.width())/embedded.height();
        const double ra=double(reference.width())/reference.height();
        const bool embeddedAligned=!embedded.isNull()&&std::abs(ea/ra-1.0)<=.015;
        if(embeddedAligned)embedded=normalizedReference(embedded,size);

        double baseEv=metadata.value("rawBaselineExposure",0.0).toDouble();if(!std::isfinite(baseEv))baseEv=0;baseEv=std::clamp(baseEv,-8.0,8.0);
        const auto makePlan=[&](const AdjustmentState &state,double additionalEv=0.0) {
            return ProcessingPlan::compile(state,ImagePipeline::InputEncoding::LinearProPhoto,
                                           ColorManagement::OutputSpace::SRgb,true,float(baseEv+additionalEv));
        };
        AdjustmentState neutral;
        AdjustmentState asShot;asShot.look.mode="as-shot";asShot=LookProfiles::resolveAsShot(asShot,metadata,true);
        AdjustmentState fl3;fl3.look.mode="manual";fl3.look.code="FL3";fl3.look.strength=1.0;
        const ProcessingPlan neutralPlan=makePlan(neutral),asShotPlan=makePlan(asShot),fl3Plan=makePlan(fl3);
        const QImage cpuNeutral=ImagePipeline::processWithPlan(rawSmall,neutralPlan);
        const QImage cpuAsShot=ImagePipeline::processWithPlan(rawSmall,asShotPlan);
        const QImage cpuFl3=ImagePipeline::processWithPlan(rawSmall,fl3Plan);
        const QImage gpuNeutral=hasGpu?gpu.render(rawSmall,neutralPlan):QImage{};
        const QImage gpuAsShot=hasGpu?gpu.render(rawSmall,asShotPlan):QImage{};
        const QImage gpuFl3=hasGpu?gpu.render(rawSmall,fl3Plan):QImage{};

        const QString prefix=id+"-";
        const auto save=[&](const QImage &image,const QString &name){if(image.isNull())return QString{};const QString file=prefix+name+".png";return image.save(QDir(output).absoluteFilePath(file))?file:QString{};};
        QJsonArray routes;
        routes.append(route("paired-camera-jpeg",reference,save(reference,"01-paired-camera-jpeg")));
        QJsonObject embeddedRoute=route("embedded-camera-jpeg",embeddedAligned?embedded:RawDecoder::thumbnail(rawPath),
                                        embeddedAligned?save(embedded,"02-embedded-camera-jpeg"):QString{});
        embeddedRoute["alignmentComparable"]=embeddedAligned;
        if(embeddedAligned)embeddedRoute["vsPairedCameraJpeg"]=deltaMetrics(embedded,reference);
        routes.append(embeddedRoute);
        routes.append(route("cpu-raw-neutral",cpuNeutral,save(cpuNeutral,"03-cpu-raw-neutral"),reference));
        routes.append(route("gpu-raw-neutral",gpuNeutral,save(gpuNeutral,"04-gpu-raw-neutral"),reference));
        routes.append(route("cpu-raw-as-shot",cpuAsShot,save(cpuAsShot,"05-cpu-raw-as-shot"),reference));
        routes.append(route("gpu-raw-as-shot",gpuAsShot,save(gpuAsShot,"06-gpu-raw-as-shot"),reference));
        QJsonObject cpuFl3Route=route("cpu-raw-fl3-diagnostic",cpuFl3,save(cpuFl3,"07-cpu-raw-fl3"));cpuFl3Route["cameraOracleAvailable"]=false;routes.append(cpuFl3Route);
        QJsonObject gpuFl3Route=route("gpu-raw-fl3-diagnostic",gpuFl3,save(gpuFl3,"08-gpu-raw-fl3"));gpuFl3Route["cameraOracleAvailable"]=false;routes.append(gpuFl3Route);
        scene["routes"]=routes;
        scene["cpuGpuNeutral"]=deltaMetrics(cpuNeutral,gpuNeutral);
        scene["cpuGpuAsShot"]=deltaMetrics(cpuAsShot,gpuAsShot);
        scene["cpuGpuFl3"]=deltaMetrics(cpuFl3,gpuFl3);
        scene["baseExposureStops"]=baseEv;
        scene["rawNeutralSceneGray"]=ProcessingPlan::RawNeutralSceneGray;
        scene["rawNeutralDisplayGray"]=ProcessingPlan::RawNeutralDisplayGray;

        QJsonArray sweep;double bestRmse=1e9,bestOffset=0;
        for(double offset:{-1.5,-1.0,-.5,0.0,.5,1.0,1.5}) {
            const QImage candidate=ImagePipeline::processWithPlan(rawSmall,makePlan(neutral,offset));
            const QJsonObject delta=deltaMetrics(candidate,reference);const double rmse=delta.value("rmse").toDouble(1e9);
            sweep.append(QJsonObject{{"additionalBaseEv",offset},{"rmseToPairedCameraJpeg",rmse}});
            if(rmse<bestRmse){bestRmse=rmse;bestOffset=offset;}
        }
        scene["neutralExposureSweep"]=sweep;scene["bestAdditionalEvInSweep"]=bestOffset;scene["bestSweepRmse"]=bestRmse;
        scenes.append(scene);
    }

    QJsonObject report{{"schema",1},{"commit",JIXELLIGHT_GIT_COMMIT},{"engineVersion",ProcessingPlan::EngineVersion},
        {"gpuBackend",hasGpu?gpu.backend():QStringLiteral("unavailable")},{"sceneCount",scenes.size()},{"scenes",scenes},
        {"displayColorManagement",QJsonObject{{"processingOutput","encoded sRGB"},{"monitorTransform","display-only 33^3 ICC LUT in display.frag"},
                                              {"includedInImageMetrics",false},{"reason","monitor calibration must not alter export/scopes/fitting pixels"}}},
        {"limitations",QJsonArray{QStringLiteral("Current pinned Sony pairs were recorded with ST, not FL3."),
                                  QStringLiteral("FL3 rows validate CPU/GPU rendering consistency only; they do not claim camera-authentic FL3 matching."),
                                  QStringLiteral("Camera JPEG comparison includes Sony tone/rendering choices and is a regression oracle, not a sensor-linear ground truth.")}}};
    if(!writeJson(QDir(output).absoluteFilePath("raw-render-matrix.json"),report))return 4;
    qInfo().noquote()<<QString::fromUtf8(QJsonDocument(report).toJson(QJsonDocument::Indented));
    return failed?1:0;
}
