#include "core/look/LookCalibration.h"
#include "core/raw/RawGeometry.h"
#include "core/pipeline/ImagePipeline.h"
#include "diagnostics/PerformanceRecorder.h"
#include <QRgba64>
#include <QDateTime>
#include <QSet>
#include <algorithm>
#include <cmath>
#include <vector>

namespace {
struct Sample {std::array<float,3> src{},dst{};std::array<int,8> nodes{};std::array<float,8> weights{};bool holdout=false;int scene=0;};
std::array<float,3> pixel(const QImage &im,int x,int y){auto p=reinterpret_cast<const QRgba64*>(im.constScanLine(y))[x];return {p.red()/65535.f,p.green()/65535.f,p.blue()/65535.f};}
float gray(std::array<float,3> c){return .2126f*c[0]+.7152f*c[1]+.0722f*c[2];}
double rmse(const LookLut *lut,const std::vector<Sample> &samples,int scene=-1){
 double sum=0;size_t count=0;for(const auto &s:samples)if(s.holdout&&(scene<0||scene==s.scene)){auto a=lut?lut->sample(s.src[0],s.src[1],s.src[2]):s.src;
  for(int c=0;c<3;++c){const double d=a[c]-s.dst[c];sum+=d*d;++count;}}
 return count?std::sqrt(sum/count):1;
}
}
static LookCalibrationResult fitPairs(const QVector<LookCalibrationPair> &pairs,const CancelToken &cancel,bool dataset){
 LookCalibrationResult out;
 const auto reject=[&](const QString &why){out.error=why;return out;};
 if(pairs.isEmpty()||pairs.size()>12)return reject("Expected 1..12 image pairs");
 if(dataset){int train=0,validation=0;QSet<QString> ids;
  for(const auto &p:pairs){if(p.id.isEmpty()||ids.contains(p.id))return reject("Each scene needs a unique identity; no train/validation overlap");ids.insert(p.id);p.validation?++validation:++train;}
  if(train<2||validation<1)return reject("Multi-scene fitting requires at least two training scenes and one independent validation scene");
 }
 constexpr int n=17,nodes=n*n*n;auto lut=std::make_shared<LookLut>(*LookLut::identity(n));
 lut->title=dataset?"Empirical multi-scene match":"Image-specific reference match";
 std::vector<Sample> samples;
 std::vector<float> mass(nodes,0);std::vector<std::array<float,3>> gradient(nodes);
 QVariantList sceneReports;double minimumAlignment=1;
 for(int scene=0;scene<pairs.size();++scene){
  if(cancelled(cancel))return reject("Cancelled");
  const auto &pair=pairs[scene];const auto &baseline=pair.baseline;const auto &reference=pair.reference;
  if(baseline.isNull()||reference.isNull())return reject("Missing source/reference image");
  const double ratioA=double(baseline.width())/baseline.height(),ratioB=double(reference.width())/reference.height();
  if(std::abs(ratioA/ratioB-1)>.015)return reject("Aspect ratios differ; use an uncropped, correctly oriented reference of the same shot");
  const QSize limit=baseline.size().boundedTo(reference.size()).boundedTo(QSize(256,256));
  const QSize size=baseline.size().scaled(limit,Qt::KeepAspectRatio);
  if(size.width()<32||size.height()<32)return reject("Reference is too small for calibration");
  const QImage a=baseline.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA64);
  const QImage b=reference.scaled(size,Qt::IgnoreAspectRatio,Qt::SmoothTransformation).convertToFormat(QImage::Format_RGBA64);
  double cross=0,aa=0,bb=0;
  for(int y=2;y<size.height()-2;y+=2)for(int x=2;x<size.width()-2;x+=2){
   const double ax=gray(pixel(a,x+2,y))-gray(pixel(a,x-2,y)),ay=gray(pixel(a,x,y+2))-gray(pixel(a,x,y-2));
   const double bx=gray(pixel(b,x+2,y))-gray(pixel(b,x-2,y)),by=gray(pixel(b,x,y+2))-gray(pixel(b,x,y-2));
   cross+=ax*bx+ay*by;aa+=ax*ax+ay*ay;bb+=bx*bx+by*by;
  }
  const double alignment=cross/std::sqrt(std::max(1e-20,aa*bb));minimumAlignment=std::min(minimumAlignment,alignment);
  out.report["gradientAlignment"]=minimumAlignment;
  if(aa<1e-6||bb<1e-6||alignment<.72)return reject("Reference alignment/texture check failed for scene "+pair.id);
  sceneReports<<QVariantMap{{"id",pair.id},{"role",pair.validation?"independent-validation":"training-with-spatial-holdout"},{"gradientAlignment",alignment}};
  for(int y=1;y<size.height()-1;++y)for(int x=1;x<size.width()-1;++x){
   if(cancelled(cancel))return reject("Cancelled");Sample sample;sample.src=pixel(a,x,y);sample.dst=pixel(b,x,y);sample.scene=scene;
   // Validation scenes contribute NO optimizer samples. Training images also
   // reserve spatial blocks: evaluating only fitted pixels would hide overfit.
   sample.holdout=pair.validation||((x/16)+3*(y/16))%5==0;
   std::array<int,3> base{};std::array<float,3> f{};
   for(int c=0;c<3;++c){float q=sample.src[c]*(n-1);base[c]=std::min(int(q),n-2);f[c]=q-base[c];}
   int j=0;for(int z=0;z<2;++z)for(int g=0;g<2;++g)for(int r=0;r<2;++r){
    const int index=(base[2]+z)*n*n+(base[1]+g)*n+base[0]+r;
    const float w=(r?f[0]:1-f[0])*(g?f[1]:1-f[1])*(z?f[2]:1-f[2]);
    sample.nodes[j]=index;sample.weights[j++]=w;if(!sample.holdout)mass[index]+=w;
   }
   samples.push_back(sample);
  }
 }
 const auto initial=lut->rgb;const double before=rmse(nullptr,samples);
 // Damped Jacobi/preconditioned least-squares updates; smooth residuals, with
 // identity anchoring for unobserved colors. No ML runtime or model download.
 for(int iteration=0;iteration<70;++iteration){
  if(cancelled(cancel))return reject("Cancelled");std::fill(gradient.begin(),gradient.end(),std::array<float,3>{});
  for(const auto &s:samples)if(!s.holdout){std::array<float,3> p{};
   for(int j=0;j<8;++j)for(int c=0;c<3;++c)p[c]+=lut->rgb[s.nodes[j]*3+c]*s.weights[j];
   for(int j=0;j<8;++j)for(int c=0;c<3;++c)gradient[s.nodes[j]][c]+=s.weights[j]*(s.dst[c]-p[c]);
  }
  QVector<float> next=lut->rgb;
  for(int z=0;z<n;++z)for(int g=0;g<n;++g)for(int r=0;r<n;++r){const int i=z*n*n+g*n+r;
   const int neighbors[]{r>0?i-1:i,r<n-1?i+1:i,g>0?i-n:i,g<n-1?i+n:i,z>0?i-n*n:i,z<n-1?i+n*n:i};
   for(int c=0;c<3;++c){const float delta=lut->rgb[i*3+c]-initial[i*3+c];float neighborDelta=0;
    for(int other:neighbors)neighborDelta+=lut->rgb[other*3+c]-initial[other*3+c];
    const float regularizer=.35f,anchor=.025f;
    const float step=(gradient[i][c]+regularizer*(neighborDelta-6*delta)-anchor*delta)/(mass[i]+6*regularizer+anchor);
    next[i*3+c]=std::clamp(lut->rgb[i*3+c]+.7f*step,0.f,1.f);
   }
  }
  lut->rgb=std::move(next);
 }
 const double after=rmse(lut.get(),samples);int covered=0;for(float v:mass)covered+=v>1;
 out.report["heldoutRmseBefore"]=before;out.report["heldoutRmseAfter"]=after;
 out.report["coveredNodeFraction"]=double(covered)/nodes;out.report["samples"]=int(samples.size());out.report["gridSize"]=n;
 out.report["scope"]=dataset?"multi-scene-empirical-not-universal-camera-characterization":"this-image-only-not-camera-characterization";out.report["colorDomain"]="display-srgb";
 bool independentPassed=true;
 for(int i=0;i<pairs.size();++i){auto m=sceneReports[i].toMap();
  const double prior=rmse(nullptr,samples,i),current=rmse(lut.get(),samples,i);
  m["heldoutRmseBefore"]=prior;m["heldoutRmseAfter"]=current;
  const bool passed=std::isfinite(current)&&current<=.05&&current<=prior+.003&&(prior<=.003||current<prior*.98);
  m["qualityGatePassed"]=passed;sceneReports[i]=m;
  if(pairs[i].validation&&!passed)independentPassed=false;
 }
 out.report["scenes"]=sceneReports;out.report["cameraCalibration"]=false;
 out.report["independentValidationPassed"]=dataset&&independentPassed;
 out.report["qualityGate"]=dataset?"each-independent-scene-RMSE<=0.05-and-improvement":"image-spatial-holdout";
 if(dataset&&!independentPassed)return reject("Independent scene validation failed; no reusable profile was created");
 if(!std::isfinite(after)||after>.15||after>before+.003||(before>.003&&after>=before*.98))return reject("Held-out validation did not improve sufficiently; no profile was applied");
 lut->evidence=QJsonObject::fromVariantMap(out.report);lut->evidence["kind"]=dataset?"multi-scene-empirical-fit":"image-specific-fit";
 lut->updateDigest();out.lut=lut;return out;
}
LookCalibrationResult fitLookImages(const QImage &baseline,const QImage &reference,const CancelToken &cancel){
 return fitPairs({{baseline,reference,"current-image",false}},cancel,false);
}
LookCalibrationResult fitLookDataset(const QVector<LookCalibrationPair> &pairs,const CancelToken &cancel){
 return fitPairs(pairs,cancel,true);
}
QImage calibrationSource(const QImage &linearSource,const QImage &reference,QVariantMap *geometry){
 const auto crop=RawGeometry::parseCrop(linearSource.text("JixelLightCameraCrop"),linearSource.size());
 const double referenceAspect=double(reference.width())/std::max(1,reference.height());
 const bool cropped=crop.isValid()&&std::abs(double(crop.width())/crop.height()/referenceAspect-1)<.001;
 if(geometry){(*geometry)["geometryBasis"]=cropped?"camera-metadata-default-crop":"full-developed-frame";
  if(cropped)(*geometry)["sourceCrop"]=QVariantList{crop.x(),crop.y(),crop.width(),crop.height()};
  (*geometry)["developedWidth"]=linearSource.width();(*geometry)["developedHeight"]=linearSource.height();}
 return cropped?linearSource.copy(crop):linearSource;
}
LookCalibrationResult calibrateLook(const LookCalibrationRequest &request,const CancelToken &cancel){
 PerformanceSpan timer("look_reference_fit");
 try{
  if(request.linearSource.isNull()||cancelled(cancel))return {{}, {}, "Missing RAW or cancelled"};
  // Match the unedited baseline; subsequent user edits are independent from the LUT.
  QVariantMap geometry;const QImage source=calibrationSource(request.linearSource,request.reference,&geometry);
  const QImage small=source.scaled(512,512,Qt::KeepAspectRatio,Qt::SmoothTransformation);
  const auto baseline=ImagePipeline::process(small,{},ImagePipeline::InputEncoding::LinearProPhoto,ColorManagement::OutputSpace::SRgb,cancel);
  auto out=fitLookImages(baseline,request.reference,cancel);
  for(auto i=geometry.cbegin();i!=geometry.cend();++i)out.report[i.key()]=i.value();
  if(out.lut){auto l=std::make_shared<LookLut>(*out.lut);l->evidence=QJsonObject::fromVariantMap(out.report);l->evidence["kind"]="image-specific-fit";l->evidence["provenance"]=QJsonObject::fromVariantMap(request.provenance);
    l->evidence["createdUtc"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODate);out.lut=l;}
  return out;
 }catch(const std::exception &e){return {{},{},QString::fromUtf8(e.what())};}catch(...){return {{},{},"Calibration failed"};}
}
