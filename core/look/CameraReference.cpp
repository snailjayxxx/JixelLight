#include "core/look/CameraReference.h"
#include "core/metadata/MetadataReader.h"
#include "core/raw/RawDecoder.h"
#include "core/cache/SourceCache.h"
#include <QCryptographicHash>
#include "diagnostics/PerformanceRecorder.h"
#include <QColorSpace>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QTransform>
#include <cmath>

bool CameraReference::matches(const QVariantMap &raw,const QVariantMap &jpeg,QString *reason){
 const auto no=[&](const char *why){if(reason)*reason=why;return false;};
 for(const char *key:{"make","model","captureTime"}) {
  if(raw.value(key).toString().isEmpty()||jpeg.value(key).toString().isEmpty())return no("missing-camera-or-time");
  if(raw.value(key).toString().compare(jpeg.value(key).toString(),Qt::CaseInsensitive)!=0)return no("camera-or-time-mismatch");
 }
 // Same second alone is insufficient; available capture fields must also agree.
 for(const char *key:{"subSecTime","shutter","aperture","iso","focalLength","imageUniqueId"}) {
  if(raw.contains(key)&&jpeg.contains(key)&&raw.value(key).toString()!=jpeg.value(key).toString())return no("capture-fields-mismatch");
 }
 const QString software=jpeg.value("software").toString().toLower();
 for(const char *editor:{"adobe","lightroom","photoshop","jixellight","capture one","darktable","dxo","rawtherapee"})
  if(software.contains(editor))return no("edited-jpeg");
 const auto a=raw.value("sonyLook").toMap(),b=jpeg.value("sonyLook").toMap();
 if(!a.value("code").toString().isEmpty()&&!b.value("code").toString().isEmpty()&&a.value("code")!=b.value("code"))return no("look-mismatch");
 const auto pa=a.value("parameters").toMap(),pb=b.value("parameters").toMap();
 for(auto i=pa.begin();i!=pa.end();++i)if(pb.contains(i.key())&&pb[i.key()]!=i.value())return no("look-parameters-mismatch");
 if(reason)*reason="matching-metadata-not-authentication";return true;
}
namespace {
QImage readJpeg(const QString &path,QVariantMap &info,const QVariantMap &meta){
 QImageReader reader(path);reader.setAutoTransform(true);
 if(!reader.canRead()||reader.format().toLower()!="jpeg")return {};
 const QSize size=reader.size();if(!size.isValid()||qint64(size.width())*size.height()>150000000)return {};
 reader.setScaledSize(size.scaled(size.boundedTo(QSize(2048,2048)),Qt::KeepAspectRatio));
 QImage image=reader.read();if(image.isNull())return {};
 if(image.colorSpace().isValid()) {image=image.convertedToColorSpace(QColorSpace::SRgb);info["colorBasis"]="embedded-ICC";}
 else {image.setColorSpace(QColorSpace::SRgb);info["colorBasis"]=meta.value("colorSpace").toString().contains("sRGB",Qt::CaseInsensitive)?"EXIF-sRGB":"assumed-sRGB";}
 info["originalWidth"]=size.width();info["originalHeight"]=size.height();return image;
}
QImage orient(QImage image,int code){
 switch(code){case 2:return image.mirrored(true,false);case 3:return image.transformed(QTransform().rotate(180));
 case 4:return image.mirrored(false,true);case 5:return image.transformed(QTransform().rotate(90)).mirrored(true,false);
 case 6:return image.transformed(QTransform().rotate(90));case 7:return image.transformed(QTransform().rotate(90)).mirrored(false,true);
 case 8:return image.transformed(QTransform().rotate(270));default:return image;}
}
}
CameraReferenceResult CameraReference::load(const CameraReferenceRequest &request,const CancelToken &cancel){
 CameraReferenceResult out;PerformanceSpan timer("camera_reference_load");
 try {
  if(cancelled(cancel))return out;
  QVariantMap metadata=request.metadata;if(metadata.isEmpty())metadata=MetadataReader::read(request.rawPath);
  QString selected=request.manualPath;QVariantMap refMeta;QStringList rejected;
  if(!selected.isEmpty()) {refMeta=MetadataReader::read(selected);out.info["kind"]="manual-reference";out.info["pairStatus"]="user-selected-not-authenticated";}
  else if(RawDecoder::isRawFile(request.rawPath)) {
   const QFileInfo raw(request.rawPath);QStringList matched;
   for(const auto &file:QDir(raw.absolutePath()).entryInfoList(QDir::Files|QDir::Readable)) {
    if(cancelled(cancel))return {};
    if(file.completeBaseName().compare(raw.completeBaseName(),Qt::CaseInsensitive)!=0)continue;
    if(file.suffix().compare("jpg",Qt::CaseInsensitive)!=0&&file.suffix().compare("jpeg",Qt::CaseInsensitive)!=0)continue;
    const auto meta=MetadataReader::read(file.absoluteFilePath());QString why;
    if(matches(metadata,meta,&why)){matched<<file.absoluteFilePath();refMeta=meta;}
    else rejected<<file.fileName()+": "+why;
   }
   if(matched.size()==1){selected=matched.first();out.info["kind"]="paired-jpeg";out.info["pairStatus"]="metadata-match-not-authentication";}
   else if(matched.size()>1)rejected<<"ambiguous-companion-JPEGs";
  }
  if(!selected.isEmpty()) {
   const auto identity=SourceCache::fileKey(selected);
   out.image=readJpeg(selected,out.info,refMeta);out.info["file"]=selected;
   out.info["fileIdentity"]=identity;
   if(identity!=SourceCache::fileKey(selected)){out.image={};out.error="Reference changed while reading; reload it";}
   if(out.image.isNull()&&out.error.isEmpty())out.error="The reference is not a readable bounded JPEG";
  }
  if(out.image.isNull() && request.manualPath.isEmpty() && RawDecoder::isRawFile(request.rawPath)) {
   if(!out.error.isEmpty())rejected<<QFileInfo(selected).fileName()+": "+out.error;
   out.error.clear();out.info.remove("file");out.info.remove("fileIdentity");
   out.image=RawDecoder::thumbnail(request.rawPath,cancel);out.info["kind"]="embedded-preview";
   out.info["pairStatus"]="embedded-in-source-RAW";out.info["colorBasis"]="assumed-sRGB";
   if(out.image.text("JixelLightThumbnailOrientationApplied")!="true")out.image=orient(out.image,metadata.value("orientationCode",1).toInt());
   if(out.image.colorSpace().isValid()){out.image=out.image.convertedToColorSpace(QColorSpace::SRgb);out.info["colorBasis"]="embedded-ICC";}
   else out.image.setColorSpace(QColorSpace::SRgb);
   if(out.image.isNull())out.error="No usable camera preview or verified companion JPEG";
  } else if(out.image.isNull()&&out.error.isEmpty())out.error="Camera reference is available for RAW files; choose a reference JPEG manually";
  if(cancelled(cancel))return {};
  out.info["rejectedCandidates"]=rejected;out.info["width"]=out.image.width();out.info["height"]=out.image.height();
  out.info["histogramSource"]="reference-preview-pixels-only";
  if(!out.image.isNull()) {
   const auto pixels=out.image.convertToFormat(QImage::Format_RGBA64);
   QCryptographicHash hash(QCryptographicHash::Sha256);
   hash.addData(QByteArray::number(pixels.width())+":"+QByteArray::number(pixels.height())+":RGBA64:");
   hash.addData(QByteArrayView(reinterpret_cast<const char*>(pixels.constBits()),pixels.sizeInBytes()));
   out.info["previewPixelSha256"]=QString::fromLatin1(hash.result().toHex());
  }
  if(!out.image.isNull())out.scopes=ScopesEngine::analyze(out.image,1024,cancel);
 }catch(const std::exception &e){out.error=QString::fromUtf8(e.what());}catch(...){out.error="Camera reference load failed";}
 return out;
}
