#pragma once
#include <QByteArray>
#include <QImage>
#include <QJsonObject>
#include <QString>
#include "core/async/LatestJob.h"
#include "core/color/ColorManagement.h"

namespace SoftProof {
constexpr qint64 MaxPixels=8LL*1024*1024;
constexpr qint64 MaxProfileBytes=4LL*1024*1024;
class Profile {
public:
    static Profile fromIcc(const QByteArray &,QString *error=nullptr);
    static Profile fromFile(const QString &,QString *error=nullptr);
    static Profile fromOutputSpace(ColorManagement::OutputSpace,QString *error=nullptr);
    bool valid() const {return !m_icc.isEmpty();}
    const QByteArray &icc() const {return m_icc;}
    QString description() const {return m_description;}
    QString key() const {return m_key;}
    QString colorModel() const {return m_model;}
private:
    QByteArray m_icc;
    QString m_description,m_key,m_model;
};
enum class Intent { Perceptual=0,RelativeColorimetric=1,Saturation=2,AbsoluteColorimetric=3 };
struct Settings {
    Intent intent=Intent::RelativeColorimetric;
    bool simulatePaper=false,gamutWarning=false,blackPointCompensation=true;
};
struct Result { QImage image;QString error;QJsonObject facts;bool cancelled=false; };
// The established renderer encodes ProPhoto with pure gamma1.8. Use its actual
// transfer for this private proof input, independently of export ICC metadata.
QColorSpace workingColorSpace();
// Input is the independent encoded ProPhoto output, never a clipped sRGB display
// frame. Result remains encoded sRGB for the existing monitor-only presentation.
// Histograms/export/working images are not modified. Caller admits owned buffers.
Result transform(const QImage &wideFrame,const Profile &,const Settings &,const CancelToken &cancel={});
}
