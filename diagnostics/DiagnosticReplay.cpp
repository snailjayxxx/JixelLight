#include "diagnostics/DiagnosticReplay.h"
#include "core/commands/EditRecipe.h"
#include "core/commands/CommandSession.h"
#include "core/pipeline/ProcessingPlan.h"
#include <QCryptographicHash>
#include <QDir>
#include <QJsonDocument>

DiagnosticReplay::Snapshot DiagnosticReplay::capture(const AdjustmentState &storedIntent,bool rawSource,
    const QString &source,const QString &versionKey,quint64 photoEpoch,quint64 revision) {
    Snapshot result;
    result.m_metadata={{"schema",1},{"available",false},{"engine",ProcessingPlan::EngineVersion},
        {"source",source},{"source_is_RAW",rawSource},{"version_key",versionKey},
        {"photo_epoch",QString::number(photoEpoch)},{"parameter_revision",QString::number(revision)},
        {"scope","full-source CPU sRGB export of frozen stored edits; viewport, monitor ICC, soft proof and action history excluded"},
        {"original_file_included",false},{"source_content_verified_at_capture",false},
        {"catalog_writes",false},{"source_writes",false},{"external_service_required",false}};
    const auto fail=[&](const char *reason){result.m_metadata.insert("reason",QLatin1String(reason));return result;};
    if(source.isEmpty())return fail("no_source");
    if(!QDir::isAbsolutePath(source))return fail("source_path_not_absolute");
    const auto recipe=EditRecipe::encode(storedIntent,rawSource);
    if(!recipe) {
        if(recipe.error==EditRecipe::Error::UnsupportedLook)return fail("unsupported_look");
        if(recipe.error==EditRecipe::Error::TooLong)return fail("recipe_too_long");
        return fail("unrepresentable_state");
    }
    const QJsonObject manifest{{"schema",1},{"engine",ProcessingPlan::EngineVersion},
        {"commands",QJsonArray{
            QJsonObject{{"command","photo.import"},{"photo","snapshot"},{"source",source}},
            QJsonObject{{"command","edit.batch"},{"commands",QJsonDocument::fromJson(recipe.text.toUtf8()).array()}},
            QJsonObject{{"command","export.run"},{"destination","replay.png"},{"space","srgb"}}}}};
    // Reuse the offline schema and history planner without touching the source.
    const auto context=[rawSource](const QString &,bool,QString *)->std::optional<CommandRegistry::LookContext>{return CommandRegistry::LookContext{rawSource,{}};};
    CommandSession compiled;
    if(!CommandSession::compile(manifest,QDir::rootPath(),context,&compiled) || compiled.exports.size()!=1
        || compiled.exports[0].state.toJson()!=storedIntent.toJson())return fail("invalid_source_or_session");
    result.m_session=QJsonDocument(manifest).toJson(QJsonDocument::Indented);
    if(result.m_session.size()>4*1024*1024){result.m_session.clear();return fail("session_too_long");}
    result.m_metadata.insert("available",true);
    result.m_metadata.insert("session_entry","replay_session.json");
    result.m_metadata.insert("session_bytes",result.m_session.size());
    result.m_metadata.insert("session_sha256",QString::fromLatin1(QCryptographicHash::hash(result.m_session,QCryptographicHash::Sha256).toHex()));
    result.m_metadata.insert("cli_option","--session");
    result.m_metadata.insert("stored_intent_sha256",QString::fromLatin1(QCryptographicHash::hash(
        QJsonDocument(storedIntent.toJson()).toJson(QJsonDocument::Compact),QCryptographicHash::Sha256).toHex()));
    result.m_metadata.insert("output","replay.png relative to the extracted manifest; normal CLI preflight refuses existing destinations");
    result.m_metadata.insert("source_hash_enforced",false);
    result.m_metadata.insert("source_requirements","Replays the file currently at the captured path. No source hash is captured or enforced; keep the original unchanged.");
    return result;
}
