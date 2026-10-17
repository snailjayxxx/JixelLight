#include "core/raw/SensorLinearMosaic.h"
#include <QCryptographicHash>
#include <QJsonArray>
#include <array>

namespace {
constexpr qint64 kMaxRawPixels = 200000000;
constexpr qint64 kMaxTilePixels = 1024LL*1024; // 4 MiB float result; tile large RAWs
bool lowerHexSha256(const QByteArray &s) {
    if(s.size()!=64) return false;
    for(char c:s) if(!((c>='0'&&c<='9')||(c>='a'&&c<='f'))) return false;
    return true;
}
}

bool SensorLinearMosaic::validCalibration(const Calibration &c) {
    if(!lowerHexSha256(c.sourceSha256)||!lowerHexSha256(c.calibrationSha256)) return false;
    int reds=0,blues=0,greens=0;
    for(int i=0;i<4;++i) {
        const auto color=c.cfa[std::size_t(i)];
        if(color==0) ++reds;
        else if(color==2) ++blues;
        else if(color==1||color==3) ++greens;
        else return false;
        if(c.black[std::size_t(i)]>=c.white[std::size_t(i)] || c.white[std::size_t(i)]>65535)
            return false;
    }
    return reds==1 && blues==1 && greens==2;
}

QByteArray SensorLinearMosaic::decoderSignature(const Calibration &c) {
    if(!validCalibration(c)) return {};
    // Only decimal ASCII integers and lowercase digests: no native-endian,
    // locale, float or JSON serializer dependency in this cache identity.
    QByteArray canonical("JixelLight-Bayer-single-plane-normalization-v1\n");
    canonical+=c.sourceSha256; canonical+='\n';
    canonical+=c.calibrationSha256; canonical+='\n';
    for(int i=0;i<4;++i) {
        canonical+=QByteArray::number(int(c.cfa[std::size_t(i)]));canonical+=',';
        canonical+=QByteArray::number(qulonglong(c.black[std::size_t(i)]));canonical+=',';
        canonical+=QByteArray::number(qulonglong(c.white[std::size_t(i)]));canonical+='\n';
    }
    return QCryptographicHash::hash(canonical,QCryptographicHash::Sha256).toHex();
}

SensorLinearMosaic::Tile SensorLinearMosaic::normalizeTile(
        const quint16 *raw,const Layout &layout,const Calibration &calibration,
        const QRect &rawRoi,const CancelToken &cancel) {
    Tile tile;
    const auto fail=[&](const char *why) {
        tile.error=QString::fromLatin1(why);
        tile.samples.clear();
        tile.statistics={};
        tile.decoderSignature.clear();
        tile.rawRoi={};tile.activeBounds={};
        return tile;
    };
    if(cancelled(cancel)) return fail("cancelled");
    if(!validCalibration(calibration)) return fail("missing or invalid explicit Bayer calibration");
    if(!raw) return fail("unpacked single-plane U16 buffer unavailable");
    if(layout.rawWidth<=0||layout.rawHeight<=0||layout.rawWidth>32768||layout.rawHeight>32768)
        return fail("invalid sensor dimensions");
    if(qint64(layout.rawWidth)*layout.rawHeight>kMaxRawPixels)
        return fail("sensor exceeds 200-million-site limit");
    if(qint64(layout.pitchBytes)!=qint64(layout.rawWidth)*2)
        return fail("unsupported U16 row pitch");
    const QRect sensor(0,0,layout.rawWidth,layout.rawHeight);
    if(!layout.active.isValid()||!sensor.contains(layout.active))
        return fail("invalid active sensor bounds");
    if(!rawRoi.isValid()||!layout.active.contains(rawRoi))
        return fail("tile must be within active sensor bounds");
    const qint64 pixels=qint64(rawRoi.width())*rawRoi.height();
    if(pixels<=0||pixels>kMaxTilePixels)
        return fail("tile exceeds one-million-site memory cap");
    const QByteArray signature=decoderSignature(calibration);
    if(signature.size()!=64) return fail("invalid decoder signature");
    tile.samples.resize(qsizetype(pixels));
    std::array<qint64,4> siteCounts{};
    qint64 negative=0,atOrAboveWhite=0;
    for(int y=rawRoi.top();y<=rawRoi.bottom();++y) {
        if(cancelled(cancel)) return fail("cancelled");
        const quint16 *src=raw+qsizetype(y)*layout.rawWidth+rawRoi.left();
        for(int x=rawRoi.left();x<=rawRoi.right();++x) {
            const int phase=(((y-layout.active.top())&1)<<1)|
                            ((x-layout.active.left())&1);
            const auto index=std::size_t(phase);
            const auto code=src[x-rawRoi.left()];
            const double black=double(calibration.black[index]);
            const double denominator=double(calibration.white[index])-black;
            const float normalized=float((double(code)-black)/denominator);
            tile.samples[qsizetype(y-rawRoi.top())*rawRoi.width()+x-rawRoi.left()]=normalized;
            ++siteCounts[index];
            negative+=code<calibration.black[index];
            atOrAboveWhite+=code>=calibration.white[index];
        }
    }
    if(cancelled(cancel)) return fail("cancelled");
    QJsonArray counts,cfa;
    for(int i=0;i<4;++i) {
        counts.append(siteCounts[std::size_t(i)]);
        cfa.append(int(calibration.cfa[std::size_t(i)]));
    }
    tile.activeBounds=layout.active;
    tile.rawRoi=rawRoi;
    tile.decoderSignature=signature;
    tile.statistics=QJsonObject{
        {"schema",1},{"available",true},
        {"kind",QStringLiteral("unclamped_single_channel_bayer_mosaic_not_rgb")},
        {"demosaiced",false},{"white_balanced",false},{"camera_color_matrix_applied",false},
        {"tile_raw_bounds",QJsonArray{rawRoi.x(),rawRoi.y(),rawRoi.width(),rawRoi.height()}},
        {"sample_count",pixels},{"site_counts",counts},{"cfa_visible_origin",cfa},
        {"negative_samples",negative},{"at_or_above_calibrated_white_samples",atOrAboveWhite},
        {"source_sha256",QString::fromLatin1(calibration.sourceSha256)},
        {"calibration_sha256",QString::fromLatin1(calibration.calibrationSha256)},
        {"decoder_signature",QString::fromLatin1(signature)},
        {"warning",QStringLiteral("A normalized CFA site is not scene RGB; no WB, demosaic, color transform or highlight recovery")}
    };
    return tile;
}
