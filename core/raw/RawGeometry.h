#pragma once
#include <QRect>
#include <QSize>
#include <QStringList>
namespace RawGeometry {
// A metadata crop expressed in the decoded image coordinate system.
inline QRect orientCrop(QSize visible,QRect relative,int flip,QSize decoded) {
    if(!relative.isValid()||!QRect(QPoint(0,0),visible).contains(relative))return {};
    const int x=relative.x(),y=relative.y(),w=relative.width(),h=relative.height();
    QRect result;
    switch(flip) {
    case 0:result=relative;break;
    case 3:result={visible.width()-x-w,visible.height()-y-h,w,h};break;
    case 5:result={y,visible.width()-x-w,h,w};break;
    case 6:result={visible.height()-y-h,x,h,w};break;
    default:return {};
    }
    const QSize expected=(flip==5||flip==6)?visible.transposed():visible;
    return decoded==expected&&QRect(QPoint(0,0),decoded).contains(result)?result:QRect{};
}
inline QRect parseCrop(const QString &text,QSize decoded) {
    const auto fields=text.split(',');if(fields.size()!=4)return {};
    int v[4];for(int i=0;i<4;++i){bool ok=false;v[i]=fields[i].toInt(&ok);if(!ok)return {};}
    if(v[0]<0||v[1]<0||v[2]<=0||v[3]<=0||qint64(v[0])+v[2]>decoded.width()||qint64(v[1])+v[3]>decoded.height())return {};
    return {v[0],v[1],v[2],v[3]};
}
}
