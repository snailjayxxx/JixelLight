#include "core/look/SonyLookMetadata.h"
#include <QSet>
#include <QStringList>
#include <array>

namespace {
struct Field { const char *name; uint16_t tag; int low; int high; };
constexpr std::array<Field,8> fields{{
    {"contrast",0x2004,-9,9},{"highlights",0x2033,-9,9},{"shadows",0x2032,-9,9},
    {"fade",0x2034,0,9},{"saturation",0x2005,-9,9},{"sharpness",0x2006,0,9},
    {"sharpnessRange",0x2035,1,5},{"clarity",0x2036,0,9}
}};
QString standardValue(const Exiv2::ExifData &exif,const char *key) {
    try { auto i=exif.findKey(Exiv2::ExifKey(key)); return i==exif.end()?QString():QString::fromStdString(i->toString()).trimmed(); }
    catch (...) { return {}; }
}
QString colorMode(const QString &raw) {
    bool ok=false; const auto n=raw.toULongLong(&ok); if(!ok) return {};
    switch(n) {
    case 0:return "ST";case 1:return "VV";case 2:return "PT";case 6:return "BW";
    case 12:case 100:return "NT";case 17:return "SE";case 18:return "FL";
    case 19:return "VV2";case 20:return "IN";case 21:return "SH";case 22:return "FL2";case 23:return "FL3";
    default:return {};
    }
}
}
QString SonyLookMetadata::normalize(const QString &name) {
    QString n=name.trimmed().toUpper(); n.remove(QChar('\0'));
    const QMap<QString,QString> aliases{{"STANDARD","ST"},{"PORTRAIT","PT"},{"NEUTRAL","NT"},
        {"VIVID","VV"},{"VIVID 2","VV2"},{"B&W","BW"},{"BLACK & WHITE","BW"},{"SEPIA","SE"}};
    if(aliases.contains(n)) return aliases[n];
    const QStringList codes{"ST","PT","NT","VV","VV2","FL","FL2","FL3","IN","SH","BW","SE"};
    return codes.contains(n)?n:QString();
}
QStringList SonyLookMetadata::parameterNames() { QStringList out;for(const auto &f:fields)out<<f.name;return out; }
QPair<int,int> SonyLookMetadata::parameterRange(const QString &name) {
    for(const auto &f:fields) if(name==QLatin1String(f.name))return {f.low,f.high};
    return {0,0};
}
QVariantMap SonyLookMetadata::read(const Exiv2::ExifData &exif) {
    QVariantMap out{{"schema",1},{"status","not-sony"},{"generation","unknown"},
        {"code",QString()},{"autoEligible",false},{"customSlotStatus","not-recorded"}};
    const QString make=standardValue(exif,"Exif.Image.Make");
    const QString recordedModel=standardValue(exif,"Exif.Image.Model");
    QString model=recordedModel;
    if(!make.contains("SONY",Qt::CaseInsensitive))return out;
    out["status"]="missing";
    // Preserve the EXIF text; preproduction files can use MODEL-NAME while
    // the MakerNote contains an authoritative numeric SonyModelID.
    QString idModel; int modelId=-1; bool modelConflict=false;
    for(const auto &item:exif) {
        const auto group=item.groupName();
        if((group!="Sony1"&&group!="Sony2")||item.tag()!=0xb001||item.count()!=1)continue;
        bool ok=false;const int id=QString::fromStdString(item.toString()).toInt(&ok);
        if(!ok||id<0||id>65535)continue;
        if(modelId>=0&&modelId!=id)modelConflict=true;
        modelId=id;
        QString printed=QString::fromStdString(item.print(&exif)).trimmed();
        // ExifTool SonyModelID 388 = ILCE-7M4; older Exiv2 may only print 388.
        if(id==388)printed="ILCE-7M4";
        for(const char *prefix:{"ILCE-","ILME-","DSC-","ZV-","NEX-","SLT-","DSLR-"})
            if(printed.startsWith(QLatin1String(prefix))&&!printed.contains('/'))idModel=printed;
    }
    const bool placeholder=recordedModel.isEmpty()||recordedModel.compare("MODEL-NAME",Qt::CaseInsensitive)==0;
    if(!idModel.isEmpty()) {
        if(!placeholder&&recordedModel.compare(idModel,Qt::CaseInsensitive)!=0)modelConflict=true;
        else model=idModel;
    }
    if(modelId>=0)out["sonyModelId"]=modelId;
    out["recordedModel"]=recordedModel;out["model"]=model;
    out["modelSource"]=placeholder&&!idModel.isEmpty()?"SonyModelID":"Exif.Image.Model";
    out["modelConflict"]=modelConflict;
    QVariantMap raw,values,sources,invalid;QVariantList candidates;QStringList warnings;
    QString primary,secondary;bool conflict=false,lookFields=false,legacyEvidence=false,unknownStyle=false;
    // Numeric tag IDs also work with Exiv2 versions that expose new tags as 0x2032.
    // Sony1/Sony2 are alternative IFDs; do not mix Minolta camera-setting encodings.
    for(const auto &item:exif) {
        const auto group=item.groupName();
        if(group.rfind("Sony",0)==0 && group!="Sony1" && group!="Sony2") {
            if(item.tagName()=="CreativeStyle")legacyEvidence=true;
        }
        if(group!="Sony1" && group!="Sony2")continue;
        const uint16_t tag=item.tag();
        bool wanted=tag==0xb020||tag==0xb029;
        for(const auto &f:fields)wanted|=tag==f.tag;
        if(!wanted)continue;
        QString text;
        try{text=QString::fromStdString(item.toString()).trimmed();}catch(...){continue;}
        if(text.size()>256) {warnings<<"oversized-field";continue;}
        const QString key=QString::fromStdString(item.key());raw[key]=text;
        if(tag==0xb020||tag==0xb029) {
            const QString code=tag==0xb020?normalize(text):colorMode(text);
            candidates.append(QVariantMap{{"source",key},{"raw",text},{"code",code}});
            if(tag==0xb020&&!text.isEmpty()&&code.isEmpty())unknownStyle=true;
            QString &slot=tag==0xb020?primary:secondary;
            if(!slot.isEmpty() && !code.isEmpty() && slot!=code)conflict=true;
            if(!code.isEmpty())slot=code;
            continue;
        }
        for(const auto &f:fields)if(tag==f.tag) {
            bool ok=false;const qlonglong v=text.toLongLong(&ok);
            const QString name=QString::fromLatin1(f.name);
            if(!ok||item.count()!=1||v<f.low||v>f.high) {invalid[name]=text;continue;}
            if(values.contains(name)&&values[name].toLongLong()!=v) {conflict=true;continue;}
            values[name]=v;sources[name]=key;
            if(tag>=0x2032&&tag<=0x2036)lookFields=true;
        }
    }
    // Some files name the family FL in the string but carry its subtype in ColorMode.
    const bool subtype=primary=="FL"&&(secondary=="FL2"||secondary=="FL3");
    if(!primary.isEmpty()&&!secondary.isEmpty()&&primary!=secondary&&!subtype)conflict=true;
    QString code=subtype?secondary:!primary.isEmpty()?primary:secondary;
    const bool unique=QStringList{"VV2","FL","FL2","FL3","IN","SH"}.contains(code);
    // Only an explicitly documented model is used as fallback for shared names.
    // Other models require Look-only fields/codes; no guessed release-date cutoff.
    const bool documentedModel=model.compare("ILCE-7M4",Qt::CaseInsensitive)==0;
    QString generation=(unique||lookFields||documentedModel)?"creative-look":legacyEvidence?"creative-style":"unknown";
    if(generation=="unknown"&&!candidates.isEmpty())warnings<<"shared-name-generation-unconfirmed";
    if(subtype)warnings<<"color-mode-refines-FL-subtype";
    if(conflict)warnings<<"conflicting-maker-note-fields";
    if(modelConflict)warnings<<"conflicting-camera-model-fields";
    if(unknownStyle)warnings<<"unknown-creative-style-not-replaced-by-color-mode";
    out["status"]=conflict?"conflict":unknownStyle?"unsupported":code.isEmpty()?(candidates.isEmpty()?"missing":"unsupported"):"recognized";
    // A known secondary mode is only a candidate when the primary name is unknown.
    // Do not present that fallback as a recognized as-shot look in the UI.
    out["generation"]=generation;out["code"]=unknownStyle?QString():code;out["rawFields"]=raw;
    out["parameters"]=values;out["parameterSources"]=sources;out["invalidParameters"]=invalid;
    out["candidates"]=candidates;out["warnings"]=warnings;
    out["autoEligible"]=!modelConflict&&!conflict&&!unknownStyle&&!code.isEmpty()&&generation=="creative-look";
    return out;
}
