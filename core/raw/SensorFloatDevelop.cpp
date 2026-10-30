#include "core/raw/SensorFloatDevelop.h"
#include "core/pipeline/ImagePipeline.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QByteArrayView>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace {
constexpr qint64 kMaxOutputPixels=1024LL*1024; // <=16 MiB FP32 per tile
bool lowerHexDigest(const QByteArray &digest) {
    if(digest.size()!=64) return false;
    for(const char c:digest)
        if(!((c>='a'&&c<='f')||(c>='0'&&c<='9'))) return false;
    return true;
}
int primaryColor(quint8 cfa) {
    return cfa==0?0:(cfa==2?2:1); // both verified greens are G
}
int siteAt(const QRect &active,int x,int y) {
    return (((y-active.top())&1)<<1)|((x-active.left())&1);
}
void digestDouble(QCryptographicHash &hash,double value) {
    static_assert(sizeof(double)==8 && std::numeric_limits<double>::is_iec559);
    std::uint64_t bits=0;
    std::memcpy(&bits,&value,sizeof(bits));
    char bytes[8];
    for(int i=0;i<8;++i) bytes[i]=char((bits>>(8*i))&0xff);
    hash.addData(QByteArrayView(bytes,8));
}
}

bool SensorFloatDevelop::validProfile(const Profile &p) {
    if(!lowerHexDigest(p.profileSha256)) return false;
    for(const double gain:p.whiteBalanceBySite)
        if(!std::isfinite(gain)||gain<=0.0||gain>64.0) return false;
    for(const double entry:p.cameraToProPhoto)
        if(!std::isfinite(entry)||std::fabs(entry)>16.0) return false;
    const auto &m=p.cameraToProPhoto;
    const double determinant=m[0]*(m[4]*m[8]-m[5]*m[7])-
                             m[1]*(m[3]*m[8]-m[5]*m[6])+
                             m[2]*(m[3]*m[7]-m[4]*m[6]);
    return std::isfinite(determinant)&&std::fabs(determinant)>1.e-8;
}

QByteArray SensorFloatDevelop::decoderSignature(
        const SensorLinearMosaic::Calibration &sensor,const Profile &profile) {
    if(!SensorLinearMosaic::validCalibration(sensor)||!validProfile(profile)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("JixelLight-Bayer-bilinear-WB-linear-ProPhoto-v1\n"));
    hash.addData(SensorLinearMosaic::decoderSignature(sensor));
    hash.addData(QByteArrayView("\n"));
    hash.addData(profile.profileSha256);
    for(const double gain:profile.whiteBalanceBySite) digestDouble(hash,gain);
    for(const double matrix:profile.cameraToProPhoto) digestDouble(hash,matrix);
    return hash.result().toHex();
}

