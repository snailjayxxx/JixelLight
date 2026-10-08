#include "core/commands/CommandRegistry.h"
#include "core/cache/SourceCache.h"
#include "core/export/JpegExporter.h"
#include "core/export/PngExporter.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include "core/project/ProjectDatabase.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSet>
#include <QTemporaryDir>
#include <QTextStream>

namespace {
struct ExportJob {
    QString source, destination, space;
    AdjustmentState state;
    QString catalogKey, versionName;
};

bool readJson(const QString &path, qint64 limit, QJsonDocument *document, QString *error) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { *error=file.errorString(); return false; }
    if (file.size()>limit) { *error="JSON file exceeds size limit"; return false; }
    QJsonParseError parseError;
    *document=QJsonDocument::fromJson(file.readAll(),&parseError);
    if (parseError.error!=QJsonParseError::NoError) { *error=parseError.errorString(); return false; }
    return true;
}

bool applyCommands(AdjustmentState &state, const QJsonValue &commands, QString *error) {
    if (!commands.isArray() || commands.toArray().size()>10000) { *error="Commands must be an array of at most 10000 entries"; return false; }
    for (const auto &entry:commands.toArray()) {
        if (!entry.isObject()) { *error="Invalid command"; return false; }
        if (!CommandRegistry::execute(state,entry.toObject(),error)) return false;
    }
    return true;
}

bool onlyFields(const QJsonObject &object, const QStringList &allowed) {
    for (auto it=object.begin();it!=object.end();++it) if (!allowed.contains(it.key())) return false;
    return true;
}

bool readBatch(const QString &path, const QString &defaultSpace, QVector<ExportJob> *jobs, QString *error) {
    QJsonDocument document;
    if (!readJson(path,4*1024*1024,&document,error)) return false;
    const auto manifest=document.object();
    if (!document.isObject() || !onlyFields(manifest,{"schema","engine","jobs"})
        || !manifest.value("schema").isDouble() || manifest.value("schema").toDouble()!=1
        || manifest.value("engine").toString()!=QLatin1String(ProcessingPlan::EngineVersion)
        || !manifest.value("jobs").isArray()) {
        *error="Invalid batch manifest schema, fields or engine"; return false;
    }
    const auto entries=manifest.value("jobs").toArray();
    if (entries.isEmpty() || entries.size()>1000) { *error="Batch must contain 1 to 1000 jobs"; return false; }
    const QDir base(QFileInfo(path).absolutePath());
    for (qsizetype i=0;i<entries.size();++i) {
        const auto entry=entries[i].toObject();
        auto fail=[&](const QString &message) { *error=QString("Job %1: %2").arg(i+1).arg(message); return false; };
        if (!entries[i].isObject() || !onlyFields(entry,{"source","destination","commands","space"})
            || !entry.value("source").isString() || entry.value("source").toString().trimmed().isEmpty()
            || !entry.value("destination").isString() || entry.value("destination").toString().trimmed().isEmpty()
            || (entry.contains("space") && !entry.value("space").isString())) return fail("Invalid job fields");
        ExportJob job{base.absoluteFilePath(entry.value("source").toString()),base.absoluteFilePath(entry.value("destination").toString()),entry.value("space").toString(defaultSpace),{}};
        if (RawDecoder::isRawFile(job.source)) job.state.look.mode="as-shot";
        QString commandError;
        if (!applyCommands(job.state,entry.value("commands"),&commandError)) return fail(commandError);
        jobs->push_back(std::move(job));
    }
    return true;
}

bool readCatalog(const QString &path, const QString &destination, const QString &space, const QString &format,
                 QVector<ExportJob> *jobs, QString *error) {
    if (format!="png" && format!="jpeg" && format!="jpg") { *error="Catalog format must be JPEG or PNG"; return false; }
    if (destination.trimmed().isEmpty() || !QFileInfo(destination).isDir()) { *error="Choose an existing --output-dir for catalog exports"; return false; }
    ProjectDatabase catalog; QVector<ProjectDatabase::SavedPhoto> photos;
    if (!catalog.readSnapshot(path,&photos)) { *error=catalog.lastError(); return false; }
    if (photos.isEmpty() || photos.size()>1000) { *error="Catalog export requires 1 to 1000 saved versions"; return false; }
    const QDir base(QFileInfo(path).absoluteFilePath()), output(QFileInfo(destination).absoluteFilePath());
    const auto extension=format=="png" ? QStringLiteral("png") : QStringLiteral("jpg");
    for (qsizetype i=0;i<photos.size();++i) {
        const auto &photo=photos[i]; const auto source=base.absoluteFilePath(photo.path);
        const auto filename=QString("%1_%2_JixelLight.%3").arg(i+1,6,10,QChar('0')).arg(QFileInfo(source).completeBaseName().left(120)).arg(extension);
        jobs->push_back({source,output.filePath(filename),space,photo.adjustments,
            photo.copyKey.isEmpty()?photo.path:photo.copyKey,photo.versionName});
    }
    return true;
}

