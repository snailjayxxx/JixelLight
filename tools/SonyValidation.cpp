#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QSaveFile>
#include <QJsonArray>
#include <QSet>
#include <QTemporaryDir>
#include "core/cache/SourceCache.h"
#include "core/pipeline/ProcessingPlan.h"
#include "core/metadata/MetadataReader.h"
#include "core/look/CameraReference.h"
#include "core/look/LookCalibration.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include "core/pipeline/ImagePipeline.h"

QString fileHash(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return h.result().toHex();}
bool writeJson(const QString &path,const QJsonObject &o){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;const auto b=QJsonDocument(o).toJson();return f.write(b)==b.size()&&f.commit();}
int fitDatasetFile(const QString &manifestPath,const QString &directory) {
 if(QFileInfo::exists(directory+"/empirical.jlook.json")){qCritical("Choose a new output directory; empirical profile already exists");return 3;}
 QFile manifest(manifestPath);if(!manifest.open(QIODevice::ReadOnly)||manifest.size()>65536)return 2;
 QJsonParseError parse;const auto doc=QJsonDocument::fromJson(manifest.readAll(),&parse);
 const auto items=doc.object()["pairs"].toArray();if(parse.error!=QJsonParseError::NoError||items.size()<3||items.size()>12)return 2;
 const QDir base=QFileInfo(manifestPath).absoluteDir();QVector<LookCalibrationPair> pairs;QVariantList evidence;
 QByteArray group;QSet<QString> rawHashes,jpegHashes,captures;QString error;QString code;
 for(const auto &value:items){const auto item=value.toObject();
  const auto rawPath=base.absoluteFilePath(item["raw"].toString()),jpgPath=base.absoluteFilePath(item["jpeg"].toString());
  if(!RawDecoder::isRawFile(rawPath)){error="Dataset source is not RAW";break;}
  const QString rh=fileHash(rawPath),jh=fileHash(jpgPath);
  if(rh.isEmpty()||jh.isEmpty()||rawHashes.contains(rh)||jpegHashes.contains(jh)){error="Missing or duplicate train/validation files";break;}
  rawHashes.insert(rh);jpegHashes.insert(jh);
  const auto meta=MetadataReader::read(rawPath),jm=MetadataReader::read(jpgPath);QString why;
  if(!CameraReference::matches(meta,jm,&why)){error="Unverified pair: "+why;break;}
  const auto recorded=meta["sonyLook"].toMap();
  const QString capture=meta["model"].toString()+"|"+meta["captureTime"].toString()+"|"+meta["subSecTime"].toString();
  if(captures.contains(capture)){error="Duplicate capture identity across scenes";break;}captures.insert(capture);
  if(!recorded.value("autoEligible").toBool()||recorded.value("parameters").toMap().size()!=8){error="Dataset requires recognized Creative Look and all eight recorded controls";break;}
  const auto key=QJsonDocument(QJsonObject{{"model",meta["model"].toString()},{"code",recorded["code"].toString()},{"fine",QJsonObject::fromVariantMap(recorded["parameters"].toMap())}}).toJson(QJsonDocument::Compact);
  if(!group.isEmpty()&&group!=key){error="Do not mix camera models, looks or recorded fine adjustments";break;}group=key;code=recorded["code"].toString();
  const QString role=item["role"].toString();if(role!="train"&&role!="validation"){error="Each pair needs an explicit train or validation role";break;}
  auto source=RawDecoder::decode(rawPath,&error);if(source.isNull())break;
  auto ref=CameraReference::load({rawPath,jpgPath,meta,1});if(ref.image.isNull()){error=ref.error;break;}
  QVariantMap geometry;auto small=calibrationSource(source,ref.image,&geometry).scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  auto baseline=ImagePipeline::process(small,{},ImagePipeline::InputEncoding::LinearProPhoto);
  if(fileHash(rawPath)!=rh||fileHash(jpgPath)!=jh){error="Source changed during dataset read";break;}
  pairs.push_back({baseline,ref.image,item["id"].toString(),role=="validation"});
  evidence<<QVariantMap{{"id",item["id"].toString()},{"rawSha256",rh},{"jpegSha256",jh},{"role",role},{"geometry",geometry}};
 }
 LookCalibrationResult fit;if(error.isEmpty())fit=fitLookDataset(pairs);else fit.error=error;
 QJsonObject report{{"schema",1},{"engineVersion",ProcessingPlan::EngineVersion},{"commit",JIXELLIGHT_GIT_COMMIT},{"accepted",bool(fit.lut)},{"error",fit.error},{"metrics",QJsonObject::fromVariantMap(fit.report)},{"inputs",QJsonArray::fromVariantList(evidence)},{"group",QString::fromUtf8(group)}};
 if(!QDir().mkpath(directory)||!writeJson(directory+"/dataset-report.json",report))return 3;

 if(fit.lut){auto lut=std::make_shared<LookLut>(*fit.lut);lut->evidence["engineVersion"]=ProcessingPlan::EngineVersion;lut->evidence["inputs"]=QJsonArray::fromVariantList(evidence);lut->evidence["cameraGroup"]=QJsonDocument::fromJson(group).object();
  LookState look;look.mode="calibrated";look.code=code;look.lut=lut;
  if(!writeJson(directory+"/empirical.jlook.json",QJsonObject{{"format","JixelLightLook"},{"version",1},{"look",look.toJson()},{"notice","Validated on the listed held-out scenes only; not a universal or official Sony camera profile"}}))return 3;
 }
 qInfo().noquote()<<QString::fromUtf8(QJsonDocument(report).toJson());return fit.lut?0:1;
}
int main(int argc,char **argv){
 QCoreApplication app(argc,argv);const auto args=app.arguments();
 if(args.size()==4&&args[1]=="--dataset")return fitDatasetFile(args[2],args[3]);
 if(args.size()!=4){qCritical("Usage: JixelLightSonyValidation RAW JPEG output-directory");return 2;}
 const QString rawPath=QFileInfo(args[1]).absoluteFilePath(),jpegPath=QFileInfo(args[2]).absoluteFilePath(),dir=args[3];
 if(!QDir().mkpath(dir))return 2;QJsonObject report{{"schema",1},{"commit",JIXELLIGHT_GIT_COMMIT},{"rawSha256",fileHash(rawPath)},{"jpegSha256",fileHash(jpegPath)}};
 QString error;auto meta=MetadataReader::read(rawPath,&error);report["rawMetadata"]=QJsonObject::fromVariantMap(meta);report["metadataError"]=error;
 auto refMeta=MetadataReader::read(jpegPath,&error);report["jpegMetadata"]=QJsonObject::fromVariantMap(refMeta);report["jpegMetadataError"]=error;
 QString reason;report["pairMetadataMatches"]=CameraReference::matches(meta,refMeta,&reason);report["pairReason"]=reason;
 QElapsedTimer timer;timer.start();QTemporaryDir cacheDirectory;SourceCache cache(600LL*1024*1024,cacheDirectory.path());auto loaded=loadSource(cache,rawPath,{});auto raw=loaded.image;error=loaded.error;report["decodeMs"]=qint64(timer.elapsed());report["decodeError"]=error;report["decoderModel"]=loaded.metadata["model"].toString();report["cameraDefaultCrop"]=raw.text("JixelLightCameraCrop");report["width"]=raw.width();report["height"]=raw.height();
 auto ref=CameraReference::load({rawPath,jpegPath,meta,1});report["reference"]=QJsonObject::fromVariantMap(ref.info);report["referenceError"]=ref.error;
 report["referencePixelCount"]=qint64(ref.scopes.pixelCount);
 auto completeMeta=loaded.metadata;
 const auto automatic=CameraReference::load({rawPath,{},completeMeta,1});report["automaticReference"]=QJsonObject::fromVariantMap(automatic.info);
 if(!raw.isNull()&&!ref.image.isNull()){
   auto small=raw.scaled(1024,1024,Qt::KeepAspectRatio,Qt::SmoothTransformation);
   auto baseline=ImagePipeline::process(small,{},ImagePipeline::InputEncoding::LinearProPhoto);baseline.save(dir+"/baseline.png");
   ref.image.save(dir+"/reference.png");
   auto fit=calibrateLook({raw,ref.image,0,0,ref.info});report["fit"]=QJsonObject::fromVariantMap(fit.report);report["fitError"]=fit.error;report["fitAccepted"]=bool(fit.lut);
   if(fit.lut){AdjustmentState s;s.look.mode="calibrated";s.look.lut=fit.lut;ImagePipeline::process(small,s,ImagePipeline::InputEncoding::LinearProPhoto).save(dir+"/fitted.png");writeJson(dir+"/fitted.jlook.json",QJsonObject{{"format","JixelLightLook"},{"version",1},{"look",s.look.toJson()}});}
   AdjustmentState shot;shot.look.mode="as-shot";shot=LookProfiles::resolveAsShot(shot,meta,true);report["asShot"]=shot.look.toJson();ImagePipeline::process(small,shot,ImagePipeline::InputEncoding::LinearProPhoto).save(dir+"/as-shot.png");
 }
 if(!writeJson(dir+"/report.json",report))return 3;
 qInfo().noquote()<<QString::fromUtf8(QJsonDocument(report).toJson());return raw.isNull()||ref.image.isNull()?1:0;
}
