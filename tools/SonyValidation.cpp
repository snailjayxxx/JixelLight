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
#include "core/pipeline/StageGraph.h"
#include "core/commands/CommandRegistry.h"
#include "core/export/RasterExporter.h"
#include <QColorSpace>
#include <cmath>
#include <cstring>

QString fileHash(const QString &path) {QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);if(!h.addData(&f))return {};return h.result().toHex();}
bool writeJson(const QString &path,const QJsonObject &o){QSaveFile f(path);if(!f.open(QIODevice::WriteOnly))return false;const auto b=QJsonDocument(o).toJson();return f.write(b)==b.size()&&f.commit();}
QJsonObject portableLookCommandPixels(const QImage &source,const QVariantMap &metadata) {
 const auto recorded=metadata.value("sonyLook").toMap();
 if(source.isNull()||!recorded.value("autoEligible").toBool())return {{"passed",false},{"error","Eligible real Sony pixels/metadata required"}};
 const CommandRegistry::LookContext context{true,metadata};
 AdjustmentState base;base.exposure=.3;base.look.mode="as-shot";
 AdjustmentState recordedManual=base;recordedManual.look.mode="manual";recordedManual.look.code=recorded.value("code").toString();
 const auto parameters=recorded.value("parameters").toMap();
 for(auto i=parameters.cbegin();i!=parameters.cend();++i)recordedManual.look.parameters[i.key()]=i.value().toDouble();
 AdjustmentState manual=base;manual.look={};manual.look.mode="manual";manual.look.code="FL";
 auto strength=manual;strength.look.strength=.65;
 auto detail=strength;detail.look.parameters={{"sharpness",3},{"clarity",5},{"contrast",-4}};
 auto materialized=recordedManual;materialized.look.strength=.4;materialized.look.parameters["clarity"]=5;
 auto off=base;off.look.mode="off";
 auto reset=base;reset.look={};
 struct Case {QString name;QJsonArray commands;AdjustmentState expected;};
 const QVector<Case> cases{
  {"off",{QJsonObject{{"command","look.mode"},{"mode","off"}}},off},
  {"as_shot",{QJsonObject{{"command","look.mode"},{"mode","as_shot"}}},recordedManual},
  {"manual",{QJsonObject{{"command","look.code"},{"code","FL"}}},manual},
  {"strength",{QJsonObject{{"command","look.code"},{"code","FL"}},QJsonObject{{"command","look.strength"},{"value",.65}}},strength},
  {"detail",{QJsonObject{{"command","look.code"},{"code","FL"}},QJsonObject{{"command","look.strength"},{"value",.65}},
    QJsonObject{{"command","look.parameter"},{"parameter","sharpness"},{"value",3.2}},
    QJsonObject{{"command","look.parameter"},{"parameter","clarity"},{"value",4.6}},
    QJsonObject{{"command","look.parameter"},{"parameter","contrast"},{"value",-3.5}}},detail},
  {"materialized_parameter",{QJsonObject{{"command","look.mode"},{"mode","as_shot"}},QJsonObject{{"command","look.strength"},{"value",.4}},
    QJsonObject{{"command","look.parameter"},{"parameter","clarity"},{"value",4.6}}},materialized},
  {"reset",{QJsonObject{{"command","look.code"},{"code","FL"}},QJsonObject{{"command","look.reset"}}},reset}
 };
 QJsonArray results;bool passed=true;
 for(const auto &test:cases) {
  auto actual=base;QString error;
  const bool applied=CommandRegistry::executeBatch(actual,test.commands,&error,&context);
  const auto effective=LookProfiles::resolveAsShot(actual,metadata,true);
  const auto rendered=applied?ImagePipeline::process(source,effective,ImagePipeline::InputEncoding::LinearProPhoto):QImage{};
  const auto reference=ImagePipeline::process(source,test.expected,ImagePipeline::InputEncoding::LinearProPhoto);
  const bool exact=!rendered.isNull()&&rendered==reference;
  const bool values=effective.look.code==test.expected.look.code && effective.look.strength==test.expected.look.strength
    && effective.look.parameters==test.expected.look.parameters && effective.exposure==test.expected.exposure;
  const bool intent=test.name!="as_shot" || (actual.look.mode=="as-shot"&&actual.look.code.isEmpty()&&actual.look.parameters.isEmpty());
  passed=passed&&applied&&exact&&values&&intent;
  results.append(QJsonObject{{"case",test.name},{"applied",applied},{"error",error},{"exact_pixels",exact},
   {"effective_values_match",values},{"symbolic_intent_preserved",intent},{"pixels",qint64(rendered.width())*rendered.height()},
   {"output",StageGraph::outputFingerprint(rendered)},{"reference",StageGraph::outputFingerprint(reference)}});
 }
 auto rejected=base;const auto before=rejected.toJson();QString error;
 const bool atomic=!CommandRegistry::executeBatch(rejected,{QJsonObject{{"command","look.code"},{"code","FL"}},
   QJsonObject{{"command","look.mode"},{"mode","calibrated"}}},&error,&context)&&rejected.toJson()==before;
 const bool injection=!CommandRegistry::execute(rejected,{{"command","look.mode"},{"mode","manual"},{"lut",LookLut::identity(2)->toJson()}},&error,&context)&&rejected.toJson()==before;
 const CommandRegistry::LookContext jpeg{false,metadata};
 const bool jpegRejected=!CommandRegistry::execute(rejected,{{"command","look.mode"},{"mode","as_shot"}},&error,&jpeg)&&rejected.toJson()==before;
 return {{"passed",passed&&atomic&&injection&&jpegRejected},{"cases",results},{"invalid_batch_atomic",atomic},
  {"lut_injection_rejected",injection},{"jpeg_as_shot_rejected",jpegRejected},{"source","real decoded ARW; existing 1024-edge probe sample"}};
}
int comparePortablePng(const QString &first,const QString &second,const QString &reportPath) {
 const auto a=QImage(first).convertToFormat(QImage::Format_RGBA64),b=QImage(second).convertToFormat(QImage::Format_RGBA64);
 bool exact=!a.isNull()&&!b.isNull()&&a.size()==b.size();
 if(exact)for(int y=0;y<a.height();++y)if(std::memcmp(a.constScanLine(y),b.constScanLine(y),size_t(a.width())*8)!=0){exact=false;break;}
 const bool icc=!a.isNull()&&a.colorSpace().iccProfile()==b.colorSpace().iccProfile();
 const QJsonObject report{{"passed",exact&&icc},{"exact_pixels",exact},{"icc_equal",icc},
  {"pixels",qint64(a.width())*a.height()},{"reference",StageGraph::outputFingerprint(a)},{"actual",StageGraph::outputFingerprint(b)}};
 return writeJson(reportPath,report)?(exact&&icc?0:1):3;
}
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
 if(args.size()==5&&args[1]=="--compare-portable-png")return comparePortablePng(args[2],args[3],args[4]);
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
   report["portableLookCommands"]=portableLookCommandPixels(small,completeMeta);
   // Independent direct-state oracle for an actual CLI RAW decode/export. Crop
   // keeps only the published test output small; RAW decoding remains real.
   AdjustmentState commandReference;commandReference.exposure=.3;
   const auto recorded=completeMeta.value("sonyLook").toMap();
   commandReference.look.mode="manual";commandReference.look.code=recorded.value("code").toString();commandReference.look.strength=.4;
   const auto controls=recorded.value("parameters").toMap();
   for(auto i=controls.cbegin();i!=controls.cend();++i)commandReference.look.parameters[i.key()]=i.value().toDouble();
   commandReference.look.parameters["clarity"]=5;
   commandReference.geometry.crop={.1,.1,.04,.04};
   const auto oraclePath=dir+"/portable-cli-reference.png";
   const double rawBase=completeMeta.value("rawBaseExposureStops",0).toDouble();QString oracleError;
   const bool oracle=exportRaster(raw,commandReference,oraclePath,ColorManagement::OutputSpace::SRgb,
      RasterFormat::Png16,92,{},&oracleError,true,float(rawBase));
   report["portableCliReference"]=QJsonObject{{"saved",oracle},{"error",oracleError},{"file",QFileInfo(oraclePath).fileName()}};
   auto baseline=ImagePipeline::process(small,{},ImagePipeline::InputEncoding::LinearProPhoto);baseline.save(dir+"/baseline.png");
   ref.image.save(dir+"/reference.png");
   auto fit=calibrateLook({raw,ref.image,0,0,ref.info});report["fit"]=QJsonObject::fromVariantMap(fit.report);report["fitError"]=fit.error;report["fitAccepted"]=bool(fit.lut);
   if(fit.lut){AdjustmentState s;s.look.mode="calibrated";s.look.lut=fit.lut;ImagePipeline::process(small,s,ImagePipeline::InputEncoding::LinearProPhoto).save(dir+"/fitted.png");writeJson(dir+"/fitted.jlook.json",QJsonObject{{"format","JixelLightLook"},{"version",1},{"look",s.look.toJson()}});}
   AdjustmentState shot;shot.look.mode="as-shot";shot=LookProfiles::resolveAsShot(shot,meta,true);report["asShot"]=shot.look.toJson();ImagePipeline::process(small,shot,ImagePipeline::InputEncoding::LinearProPhoto).save(dir+"/as-shot.png");
 }
 if(!writeJson(dir+"/report.json",report))return 3;
 qInfo().noquote()<<QString::fromUtf8(QJsonDocument(report).toJson());return raw.isNull()||ref.image.isNull()?1:0;
}