// Validate the entire plan before decoding or producing any output. Resolve
// parent aliases, since two lexical paths can designate the same new file.
bool preflight(QVector<ExportJob> *jobs, QString *error) {
    QSet<QString> destinations;
    for (qsizetype i=0;i<jobs->size();++i) {
        auto &job=(*jobs)[i];
        auto fail=[&](const QString &message) { *error=QString("Job %1: %2").arg(i+1).arg(message); return false; };
        if (!ColorManagement::keys().contains(job.space)) return fail("Unknown output color space");
        const QFileInfo source(job.source), destination(job.destination);
        if (!source.isFile() || !source.isReadable()) return fail("Source is not a readable file: "+job.source);
        if (destination.exists() || destination.isSymLink()) return fail("Destination already exists; choose a new path");
        const auto suffix=destination.suffix().toLower();
        if (suffix!="png" && suffix!="jpg" && suffix!="jpeg") return fail("Destination must be JPEG or PNG");
        const QFileInfo parent(destination.absolutePath());
        if (!parent.isDir() || parent.canonicalFilePath().isEmpty()) return fail("Destination directory does not exist");
        job.source=source.absoluteFilePath();
        job.destination=QDir(parent.canonicalFilePath()).filePath(destination.fileName());
        // Portable plans also reject case-only collisions on a case-sensitive host.
        const auto key=job.destination.toCaseFolded();
        if (destinations.contains(key)) return fail("Duplicate destination in batch");
        destinations.insert(key);
    }
    return true;
}

bool executeExport(const ExportJob &job, SourceCache &cache, QJsonObject *result, QString *error) {
    const auto source=loadSource(cache,job.source,{});
    if (source.image.isNull()) { *error=source.error; return false; }
    const bool raw=RawDecoder::isRawFile(job.source);
    const auto state=LookProfiles::resolveAsShot(job.state,source.metadata,raw);
    const double base=source.metadata.value("rawBaseExposureStops",0).toDouble();
    const auto suffix=QFileInfo(job.destination).suffix().toLower();
    // A unique directory on the destination filesystem has no open file handle
    // for QSaveFile to replace. QTemporaryFile::close() retains its native handle
    // until destruction, blocking that replacement on Windows. The directory
    // also owns cleanup after encode/publication failure. Final rename refuses
    // any destination which appeared after preflight.
    QTemporaryDir staging(QDir(QFileInfo(job.destination).absolutePath()).filePath(".jixellight-export-XXXXXX"));
    if (!staging.isValid()) { *error=staging.errorString(); return false; }
    const auto temporary=QDir(staging.path()).filePath("output."+suffix);
    const auto space=ColorManagement::fromKey(job.space);
    const bool rendered=suffix=="png" ? exportPng16(source.image,state,temporary,space,{},error,raw,float(base))
        : exportJpegTiled(source.image,state,temporary,space,92,{},error,{}, {},raw,float(base));
    if (!rendered) return false;
    QFile publication(temporary);
    if (!publication.rename(job.destination)) { *error="Cannot publish new destination: "+publication.errorString(); return false; }
    *result={{"ok",true},{"backend","cpu-reference"},{"source_commit",JIXELLIGHT_GIT_COMMIT},
        {"engine",ProcessingPlan::EngineVersion},{"source",job.source},{"destination",job.destination},
        {"space",job.space},{"adjustments",state.toJson()}};
    return true;
}
}