SensorFloatDevelop::Frame SensorFloatDevelop::developTile(
        const SensorLinearMosaic::Tile &mosaic,
        const SensorLinearMosaic::Calibration &sensor,const Profile &profile,
        const QRect &outputRoi,const CancelToken &cancel) {
    Frame frame;
    const auto refuse=[&](const char *why) {
        frame.error=QString::fromLatin1(why);
        frame.image={};frame.rawRoi={};frame.decoderSignature={};
        frame.diagnostics={};
        return frame;
    };
    if(cancelled(cancel)) return refuse("cancelled");
    const QByteArray sourceSignature=SensorLinearMosaic::decoderSignature(sensor);
    const QByteArray signature=decoderSignature(sensor,profile);
    if(signature.size()!=64||sourceSignature.size()!=64)
        return refuse("explicit sensor and color calibration required");
    if(!mosaic.valid()||mosaic.decoderSignature!=sourceSignature ||
       mosaic.statistics.value("source_sha256").toString()!=QString::fromLatin1(sensor.sourceSha256)||
       mosaic.statistics.value("calibration_sha256").toString()!=QString::fromLatin1(sensor.calibrationSha256))
        return refuse("mosaic source/calibration identity mismatch");
    if(!mosaic.activeBounds.isValid()||mosaic.activeBounds.width()<2||
       mosaic.activeBounds.height()<2||!mosaic.rawRoi.isValid()||
       !mosaic.activeBounds.contains(mosaic.rawRoi))
        return refuse("unsupported or missing active CFA sensor bounds");
    const qint64 sourcePixels=qint64(mosaic.rawRoi.width())*mosaic.rawRoi.height();
    const qint64 pixels=qint64(outputRoi.width())*outputRoi.height();
    if(sourcePixels<=0||sourcePixels>kMaxOutputPixels||
       mosaic.samples.size()!=sourcePixels||!outputRoi.isValid()||
       !mosaic.rawRoi.contains(outputRoi)||pixels<=0||pixels>kMaxOutputPixels)
        return refuse("invalid or over-budget float mosaic tile");
    // A missing single-pixel halo changes both diagonal and axial
    // interpolation across tile seams; refuse rather than produce wrong RGB.
    const QRect support=outputRoi.adjusted(-1,-1,1,1).intersected(mosaic.activeBounds);
    if(!mosaic.rawRoi.contains(support))
        return refuse("missing required Bayer halo; tile would introduce seams");
    for(const float value:mosaic.samples)
        if(!std::isfinite(value)||std::fabs(value)>ImagePipeline::MaxLinearInputMagnitude)
            return refuse("non-finite or unbounded calibrated mosaic input");
    QImage image(outputRoi.size(),QImage::Format_RGBA32FPx4);
    if(image.isNull()) return refuse("unable to allocate bounded FP32 scene tile");
    image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb)
                            .withTransferFunction(QColorSpace::TransferFunction::Linear));
    image.setText(QStringLiteral("JixelLightSource"),
                  QStringLiteral("EXPERIMENTAL calibrated Bayer FP32"));
    image.setText(QStringLiteral("JixelLightWorkingSpace"),
                  QStringLiteral("Linear ProPhoto RGB"));
    image.setText(QStringLiteral("JixelLightDecoderSignature"),QString::fromLatin1(signature));
    image.setText(QStringLiteral("JixelLightSensorRawRoi"),
                  QStringLiteral("%1,%2,%3,%4").arg(outputRoi.x()).arg(outputRoi.y())
                      .arg(outputRoi.width()).arg(outputRoi.height()));
    std::array<double,3> minRgb{
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity(),
        std::numeric_limits<double>::infinity()};
    std::array<double,3> maxRgb{
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity()};
    qint64 underZero=0,overOne=0;
    for(int y=outputRoi.top();y<=outputRoi.bottom();++y) {
        if(cancelled(cancel)) return refuse("cancelled");
        auto *out=reinterpret_cast<float *>(image.scanLine(y-outputRoi.top()));
        for(int x=outputRoi.left();x<=outputRoi.right();++x) {
            std::array<double,3> camera{};
            for(int channel=0;channel<3;++channel) {
                // Current CFA site (0), axial neighbors (1) or diagonals
                // (2), never a farther class when a nearer color exists.
                int nearest=3,count=0;double sum=0;
                for(int dy=-1;dy<=1;++dy) for(int dx=-1;dx<=1;++dx) {
                    const int sx=x+dx,sy=y+dy;
                    if(!mosaic.activeBounds.contains(sx,sy)) continue;
                    const int site=siteAt(mosaic.activeBounds,sx,sy);
                    if(primaryColor(sensor.cfa[std::size_t(site)])!=channel) continue;
                    const int distance=dx*dx+dy*dy;
                    if(distance>nearest) continue;
                    if(distance<nearest) {nearest=distance;count=0;sum=0;}
                    const qsizetype index=qsizetype(sy-mosaic.rawRoi.top())*mosaic.rawRoi.width()+
                                          (sx-mosaic.rawRoi.left());
                    sum+=double(mosaic.samples[index])*profile.whiteBalanceBySite[std::size_t(site)];
                    ++count;
                }
                if(count==0) return refuse("missing Bayer channel neighbors");
                camera[std::size_t(channel)]=sum/count;
            }
            const auto &m=profile.cameraToProPhoto;
            auto *pixel=out+4*(x-outputRoi.left());
            for(int channel=0;channel<3;++channel) {
                const int k=channel*3;
                const double mapped=m[std::size_t(k)]*camera[0]+
                                    m[std::size_t(k+1)]*camera[1]+
                                    m[std::size_t(k+2)]*camera[2];
                if(!std::isfinite(mapped)||
                   std::fabs(mapped)>double(ImagePipeline::MaxLinearInputMagnitude))
                    return refuse("camera transform output exceeds finite scene range");
                pixel[channel]=float(mapped); // never pre-tone clamp RGB
                minRgb[std::size_t(channel)]=std::min(minRgb[std::size_t(channel)],mapped);
                maxRgb[std::size_t(channel)]=std::max(maxRgb[std::size_t(channel)],mapped);
                underZero+=mapped<0;
                overOne+=mapped>1;
            }
            pixel[3]=1.0f;
        }
    }
    if(cancelled(cancel)) return refuse("cancelled");
    if(!ImagePipeline::validFloatSource(image,cancel))
        return refuse("generated float scene fails pipeline validation");
    QJsonArray ranges;
    for(int i=0;i<3;++i)
        ranges.append(QJsonArray{minRgb[std::size_t(i)],maxRgb[std::size_t(i)]});
    frame.image=std::move(image);
    frame.rawRoi=outputRoi;
    frame.decoderSignature=signature;
    frame.diagnostics=QJsonObject{
        {"schema",1},{"kind",QStringLiteral("experimental_bayer_bilinear_fp32_scene")},
        {"input",QStringLiteral("calibrated_single_channel_Bayer_U16_mosaic")},
        {"working_space",QStringLiteral("Linear ProPhoto RGB (D50)")},
        {"output_raw_bounds",QJsonArray{outputRoi.x(),outputRoi.y(),outputRoi.width(),outputRoi.height()}},
        {"source_sha256",QString::fromLatin1(sensor.sourceSha256)},
        {"sensor_calibration_sha256",QString::fromLatin1(sensor.calibrationSha256)},
        {"color_profile_sha256",QString::fromLatin1(profile.profileSha256)},
        {"decoder_signature",QString::fromLatin1(signature)},
        {"pixel_count",pixels},{"negative_rgb_channel_values",underZero},
        {"above_one_rgb_channel_values",overOne},{"rgb_range",ranges},
        {"unverified_camera_calibration",true},
        {"algorithm",QStringLiteral("nearest-distance bilinear Bayer with per-site WB, double matrix accumulation")},
        {"warning",QStringLiteral("Experimental: not camera-validated; no highlight reconstruction or Sony look fitting")}
    };
    return frame;
}
