#include "core/look/LookLut.h"
#include <QCryptographicHash>
#include <QDataStream>
#include <QIODevice>
#include <QRegularExpression>
#include <QTextStream>
#include <algorithm>
#include <cmath>

namespace {
QByteArray payload(const LookLut &lut) {
    QByteArray bytes;QDataStream stream(&bytes,QIODevice::WriteOnly);
    stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    for(float v:lut.rgb)stream<<v;return bytes;
}
QString hashOf(int n,const QByteArray &bytes) {
    return QString::fromLatin1(QCryptographicHash::hash(QByteArray("JixelLook-v1-srgb:")+QByteArray::number(n)+':'+bytes,QCryptographicHash::Sha256).toHex());
}
}
bool LookLut::validate(QString *error) const {
    if(error)error->clear();
    if(size<2||size>33||rgb.size()!=size*size*size*3) {if(error)*error="LUT size/count is invalid (2..33 supported)";return false;}
    for(float v:rgb)if(!std::isfinite(v)||v<0||v>1) {if(error)*error="LUT contains non-finite/out-of-range values";return false;}
    return true;
}
void LookLut::updateDigest(){digest=hashOf(size,payload(*this));}
std::array<float,3> LookLut::sample(float r,float g,float b) const {
    const float x=std::clamp(r,0.f,1.f)*(size-1),y=std::clamp(g,0.f,1.f)*(size-1),z=std::clamp(b,0.f,1.f)*(size-1);
    const int ix=std::min(int(x),size-2),iy=std::min(int(y),size-2),iz=std::min(int(z),size-2);
    const float fx=x-ix,fy=y-iy,fz=z-iz;std::array<float,3> out{};
    for(int k=0;k<2;++k)for(int j=0;j<2;++j)for(int i=0;i<2;++i) {
        const float w=(i?fx:1-fx)*(j?fy:1-fy)*(k?fz:1-fz);
        const int index=((iz+k)*size*size+(iy+j)*size+ix+i)*3;
        for(int c=0;c<3;++c)out[c]+=rgb[index+c]*w;
    }
    return out;
}
QImage LookLut::atlas() const {
    if(!validate())return {};
    QImage out(size*size,size,QImage::Format_RGBA32FPx4);
    if(out.isNull())return {};
    for(int b=0;b<size;++b)for(int g=0;g<size;++g)for(int r=0;r<size;++r) {
        auto *p=reinterpret_cast<float*>(out.scanLine(g))+4*(b*size+r);
        const int i=(b*size*size+g*size+r)*3;
        p[0]=rgb[i];p[1]=rgb[i+1];p[2]=rgb[i+2];p[3]=1;
    }
    return out;
}
QJsonObject LookLut::toJson() const {
    const auto bytes=payload(*this);
    return {{"schema",1},{"space","display-srgb"},{"size",size},{"title",title.left(128)},
            {"dataLE",QString::fromLatin1(bytes.toBase64())},{"sha256",hashOf(size,bytes)},{"evidence",evidence}};
}
std::shared_ptr<const LookLut> LookLut::fromJson(const QJsonObject &j,QString *error) {
    const auto fail=[&](const char *s)->std::shared_ptr<const LookLut>{if(error)*error=s;return {};};
    if(j["schema"].toInt()!=1||j["space"].toString()!="display-srgb")return fail("Unsupported LUT schema or color domain");
    const int n=j["size"].toInt();if(n<2||n>33)return fail("Invalid LUT grid size");
    const auto text=j["dataLE"].toString().toLatin1();if(text.size()>600000)return fail("LUT payload too large");
    const auto decoded=QByteArray::fromBase64Encoding(text,QByteArray::AbortOnBase64DecodingErrors);
    if(!decoded)return fail("Invalid LUT base64");const QByteArray bytes=decoded.decoded;
    if(bytes.size()!=n*n*n*12||hashOf(n,bytes)!=j["sha256"].toString())return fail("LUT integrity check failed");
    auto lut=std::make_shared<LookLut>();lut->size=n;lut->title=j["title"].toString().left(128);lut->evidence=j["evidence"].toObject();
    QDataStream stream(bytes);stream.setByteOrder(QDataStream::LittleEndian);stream.setFloatingPointPrecision(QDataStream::SinglePrecision);
    lut->rgb.resize(n*n*n*3);for(auto &v:lut->rgb)stream>>v;
    if(stream.status()!=QDataStream::Ok||!lut->validate(error))return {};lut->updateDigest();return lut;
}
std::shared_ptr<const LookLut> LookLut::identity(int n) {
    auto l=std::make_shared<LookLut>();l->size=std::clamp(n,2,33);n=l->size;l->title="Identity";
    for(int b=0;b<n;++b)for(int g=0;g<n;++g)for(int r=0;r<n;++r)l->rgb<<float(r)/(n-1)<<float(g)/(n-1)<<float(b)/(n-1);
    l->updateDigest();return l;
}
std::shared_ptr<const LookLut> LookLut::fromCube(const QByteArray &text,QString *error) {
    if(error)error->clear();
    const auto fail=[&](const char *s)->std::shared_ptr<const LookLut>{if(error)*error=s;return {};};
    if(text.size()>4*1024*1024)return fail("Cube file exceeds 4 MiB");
    auto l=std::make_shared<LookLut>();l->title="Imported cube";
    const auto lines=QString::fromUtf8(text).split('\n');
    for(QString line:lines) {
        line=line.section('#',0,0).trimmed();if(line.isEmpty())continue;
        const auto parts=line.split(QRegularExpression("\\s+"),Qt::SkipEmptyParts);
        if(parts[0]=="TITLE"){l->title=line.mid(5).trimmed().remove('"').left(128);continue;}
        if(parts[0]=="LUT_3D_SIZE") {bool ok=false;int n=parts.value(1).toInt(&ok);if(!ok||n<2||n>33||l->size||parts.size()!=2)return fail("Invalid/duplicate LUT_3D_SIZE");l->size=n;continue;}
        if(parts[0]=="DOMAIN_MIN"||parts[0]=="DOMAIN_MAX") {
            if(parts.size()!=4)return fail("Invalid LUT domain");const float expected=parts[0]=="DOMAIN_MIN"?0:1;
            for(int i=1;i<4;++i){bool ok=false;double v=parts[i].toDouble(&ok);if(!ok||v!=expected)return fail("Only normalized 0..1 display-sRGB LUT domains are supported");}continue;
        }
        if(!l->size||parts.size()!=3||l->rgb.size()>=l->size*l->size*l->size*3)return fail("Unexpected cube directive or sample count");
        for(const auto &p:parts){bool ok=false;float v=p.toFloat(&ok);if(!ok||!std::isfinite(v)||v<0||v>1)return fail("Invalid cube sample");l->rgb<<v;}
    }
    if(!l->validate(error))return {};l->evidence={{"kind","user-imported"},{"calibrated",false}};l->updateDigest();return l;
}
