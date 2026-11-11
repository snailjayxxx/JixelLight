#include "core/look/LookDetail.h"
#include "core/async/ParallelRows.h"
#include <QRgba64>
#include <algorithm>
#include <cmath>
#include <vector>

int LookDetail::halo(const std::array<float,4> &s){
 return std::max(s[0]>0?int(s[2]):0,s[1]>0?int(s[3]):0);
}
QImage LookDetail::apply(const QImage &image,const std::array<float,4> &s,const CancelToken &cancel){
 if(halo(s)==0||image.isNull())return image;
 const int w=image.width(),h=image.height(),rs=std::clamp(int(s[2]),1,12),rc=std::clamp(int(s[3]),1,12);
 const QImage input=image.format()==QImage::Format_RGBA64?image:image.convertToFormat(QImage::Format_RGBA64);
 QImage out=input.copy();if(out.isNull())return {};
 std::vector<std::array<float,2>> horizontal(size_t(w)*h);
 const auto luma=[](QRgba64 p){return (.2126f*(p.red()/65535.f)+.7152f*(p.green()/65535.f))+.0722f*(p.blue()/65535.f);};
 ParallelRows::run(h,w,cancel,[&](int y){
  const auto *row=reinterpret_cast<const QRgba64*>(input.constScanLine(y));
  for(int x=0;x<w;++x){float a=0,b=0;
   if(s[0]>0)for(int i=-rs;i<=rs;++i)a+=luma(row[std::clamp(x+i,0,w-1)]);
   if(s[1]>0)for(int i=-rc;i<=rc;++i)b+=luma(row[std::clamp(x+i,0,w-1)]);
   horizontal[size_t(y)*w+x]={a/(2*rs+1),b/(2*rc+1)};
  }
 });
 if(cancelled(cancel))return {};
 uchar *bits=out.bits();const qsizetype stride=out.bytesPerLine();
 ParallelRows::run(h,w,cancel,[&](int y){
  const auto *row=reinterpret_cast<const QRgba64*>(input.constScanLine(y));auto *dst=reinterpret_cast<QRgba64*>(bits+y*stride);
  for(int x=0;x<w;++x){float a=0,b=0;
   if(s[0]>0)for(int i=-rs;i<=rs;++i)a+=horizontal[size_t(std::clamp(y+i,0,h-1))*w+x][0];
   if(s[1]>0)for(int i=-rc;i<=rc;++i)b+=horizontal[size_t(std::clamp(y+i,0,h-1))*w+x][1];
   const float Y=luma(row[x]);const float delta=s[0]*(Y-a/(2*rs+1))+s[1]*(Y-b/(2*rc+1));
   const auto q=[&](quint16 v){return quint16(std::lround(std::clamp(v/65535.f+delta,0.f,1.f)*65535.f));};
   dst[x]=QRgba64::fromRgba64(q(row[x].red()),q(row[x].green()),q(row[x].blue()),row[x].alpha());
  }
 });
 return cancelled(cancel)?QImage{}:out;
}

int LookDetail::halo(const std::array<float,4> &s,const std::array<float,2> &independent) {
 return std::max(halo(s),independent[0]>0?int(std::ceil(std::clamp(independent[1],0.f,12.f))):0);
}
QImage LookDetail::apply(const QImage &image,const std::array<float,4> &s,
                        const std::array<float,2> &independent,const CancelToken &cancel) {
 // Preserve the original Sony path, allocation and arithmetic when disabled.
 if(independent[0]<=0)return apply(image,s,cancel);
 if(image.isNull()||cancelled(cancel))return {};
 const int w=image.width(),h=image.height(),rs=std::clamp(int(s[2]),1,12),rc=std::clamp(int(s[3]),1,12);
 const float radius=std::clamp(independent[1],0.f,12.f),normalizer=1+2*radius;
 const int extent=int(std::ceil(radius));
 const QImage input=image.format()==QImage::Format_RGBA64?image:image.convertToFormat(QImage::Format_RGBA64);
 QImage out=input.copy();if(out.isNull())return {};
 std::vector<std::array<float,4>> horizontal(size_t(w)*h);
 const auto luma=[](QRgba64 p){return (.2126f*(p.red()/65535.f)+.7152f*(p.green()/65535.f))+.0722f*(p.blue()/65535.f);};
 const auto weight=[radius](int offset){return std::clamp(radius+1-std::abs(float(offset)),0.f,1.f);};
 ParallelRows::run(h,w,cancel,[&](int y){
  const auto *row=reinterpret_cast<const QRgba64*>(input.constScanLine(y));
  for(int x=0;x<w;++x){float a=0,b=0,c=0,d=0;
   if(s[0]>0)for(int i=-rs;i<=rs;++i)a+=luma(row[std::clamp(x+i,0,w-1)]);
   if(s[1]>0)for(int i=-rc;i<=rc;++i)b+=luma(row[std::clamp(x+i,0,w-1)]);
   for(int i=-extent;i<=extent;++i){const auto p=row[std::clamp(x+i,0,w-1)];const float alpha=p.alpha()/65535.f,k=weight(i);
    c+=k*luma(p)*alpha;d+=k*alpha;}
   horizontal[size_t(y)*w+x]={a/(2*rs+1),b/(2*rc+1),c/normalizer,d/normalizer};
  }
 });
 if(cancelled(cancel))return {};
 uchar *bits=out.bits();const qsizetype stride=out.bytesPerLine();
 ParallelRows::run(h,w,cancel,[&](int y){
  const auto *row=reinterpret_cast<const QRgba64*>(input.constScanLine(y));auto *dst=reinterpret_cast<QRgba64*>(bits+y*stride);
  for(int x=0;x<w;++x){float a=0,b=0,c=0,d=0;
   if(s[0]>0)for(int i=-rs;i<=rs;++i)a+=horizontal[size_t(std::clamp(y+i,0,h-1))*w+x][0];
   if(s[1]>0)for(int i=-rc;i<=rc;++i)b+=horizontal[size_t(std::clamp(y+i,0,h-1))*w+x][1];
   for(int i=-extent;i<=extent;++i){const auto p=horizontal[size_t(std::clamp(y+i,0,h-1))*w+x];const float k=weight(i);c+=k*p[2];d+=k*p[3];}
   const float Y=luma(row[x]);float delta=s[0]*(Y-a/(2*rs+1))+s[1]*(Y-b/(2*rc+1));
   if(row[x].alpha()>0&&d>1e-12f)delta+=independent[0]*(Y-c/d);
   const auto q=[&](quint16 v){return quint16(std::lround(std::clamp(v/65535.f+delta,0.f,1.f)*65535.f));};
   dst[x]=QRgba64::fromRgba64(q(row[x].red()),q(row[x].green()),q(row[x].blue()),row[x].alpha());
  }
 });
 return cancelled(cancel)?QImage{}:out;
}
