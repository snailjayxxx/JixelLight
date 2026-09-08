#include "core/look/LookProfiles.h"
#include "core/look/SonyLookMetadata.h"
#include <algorithm>
#include <cmath>

// Independent starting approximations, NOT Sony coefficients or calibrated profiles.
// Names/descriptions are identified using Sony manuals; numeric values are ours.
namespace {
struct Preset {const char *code;double contrast,saturation,exposure,shadows,highlights,fade;};
constexpr Preset presets[]{
 {"ST",0,0,0,0,0,0},{"PT",-4,-4,0,2,-8,0},{"NT",-6,-12,0,0,0,0},
 {"VV",8,18,0,0,0,0},{"VV2",4,24,.08,3,0,0},{"FL",14,-18,0,-2,0,.015},
 {"IN",-12,-20,0,2,-4,.05},{"SH",-10,5,.18,15,-8,.01},
 {"BW",4,0,0,0,0,0},{"SE",0,0,0,0,0,.02},
 {"FL2",10,-22,0,0,-4,.025},{"FL3",6,-8,.06,3,-3,.012}
};
const Preset *find(const QString &code){for(const auto &p:presets)if(code==QLatin1String(p.code))return &p;return nullptr;}
double parameter(const LookState &l,const QString &key,double fallback=0) {
 const auto range=SonyLookMetadata::parameterRange(key);const auto i=l.parameters.constFind(key);
 if(i==l.parameters.cend()||!std::isfinite(i.value())||i.value()<range.first||i.value()>range.second)return fallback;return i.value();
}
}
QVariantList LookProfiles::catalog(){QVariantList l;for(const auto &p:presets)l<<QVariantMap{{"code",p.code},{"calibration","approximation"}};return l;}
bool LookProfiles::active(const LookState &l){return l.mode!="off"&&l.error.isEmpty()&&l.strength>0&&(l.lut||find(l.code));}
AdjustmentState LookProfiles::resolveAsShot(AdjustmentState state,const QVariantMap &metadata,bool raw){
 if(state.look.mode!="as-shot")return state;
 state.look.code.clear();state.look.parameters.clear();state.look.lut.reset();
 const auto sony=metadata.value("sonyLook").toMap();
 if(raw&&sony.value("autoEligible").toBool()) {
  state.look.code=sony.value("code").toString();const auto params=sony.value("parameters").toMap();
  for(auto i=params.cbegin();i!=params.cend();++i)state.look.parameters[i.key()]=i.value().toDouble();
 }
 return state;
}
AdjustmentState LookProfiles::effective(const AdjustmentState &original){
 auto state=original;const auto &l=original.look;if(!active(l))return state;
 const double w=std::clamp(l.strength,0.,1.);const auto *p=l.lut?nullptr:find(l.code);
 if(p) {
  state.contrast+=w*p->contrast;state.saturation+=w*p->saturation;state.exposure+=w*p->exposure;
  state.shadows+=w*p->shadows;state.highlights+=w*p->highlights;
  if(l.code.startsWith("FL")){state.hslHue[3]-=w*8;state.hslSaturation[3]-=w*10;state.hslSaturation[5]+=w*8;}
  if(l.code=="PT"){state.hslLuminance[1]+=w*3;state.hslSaturation[0]-=w*4;}
  if(l.code=="FL3")state.temperature+=w*5;
 }
 state.contrast+=w*parameter(l,"contrast")*2.5;
 state.highlights+=w*parameter(l,"highlights")*5.5;state.shadows+=w*parameter(l,"shadows")*5.5;
 if(l.code!="BW"&&l.code!="SE")state.saturation+=w*parameter(l,"saturation")*4;
 return state;
}
std::array<float,4> LookProfiles::style(const LookState &l){
 if(!active(l))return {};const float w=float(std::clamp(l.strength,0.,1.));const auto *p=l.lut?nullptr:find(l.code);
 return {p&&(l.code=="BW"||l.code=="SE")?w:0,p&&l.code=="SE"?w:0,
         std::clamp(float((p?p->fade:0)+parameter(l,"fade")*.012)*w,0.f,.25f),l.lut?1.f:0.f};
}
std::array<float,4> LookProfiles::detail(const LookState &l){
 if(!active(l))return {};const float w=float(std::clamp(l.strength,0.,1.));
 return {float(parameter(l,"sharpness")*.075)*w,float(parameter(l,"clarity")*.045)*w,
         float(6-parameter(l,"sharpnessRange",3)),12};
}
