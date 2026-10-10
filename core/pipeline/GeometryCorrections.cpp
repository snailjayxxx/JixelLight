#include "GeometryState.h"
#include "core/async/ParallelRows.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QColorSpace>
#include <QRgba64>
#include <numbers>

namespace {
// Backward mapping, in source sample-center units. Forward operation order:
// radial lens/CA correction -> perspective -> clockwise straighten -> crop -> orientation.
// No lens profile or camera calibration is inferred from these manual values.
struct Mapping {
    const GeometryState &state;
    double cx, cy, c, s, radiusSquared;
    Mapping(const GeometryState &geometry, QSize size) : state(geometry),
        cx((size.width()-1)/2.0),cy((size.height()-1)/2.0),
        c(std::cos(geometry.straighten*std::numbers::pi/180)),
        s(std::sin(geometry.straighten*std::numbers::pi/180)),radiusSquared(cx*cx+cy*cy) {}
    QPointF project(double x, double y, double *denominator=nullptr) const {
        const double rx=c*x+s*y, ry=-s*x+c*y;
        const double d=1+state.perspectiveHorizontal*rx/std::max(.5,cx)
                        +state.perspectiveVertical*ry/std::max(.5,cy);
        if (denominator) *denominator=d;
        return {rx/d,ry/d};
    }
    double radial(QPointF p) const {
        return radiusSquared>0 ? 1+state.distortion*(p.x()*p.x()+p.y()*p.y())/radiusSquared : 1;
    }
    bool fits(double scale) const {
        double maxX=0,maxY=0;
        // A homography with positive denominator maps the rectangle to a
        // convex quadrilateral; extrema of either coordinate are at vertices.
        for (double x : {-cx*scale,cx*scale}) for (double y : {-cy*scale,cy*scale}) {
            double d; const auto p=project(x,y,&d);
            if (d<=0 || !std::isfinite(p.x()) || !std::isfinite(p.y())) return false;
            maxX=std::max(maxX,std::abs(p.x())); maxY=std::max(maxY,std::abs(p.y()));
        }
        if (maxX>cx || maxY>cy) return false;
        // Conservative bounds cover the ENTIRE rectangle, including curved
        // edges and each CA channel, rather than checking only output corners.
        // r² <= 1 also keeps 1+3*k*r² positive throughout the accepted domain.
        const double radialBound=radiusSquared>0 ? 1+std::max(0.0,state.distortion)*(maxX*maxX+maxY*maxY)/radiusSquared : 1;
        const double caBound=1+std::max({0.0,state.redCa,state.blueCa})/100;
        return maxX*radialBound*caBound<=cx && maxY*radialBound*caBound<=cy;
    }
};
struct Sample { double r=0,g=0,b=0,a=0; };
Sample sample(const QImage &input,double x,double y,std::atomic_bool &invalid) {
    // Only absorb rounding error: a mapping outside the source is a failed
    // correction, never an edge stretched silently across an invalid region.
    if (!std::isfinite(x) || !std::isfinite(y) || x < -1e-7 || y < -1e-7
        || x > input.width()-1+1e-7 || y > input.height()-1+1e-7) { invalid=true; return {}; }
    x=std::clamp(x,0.0,double(input.width()-1)); y=std::clamp(y,0.0,double(input.height()-1));
    const int ix=int(x),iy=int(y),jx=std::min(ix+1,input.width()-1),jy=std::min(iy+1,input.height()-1);
    const auto top=reinterpret_cast<const QRgba64 *>(input.constScanLine(iy));
    const auto bottom=reinterpret_cast<const QRgba64 *>(input.constScanLine(jy));
    const QRgba64 p[]{top[ix],top[jx],bottom[ix],bottom[jx]};
    const double dx=x-ix,dy=y-iy,w[]{(1-dx)*(1-dy),dx*(1-dy),(1-dx)*dy,dx*dy};
    Sample out;
    for (int i=0;i<4;++i) {
        const double a=w[i]*p[i].alpha(); out.a+=a;
        out.r+=a*p[i].red(); out.g+=a*p[i].green(); out.b+=a*p[i].blue();
    }
    if (out.a>0) { out.r/=out.a; out.g/=out.a; out.b/=out.a; }
    return out;
}
quint16 channel(double value) { return quint16(std::clamp(std::lround(value),0L,65535L)); }
}

QSize GeometryState::correctedSize(QSize source) const {
    if (!validCorrections() || !validStraighten(straighten) || source.isEmpty()) return {};
    if (!hasCorrections()) return straightenedSize(source);
    const Mapping map(*this,source);
    double low=0,high=1;
    if (map.fits(high)) low=1;
    else for (int i=0;i<52;++i) {
        const double mid=(low+high)/2;
        if (map.fits(mid)) low=mid; else high=mid;
    }
    return {int(std::floor((source.width()-1)*low))+1,int(std::floor((source.height()-1)*low))+1};
}

QImage GeometryState::resampleCorrections(const QImage &source,const CancelToken &cancel) const {
    PerformanceSpan timer("geometry_corrections",{{"backend","cpu"},{"source_pixels",qint64(source.width())*source.height()},
        {"perspective_horizontal",perspectiveHorizontal},{"perspective_vertical",perspectiveVertical},
        {"distortion",distortion},{"red_ca_percent",redCa},{"blue_ca_percent",blueCa},{"straighten",straighten}});
    const auto input=source.format()==QImage::Format_RGBA64 ? source : source.convertToFormat(QImage::Format_RGBA64);
    if (input.isNull() || cancelled(cancel)) return {};
    QImage result(correctedSize(source.size()),QImage::Format_RGBA64);
    if (result.isNull()) return {};
    result.setColorSpace(source.colorSpace()); result.setDevicePixelRatio(source.devicePixelRatio());
    result.setDotsPerMeterX(source.dotsPerMeterX()); result.setDotsPerMeterY(source.dotsPerMeterY());
    for (const auto &key : source.textKeys()) result.setText(key,source.text(key));
    const Mapping map(*this,source.size());
    const double ox=(result.width()-1)/2.0,oy=(result.height()-1)/2.0;
    auto *bits=result.bits(); const auto stride=result.bytesPerLine(); // Detach before parallel writers.
    std::atomic_bool invalid{false};
    ParallelRows::run(result.height(),result.width(),cancel,[&](int y) {
        auto *out=reinterpret_cast<QRgba64 *>(bits+y*stride);
        for (int x=0;x<result.width();++x) {
            if ((x%256==0 && cancelled(cancel)) || invalid.load(std::memory_order_relaxed)) return;
            const auto p=map.project(x-ox,y-oy);
            const double factor=map.radial(p),rx=p.x()*factor,ry=p.y()*factor;
            const auto green=sample(input,map.cx+rx,map.cy+ry,invalid);
            const auto red=redCa==0 ? green : sample(input,map.cx+rx*(1+redCa/100),map.cy+ry*(1+redCa/100),invalid);
            const auto blue=blueCa==0 ? green : sample(input,map.cx+rx*(1+blueCa/100),map.cy+ry*(1+blueCa/100),invalid);
            // Green defines alpha; channels interpolate associated colors at
            // their own radial position and return unassociated 16-bit values.
            out[x]=green.a>0 ? QRgba64::fromRgba64(channel(red.r),channel(green.g),channel(blue.b),channel(green.a)) : QRgba64::fromRgba64(0,0,0,0);
        }
    });
    return cancelled(cancel) || invalid ? QImage{} : result;
}
