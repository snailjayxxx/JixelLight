#include "CommandSession.h"
#include "core/commands/EditHistory.h"
#include "core/color/ColorManagement.h"
#include "core/pipeline/ProcessingPlan.h"
#include <QDir>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QHash>

namespace {
bool fields(const QJsonObject &object,const QStringList &required,const QStringList &optional={}) {
    for(const auto &key:required)if(!object.contains(key))return false;
    for(auto it=object.begin();it!=object.end();++it)if(!required.contains(it.key())&&!optional.contains(it.key()))return false;
    return true;
}
bool text(const QJsonValue &value) {
    if(!value.isString()||value.toString().isEmpty()||value.toString().size()>32768)return false;
    for(const auto ch:value.toString())if(ch.unicode()<32||ch.unicode()==127)return false;
    return true;
}
}
bool CommandSession::compile(const QJsonObject &manifest,const QString &baseDirectory,const SourceContext &sourceContext,CommandSession *output,QString *error) {
    if(error)error->clear();
    const auto fail=[&](const QString &why){if(error)*error=why;return false;};
    if(!output||!sourceContext||!QDir::isAbsolutePath(baseDirectory)
        ||!fields(manifest,{"schema","engine","commands"})||!manifest["schema"].isDouble()||manifest["schema"].toDouble()!=1
        ||manifest["engine"].toString()!=QLatin1String(ProcessingPlan::EngineVersion)||!manifest["commands"].isArray()
        ||QJsonDocument(manifest).toJson(QJsonDocument::Compact).size()>4*1024*1024)return fail("Invalid session schema, engine or size");
    const auto commands=manifest["commands"].toArray();
    if(commands.isEmpty()||commands.size()>10000)return fail("Session requires1–10000 commands");
    struct Photo {QString source;AdjustmentState state;EditHistory history;};
    QHash<QString,Photo> photos;QString current;CommandSession staged;const QDir base(baseDirectory);
    const auto charge=[&](const AdjustmentState &state){
        const auto bytes=QJsonDocument(state.toJson()).toJson(QJsonDocument::Compact).size();
        if(bytes>MaximumStateBytes-staged.chargedStateBytes)return false;
        staged.chargedStateBytes+=bytes;return true;
    };
    for(qsizetype index=0;index<commands.size();++index){
        const auto reject=[&](const QString &why){return fail(QString("Session command %1: %2").arg(index+1).arg(why));};
        if(!commands[index].isObject()||++staged.commandCount>10000)return reject("Invalid command or aggregate command limit");
        const auto command=commands[index].toObject();
        if(!command["command"].isString())return reject("Expected command name");
        const auto name=command["command"].toString();
        if(name=="photo.import"){
            static const QRegularExpression id("^[A-Za-z0-9][A-Za-z0-9_.-]{0,63}$");
            if(!fields(command,{"command","photo","source"})||!command["photo"].isString()||!id.match(command["photo"].toString()).hasMatch()
                ||photos.contains(command["photo"].toString())||photos.size()>=1000||!text(command["source"]))return reject("Invalid, duplicate or excessive photo identity/source");
            const auto path=QDir::cleanPath(base.absoluteFilePath(command["source"].toString()));QString why;
            const auto context=sourceContext(path,false,&why);if(!context)return reject(why.isEmpty()?QStringLiteral("Source unavailable"):why);
            Photo photo;photo.source=path;if(context->rawSource)photo.state.look.mode="as-shot";
            if(!charge(photo.state))return reject("Session state budget exceeded");
            photo.history.initialize(photo.state);current=command["photo"].toString();photos.insert(current,std::move(photo));continue;
        }
        if(name=="photo.select"){
            if(!fields(command,{"command","photo"})||!command["photo"].isString()||!photos.contains(command["photo"].toString()))return reject("Unknown photo identity");
            current=command["photo"].toString();continue;
        }
        if(current.isEmpty())return reject("Import/select a photo before editing or exporting");
        auto &photo=photos[current];
        if(name=="export.run"){
            if(!fields(command,{"command","destination"},{"space"})||!text(command["destination"])
                ||(command.contains("space")&&!command["space"].isString())||staged.exports.size()>=1000)return reject("Invalid or excessive export");
            const auto space=command.value("space").toString("srgb");if(!ColorManagement::keys().contains(space))return reject("Unknown output color space");
            if(!charge(photo.state))return reject("Session state budget exceeded");
            staged.exports.push_back({current,photo.source,QDir::cleanPath(base.absoluteFilePath(command["destination"].toString())),space,photo.state});continue;
        }
        if(name=="history.undo"||name=="history.redo"){
            CommandRegistry::Control control;
            if(!CommandRegistry::decodeControl(command,&control))return reject("History boundary or invalid fields");
            const bool undo=control.kind==CommandRegistry::ControlKind::Undo;
            if(undo?!photo.history.canUndo():!photo.history.canRedo())return reject("History boundary or invalid fields");
            const auto &next=undo?photo.history.undo():photo.history.redo();if(!charge(next))return reject("Session state budget exceeded");
            photo.state=next;continue;
        }
        QJsonArray edits;
        if(name=="edit.batch"){
            CommandRegistry::Control control;
            if(!CommandRegistry::decodeControl(command,&control))return reject("Invalid edit batch");
            edits=std::move(control.edits);
            if(edits.size()>10000-staged.commandCount)return reject("Aggregate command limit exceeded");
            staged.commandCount+=int(edits.size());
        }else edits.append(command);
        QString why;std::optional<CommandRegistry::LookContext> context;
        if(CommandRegistry::requiresLookContext(photo.state,edits)){
            context=sourceContext(photo.source,true,&why);if(!context)return reject(why.isEmpty()?QStringLiteral("Source metadata unavailable"):why);
        }
        auto next=photo.state;
        if(!CommandRegistry::executeBatch(next,edits,&why,context?&*context:nullptr))return reject(why);
        if(!charge(next))return reject("Session state budget exceeded");
        photo.history.finish();photo.history.record(next,name);photo.state=std::move(next);
    }
    if(staged.exports.isEmpty())return fail("Session requires at least one export.run");
    staged.photoCount=int(photos.size());*output=std::move(staged);return true;
}
QJsonObject CommandSession::schema(){
    return {{"schema",1},{"engine",ProcessingPlan::EngineVersion},{"option","--session"},
        {"required_fields",QJsonArray{"schema","engine","commands"}},{"maximum_bytes",4*1024*1024},{"maximum_commands",10000},
        {"maximum_photos",1000},{"maximum_exports",1000},{"maximum_charged_state_bytes",MaximumStateBytes},
        {"relative_paths","manifest directory"},{"catalog_writes",false},{"source_writes",false},{"backend","cpu-reference"},
        {"validation","whole plan before image decode or output; source facts/metadata may be read"},
        {"runtime_failure","stop; completed exports retained"},{"edit_commands","same CommandRegistry as desktop/--commands; source-specific as-shot metadata"},
        {"history","per-photo desktop EditHistory; edit.batch is one undo step; invalid boundary rejects whole plan"},
        {"shared_controls",CommandRegistry::controlSchema()},
        {"commands",QJsonArray{
            QJsonObject{{"name","photo.import"},{"fields",QJsonArray{"photo","source"}},{"operation","register a transient photo and select it; no file copy/catalog write"}},
            QJsonObject{{"name","photo.select"},{"fields",QJsonArray{"photo"}}},
            QJsonObject{{"name","edit.batch"},{"fields",QJsonArray{"commands"}}},
            QJsonObject{{"name","history.undo"},{"fields",QJsonArray{}}},QJsonObject{{"name","history.redo"},{"fields",QJsonArray{}}},
            QJsonObject{{"name","export.run"},{"fields",QJsonArray{"destination"}},{"optional_fields",QJsonArray{"space"}},{"default_space","srgb"},{"operation","freeze current state; publish after complete plan validates; never overwrite"}}}}};
}