int main(int argc, char **argv) {
    QCoreApplication app(argc,argv);
    app.setApplicationName("JixelLight CLI");
    QCommandLineParser parser;
    parser.setApplicationDescription("Offline CPU reference export using the JixelLight engine. Existing destinations are never overwritten.");
    parser.addHelpOption();
    parser.addOption({"schema","Print supported command schema."});
    parser.addOption({"commands","JSON array of shared Develop commands.","file"});
    parser.addOption({"batch","Execute a validated JSON export plan; relative paths use its directory. Stop on runtime failure, retaining completed outputs.","file"});
    parser.addOption({"catalog","Export all saved catalog versions from a read-only snapshot.","project.jlp"});
    parser.addOption({"output-dir","Existing output directory for --catalog.","directory"});
    parser.addOption({"format","Catalog output format: png, jpeg (JPEG quality 92).","format","png"});
    parser.addOption({"space","Output ICC space: srgb, display-p3, adobe-rgb, prophoto-rgb.","space","srgb"});
    parser.addPositionalArgument("source","Input photograph.");
    parser.addPositionalArgument("destination","New JPEG or 16-bit PNG path.");
    parser.process(app);
    auto fail=[](const QString &error) { QTextStream(stderr) << error << '\n'; return 2; };
    if (parser.isSet("schema")) {
        auto schema=CommandRegistry::schema();
        schema["batch"]=QJsonObject{{"schema",1},{"engine",ProcessingPlan::EngineVersion},
            {"required_fields",QJsonArray{"schema","engine","jobs"}},
            {"job_required_fields",QJsonArray{"source","destination","commands"}},
            {"job_optional_fields",QJsonArray{"space"}},{"maximum_jobs",1000},{"maximum_bytes",4*1024*1024},
            {"relative_paths","manifest directory"},{"runtime_failure","stop; completed outputs retained"}};
        schema["catalog"]=QJsonObject{{"read_only",true},{"selection","all saved originals and virtual versions"},
            {"maximum_versions",1000},{"optional_commands","applied to each saved snapshot without changing catalog"}};
        QTextStream(stdout)<<QJsonDocument(schema).toJson(); return 0;
    }
    if (!ColorManagement::keys().contains(parser.value("space"))) return fail("Unknown output color space");
    const auto paths=parser.positionalArguments();
    const bool batch=parser.isSet("batch"), catalog=parser.isSet("catalog"), multi=batch||catalog;
    QVector<ExportJob> jobs; QString error;
    if (batch && catalog) return fail("Choose either --batch or --catalog");
    if (!catalog && (parser.isSet("output-dir") || parser.isSet("format"))) return fail("--output-dir and --format require --catalog");
    if (catalog) {
        if (!paths.isEmpty()) return fail("Catalog cannot be combined with positional paths");
        if (!readCatalog(parser.value("catalog"),parser.value("output-dir"),parser.value("space"),parser.value("format").toLower(),&jobs,&error)) return fail(error);
        if (parser.isSet("commands")) {
            QJsonDocument document;
            if (!readJson(parser.value("commands"),1024*1024,&document,&error)) return fail(error);
            for (auto &job:jobs) if (!document.isArray() || !applyCommands(job.state,QJsonValue(document.array()),&error)) return fail(error.isEmpty()?QStringLiteral("Commands must be a JSON array"):error);
        }
    } else if (batch) {
        if (!paths.isEmpty() || parser.isSet("commands")) return fail("Batch cannot be combined with positional paths or --commands");
        if (!readBatch(parser.value("batch"),parser.value("space"),&jobs,&error)) return fail(error);
    } else {
        if (paths.size()!=2) return fail("Expected source and destination, --batch or --catalog");
        ExportJob job{paths[0],paths[1],parser.value("space"),{}};
        if (RawDecoder::isRawFile(job.source)) job.state.look.mode="as-shot";
        if (parser.isSet("commands")) {
            QJsonDocument document;
            if (!readJson(parser.value("commands"),1024*1024,&document,&error)) return fail(error);
            if (!document.isArray() || !applyCommands(job.state,QJsonValue(document.array()),&error)) return fail(error.isEmpty()?QStringLiteral("Commands must be a JSON array"):error);
        }
        jobs.push_back(std::move(job));
    }
    if (!preflight(&jobs,&error)) return fail(error);
    SourceCache cache; QJsonArray results; int completed=0;
    for (qsizetype i=0;i<jobs.size();++i) {
        QJsonObject result;
        const bool ok=executeExport(jobs[i],cache,&result,&error);
        if (!multi) {
            if (!ok) return fail(error);
            QTextStream(stdout)<<QJsonDocument(result).toJson(); return 0;
        }
        if (!ok) result={{"ok",false},{"source",jobs[i].source},{"destination",jobs[i].destination},{"error",error}};
        if (!jobs[i].catalogKey.isEmpty()) { result["catalog_key"]=jobs[i].catalogKey; result["version_name"]=jobs[i].versionName; }
        result["index"]=qint64(i); results.append(result);
        if (!ok) break;
        ++completed;
    }
    const bool ok=completed==jobs.size();
    QTextStream(stdout)<<QJsonDocument(QJsonObject{{"schema",1},{"mode",catalog?"catalog":"batch"},{"ok",ok},
        {"backend","cpu-reference"},{"source_commit",JIXELLIGHT_GIT_COMMIT},{"engine",ProcessingPlan::EngineVersion},
        {"job_count",qint64(jobs.size())},{"completed",completed},{"failed",ok?0:1},
        {"not_attempted",qint64(jobs.size()-results.size())},{"results",results}}).toJson();
    return ok?0:fail(error);
}
