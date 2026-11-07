#include "core/raw/SensorMosaicAudit.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QByteArray>
#include <QCryptographicHash>
#include <QJsonArray>
#include <QVector>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <memory>

QJsonObject SensorMosaicAudit::capture(const quint16 *raw,const Layout &layout,
                                       const CancelToken &cancel) {
    QJsonObject unavailable{{"schema",1},{"available",false},{"stage","LibRaw unpacked raw_image before dcraw_process"},
                            {"rgb_demosaiced",false},{"black_subtracted",false}};
    const auto fail=[&](const char *reason) {
        auto result=unavailable;result["reason"]=QString::fromLatin1(reason);return result;
    };
    if(cancelled(cancel)) return fail("cancelled");
    if(!raw) return fail("single-plane raw_image unavailable (non-Bayer or unsupported source)");
    if(layout.rawWidth<=0 || layout.rawHeight<=0 || layout.rawWidth>32768 || layout.rawHeight>32768)
        return fail("invalid or unsupported raw sensor dimensions");
    const quint64 pixels=quint64(layout.rawWidth)*layout.rawHeight;
    if(pixels>200000000ULL) return fail("sensor sampling exceeds 200 million pixel audit limit");
    // Current validated LibRaw raw_image contract is the tightly packed
    // 16-bit plane. Do not reinterpret padded or packed proprietary layouts.
    if(qint64(layout.pitchBytes)!=qint64(layout.rawWidth)*2)
        return fail("unsupported RAW row pitch (not a packed U16 mosaic)");
    const QRect bounds(0,0,layout.rawWidth,layout.rawHeight);
    if(!layout.active.isValid() || layout.active.isEmpty() || !bounds.contains(layout.active))
        return fail("invalid or unsupported active sensor bounds");
    PerformanceSpan timer(QStringLiteral("sensor_mosaic_audit"),
        {{"pixels",qint64(layout.active.width())*layout.active.height()}});
    const qint64 sampleCount=qint64(layout.active.width())*layout.active.height();
    if(sampleCount<=0 || sampleCount>200000000) return fail("invalid active sensor sample count");
    auto histogram=std::make_unique<std::array<quint64,65536>>();
    histogram->fill(0);
    struct Parity { quint64 count=0,sum=0; quint16 low=65535,high=0; };
    std::array<Parity,4> parity{};
    quint64 total=0,atMax16=0,aboveNominal=0,atOrBelowBlack=0;
    quint16 minimum=65535,maximum=0;
    const bool haveWhite=layout.nominalWhite>0 && layout.nominalWhite<=65535;
    const bool haveBlack=layout.nominalBlack<=65535;
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(QByteArrayView("JixelLight-sensor-U16-LE-active-v1"));
    QByteArray row(layout.active.width()*2,Qt::Uninitialized);
    for(int y=layout.active.top();y<=layout.active.bottom();++y) {
        if(cancelled(cancel)) return fail("cancelled");
        const auto *samples=raw+qsizetype(y)*layout.rawWidth+layout.active.left();
        for(int x=0;x<layout.active.width();++x) {
            const quint16 code=samples[x];
            row[2*x]=char(code&0xff);
            row[2*x+1]=char(code>>8);
            ++(*histogram)[code];
            minimum=std::min(minimum,code);maximum=std::max(maximum,code);
            total+=code;
            atMax16+=code==65535;
            if(haveWhite)aboveNominal+=code>=layout.nominalWhite;
            if(haveBlack)atOrBelowBlack+=code<=layout.nominalBlack;
            const int px=layout.active.left()+x;
            auto &group=parity[std::size_t(((y&1)<<1)|(px&1))];
            ++group.count;group.sum+=code;
            group.low=std::min(group.low,code);group.high=std::max(group.high,code);
        }
        hash.addData(row);
    }
    if(cancelled(cancel)) return fail("cancelled");
    const auto quantile=[&](quint64 numerator,quint64 denominator) {
        const quint64 target=(quint64(sampleCount-1)*numerator)/denominator;
        quint64 running=0;
        for(unsigned code=0;code<65536;++code) {
            running+=(*histogram)[code];
            if(running>target) return int(code);
        }
        return 65535;
    };
    QJsonArray classes;
    for(const auto &group:parity)
        classes.append(QJsonObject{{"pixels",qint64(group.count)},
            {"min",group.count?QJsonValue(int(group.low)):QJsonValue()},
            {"max",group.count?QJsonValue(int(group.high)):QJsonValue()},
            {"mean",group.count?QJsonValue(double(group.sum)/group.count):QJsonValue()}});
    QJsonObject result{{"schema",1},{"available",true},
        {"stage","LibRaw rawdata.raw_image immediately after unpack; pre-demosaic"},
        {"storage","single-component U16; no interpretation as linear RGB"},
        {"rgb_demosaiced",false},{"black_subtracted",false},
        {"unpacked_sensor_codes_may_have_camera_linearization",true},
        {"raw_width",layout.rawWidth},{"raw_height",layout.rawHeight},
        {"active_bounds",QJsonArray{layout.active.x(),layout.active.y(),layout.active.width(),layout.active.height()}},
        {"sample_count",sampleCount},{"minimum_code",int(minimum)},{"maximum_code",int(maximum)},
        {"mean_code",double(total)/sampleCount},
        {"p50_code",quantile(50,100)},{"p99_code",quantile(99,100)},
        {"p999_code",quantile(999,1000)},
        {"at_code_65535",qint64(atMax16)},
        {"parity_2x2_raw_grid_not_cfa_color",classes},
        {"pixel_sha256",QString::fromLatin1(hash.result().toHex())},
        {"pixel_hash_format","SHA256 over label plus active row-major U16 LE; no row padding"},
        {"nominal_black_code",qint64(layout.nominalBlack)},
        {"nominal_white_code",qint64(layout.nominalWhite)},
        {"note","Raw sample distributions and threshold counts are evidence, not actual recoverable highlight stops or a decoded RGB image"}};
    result["at_or_above_nominal_white_code"]=haveWhite ? QJsonValue(qint64(aboveNominal)) : QJsonValue();
    result["at_or_below_nominal_black_code"]=haveBlack ? QJsonValue(qint64(atOrBelowBlack)) : QJsonValue();
    return result;
}
