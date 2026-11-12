#include "SoftProof.h"
#include <QColorSpace>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QRgba64>
#include <lcms2.h>
#include <array>
#include <memory>

namespace {
struct Context {
    QString error;
    cmsContext value=nullptr;
    Context(){value=cmsCreateContext(nullptr,this);if(value)cmsSetLogErrorHandlerTHR(value,[](cmsContext c,cmsUInt32Number,const char *message){if(auto *self=static_cast<Context *>(cmsGetContextUserData(c)))self->error=QString::fromUtf8(message?message:"").left(512);});}
    ~Context(){if(value)cmsDeleteContext(value);}
};
struct ProfileHandle {cmsHPROFILE value=nullptr;~ProfileHandle(){if(value)cmsCloseProfile(value);}};
struct TransformHandle {cmsHTRANSFORM value=nullptr;~TransformHandle(){if(value)cmsDeleteTransform(value);}};
QString digest(const QByteArray &bytes){return QString::fromLatin1(QCryptographicHash::hash(bytes,QCryptographicHash::Sha256).toHex());}
bool supported(cmsHPROFILE profile){const auto model=cmsGetColorSpace(profile);const auto kind=cmsGetDeviceClass(profile);return (model==cmsSigRgbData||model==cmsSigCmykData)&&(kind==cmsSigDisplayClass||kind==cmsSigOutputClass);}
}
QColorSpace SoftProof::workingColorSpace(){static const QColorSpace space(QColorSpace::Primaries::ProPhotoRgb,QColorSpace::TransferFunction::Gamma,1.8f);return space;}
SoftProof::Profile SoftProof::Profile::fromIcc(const QByteArray &icc,QString *error) {
    if(error)error->clear();const auto fail=[&](const QString &why){if(error)*error=why;return Profile{};};
    if(icc.isEmpty()||icc.size()>MaxProfileBytes)return fail("Proof ICC must be nonempty and no larger than4MiB");
    Context context;if(!context.value)return fail("Cannot create proof color context");
    ProfileHandle profile{cmsOpenProfileFromMemTHR(context.value,icc.constData(),cmsUInt32Number(icc.size()))};
    if(!profile.value||!supported(profile.value))return fail("Proof ICC must be an RGB/CMYK display or output profile");
    char description[512]{};cmsGetProfileInfoASCII(profile.value,cmsInfoDescription,"en","US",description,sizeof(description));description[sizeof(description)-1]=0;
    Profile result;result.m_icc=icc;result.m_description=QString::fromUtf8(description).simplified().left(160);
    if(result.m_description.isEmpty())result.m_description="ICC proof profile";
    result.m_key="soft-proof-icc-v1:"+digest(icc);result.m_model=cmsGetColorSpace(profile.value)==cmsSigCmykData?"CMYK":"RGB";return result;
}
SoftProof::Profile SoftProof::Profile::fromFile(const QString &path,QString *error) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly)){if(error)*error="Cannot read proof ICC: "+file.errorString();return {};}
    if(file.size()<1||file.size()>MaxProfileBytes){if(error)*error="Proof ICC must be nonempty and no larger than4MiB";return {};}
    const auto data=file.read(MaxProfileBytes+1);
    if(file.error()!=QFileDevice::NoError||!file.atEnd()||data.size()>MaxProfileBytes){if(error)*error="Cannot read bounded proof ICC snapshot";return {};}
    return fromIcc(data,error);
}
SoftProof::Profile SoftProof::Profile::fromOutputSpace(ColorManagement::OutputSpace space,QString *error){if(int(space)<0||int(space)>3){if(error)*error="Invalid proof output space";return {};}return fromIcc(ColorManagement::iccProfile(space),error);}
SoftProof::Result SoftProof::transform(const QImage &source,const Profile &proof,const Settings &settings,const CancelToken &cancel) {
    Result result;QElapsedTimer timer;timer.start();
    const auto fail=[&](const QString &why){result.image={};result.error=why;return result;};
    const auto stop=[&]{result.cancelled=true;return fail("Soft proof cancelled");};
    if(cancelled(cancel))return stop();
    if(source.isNull()||source.format()!=QImage::Format_RGBA64||qint64(source.width())*source.height()>MaxPixels
        ||source.colorSpace()!=workingColorSpace())return fail("Soft proof requires a bounded encoded ProPhoto RGBA16 working result");
    const auto intent=int(settings.intent);if(intent<0||intent>3||!proof.valid())return fail("Invalid proof profile or intent");
    Context context;if(!context.value)return fail("Cannot create proof color context");
    const auto inputBytes=workingColorSpace().iccProfile(),outputBytes=ColorManagement::iccProfile(ColorManagement::OutputSpace::SRgb);
    ProfileHandle input{cmsOpenProfileFromMemTHR(context.value,inputBytes.constData(),cmsUInt32Number(inputBytes.size()))};
    ProfileHandle output{cmsOpenProfileFromMemTHR(context.value,outputBytes.constData(),cmsUInt32Number(outputBytes.size()))};
    ProfileHandle target{cmsOpenProfileFromMemTHR(context.value,proof.icc().constData(),cmsUInt32Number(proof.icc().size()))};
    if(!input.value||!output.value||!target.value||!supported(target.value))return fail("Cannot open proof color profiles");
    if(!cmsIsIntentSupported(target.value,cmsUInt32Number(intent),LCMS_USED_AS_PROOF))return fail("Proof ICC does not support the selected rendering intent");
    std::array<cmsUInt16Number,cmsMAXCHANNELS> alarm{};alarm[0]=65535;alarm[2]=65535;cmsSetAlarmCodesTHR(context.value,alarm.data());
    cmsUInt32Number flags=cmsFLAGS_SOFTPROOFING|cmsFLAGS_COPY_ALPHA|cmsFLAGS_NOOPTIMIZE;
    if(settings.blackPointCompensation)flags|=cmsFLAGS_BLACKPOINTCOMPENSATION;
    if(settings.gamutWarning)flags|=cmsFLAGS_GAMUTCHECK;
    TransformHandle conversion{cmsCreateProofingTransformTHR(context.value,input.value,TYPE_RGBA_16,output.value,TYPE_RGBA_16,target.value,
        cmsUInt32Number(intent),settings.simulatePaper?INTENT_ABSOLUTE_COLORIMETRIC:INTENT_RELATIVE_COLORIMETRIC,flags)};
    if(!conversion.value)return fail(context.error.isEmpty()?"Cannot create proof color transform":context.error);
    result.image=QImage(source.size(),QImage::Format_RGBA64);if(result.image.isNull())return fail("Cannot allocate bounded proof frame");
    for(int y=0;y<source.height();++y){if(cancelled(cancel))return stop();
        cmsDoTransform(conversion.value,source.constScanLine(y),result.image.scanLine(y),cmsUInt32Number(source.width()));
        const auto *from=reinterpret_cast<const QRgba64 *>(source.constScanLine(y));auto *to=reinterpret_cast<QRgba64 *>(result.image.scanLine(y));
        for(int x=0;x<source.width();++x)to[x].setAlpha(from[x].alpha());
    }
    result.image.setColorSpace(ColorManagement::colorSpace(ColorManagement::OutputSpace::SRgb));
    result.facts={{"available",true},{"presentation_only",true},{"input","gamma1.8-prophoto-rgba16"},{"output","encoded-srgb-rgba16"},
        {"proof_profile_key",proof.key()},{"proof_profile_description",proof.description()},{"proof_profile_model",proof.colorModel()},
        {"intent",intent},{"simulate_paper",settings.simulatePaper},{"gamut_warning",settings.gamutWarning},{"black_point_compensation",settings.blackPointCompensation},
        {"pixels",qint64(source.width())*source.height()},{"alpha_exact",true},{"monitor_icc_applied",false},{"export_or_scopes_modified",false},{"elapsed_ms",timer.elapsed()}};
    return result;
}
