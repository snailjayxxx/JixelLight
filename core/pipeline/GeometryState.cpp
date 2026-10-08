#include "GeometryState.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QColorSpace>
#include <QRgba64>
#include <numbers>

QSize GeometryState::straightenedSize(QSize source) const {
    if (source.isEmpty() || !validStraighten(straighten)) return {};
    if (straighten==0) return source;
    if (source.width()==1 || source.height()==1) return {1,1};
    // Inscribe a centered rectangle with the original sample-grid aspect.
    // Pixel centers, not outer pixel edges, bound all bilinear samples.
    const double w=source.width()-1, h=source.height()-1;
    const double a=std::abs(straighten)*std::numbers::pi/180, c=std::cos(a), s=std::sin(a);
    const double scale=std::min(w/(c*w+s*h),h/(s*w+c*h));
    return {std::min(source.width(),int(std::floor(w*scale))+1),
            std::min(source.height(),int(std::floor(h*scale))+1)};
}

QImage GeometryState::apply(const QImage &source, const CancelToken &cancel) const {
    if (source.isNull() || cancelled(cancel) || !validStraighten(straighten)
        || !validCrop(crop.x(),crop.y(),crop.width(),crop.height())) return {};
    QImage result=source;
    if (straighten!=0) {
        PerformanceSpan timer("geometry_straighten",{{"degrees",straighten},{"source_pixels",qint64(source.width())*source.height()}});
        const auto input=source.format()==QImage::Format_RGBA64 ? source : source.convertToFormat(QImage::Format_RGBA64);
        if (input.isNull() || cancelled(cancel)) return {};
        result=QImage(straightenedSize(source.size()),QImage::Format_RGBA64);
        if (result.isNull()) return {};
        result.setColorSpace(source.colorSpace()); result.setDevicePixelRatio(source.devicePixelRatio());
        result.setDotsPerMeterX(source.dotsPerMeterX()); result.setDotsPerMeterY(source.dotsPerMeterY());
        for (const auto &key : source.textKeys()) result.setText(key,source.text(key));
        const double angle=straighten*std::numbers::pi/180, c=std::cos(angle), s=std::sin(angle);
        const double cx=(input.width()-1)/2.0, cy=(input.height()-1)/2.0;
        const double ox=(result.width()-1)/2.0, oy=(result.height()-1)/2.0;
        for (int y=0;y<result.height();++y) {
            if (cancelled(cancel)) return {};
            auto out=reinterpret_cast<QRgba64 *>(result.scanLine(y));
            for (int x=0;x<result.width();++x) {
                // The inscribed grid already lies inside the source; clamp only
                // floating-point roundoff at the last valid sample center.
                const double sx=std::clamp(cx+c*(x-ox)+s*(y-oy),0.0,double(input.width()-1));
                const double sy=std::clamp(cy-s*(x-ox)+c*(y-oy),0.0,double(input.height()-1));
                const int ix=int(sx), iy=int(sy), jx=std::min(ix+1,input.width()-1), jy=std::min(iy+1,input.height()-1);
                const auto top=reinterpret_cast<const QRgba64 *>(input.constScanLine(iy));
                const auto bottom=reinterpret_cast<const QRgba64 *>(input.constScanLine(jy));
                const QRgba64 p[]{top[ix],top[jx],bottom[ix],bottom[jx]};
                const double dx=sx-ix, dy=sy-iy, w[]{(1-dx)*(1-dy),dx*(1-dy),(1-dx)*dy,dx*dy};
                double r=0,g=0,b=0,a=0;
                // Interpolate premultiplied values, then retain unassociated
                // RGBA64 storage. Opaque RAW images keep all 16-bit precision.
                for (int i=0;i<4;++i) { const double aw=w[i]*p[i].alpha(); a+=aw; r+=aw*p[i].red(); g+=aw*p[i].green(); b+=aw*p[i].blue(); }
                auto channel=[](double value) { return quint16(std::clamp(std::lround(value),0L,65535L)); };
                out[x]=a>0 ? QRgba64::fromRgba64(channel(r/a),channel(g/a),channel(b/a),channel(a)) : QRgba64::fromRgba64(0,0,0,0);
            }
        }
    }
    if (cancelled(cancel)) return {};
    if (crop!=QRectF(0,0,1,1)) {
        const int left=std::clamp(int(std::floor(crop.x()*result.width())),0,result.width()-1);
        const int top=std::clamp(int(std::floor(crop.y()*result.height())),0,result.height()-1);
        const int right=std::clamp(int(std::ceil(crop.right()*result.width())),left+1,result.width());
        const int bottom=std::clamp(int(std::ceil(crop.bottom()*result.height())),top+1,result.height());
        result=result.copy(left,top,right-left,bottom-top);
    }
    if (cancelled(cancel)) return {};
    if (quarterTurns%4) result=result.transformed(QTransform().rotate(90*(quarterTurns%4)),Qt::FastTransformation);
    if (cancelled(cancel)) return {};
    if (flipHorizontal||flipVertical) result=result.mirrored(flipHorizontal,flipVertical);
    return cancelled(cancel) ? QImage{} : result;
}
