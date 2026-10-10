#include "core/commands/CommandRegistry.h"
#include "core/cache/SourceCache.h"
#include "core/export/JpegExporter.h"
#include "core/export/PngExporter.h"
#include "core/export/RasterExporter.h"
#include "core/export/ExportNaming.h"
#include "core/metadata/MetadataReader.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include "core/project/ProjectDatabase.h"
#include "core/library/CatalogSelection.h"
#include <QCoreApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QImageWriter>
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

bool readCatalogVersions(const QString &path,const QStringList &keys,QVector<ProjectDatabase::SavedPhoto> *photos,QString *error) {
    ProjectDatabase catalog; QVector<ProjectDatabase::SavedPhoto> all;
    if (!catalog.readSnapshot(path,&all)) { *error=catalog.lastError(); return false; }
    QStringList identities;
    for (const auto &photo : all) identities.push_back(photo.copyKey.isEmpty()?photo.path:photo.copyKey);
    const auto selection=selectCatalogVersions(identities,keys);
    if (!selection.error.isEmpty()) { *error=selection.error; return false; }
    if (selection.indices.size()==all.size()) *photos=std::move(all);
    else {
        photos->clear(); photos->reserve(selection.indices.size());
        for (const auto index : selection.indices) photos->push_back(std::move(all[index]));
    }
    return true;
}

bool listCatalog(const QString &path,const QStringList &keys,QJsonObject *report,QString *error) {
    QVector<ProjectDatabase::SavedPhoto> photos;
    if (!readCatalogVersions(path,keys,&photos,error)) return false;
    const QDir base(QFileInfo(path).absoluteFilePath()); QJsonArray versions;
    for (const auto &photo : photos) {
        versions.append(QJsonObject{{"catalog_key",photo.copyKey.isEmpty()?photo.path:photo.copyKey},
            {"source",QDir::cleanPath(base.absoluteFilePath(photo.path))},{"version_name",photo.versionName},
            {"display_name",photo.copyKey.isEmpty()?QStringLiteral("Original"):photo.versionName},
            {"is_virtual",!photo.copyKey.isEmpty()},{"rating",photo.rating},{"flag",photo.flag},
            {"tags",photo.tags.toJson()},{"timeline",photo.timeline.toJson()}});
    }
    *report={{"schema",1},{"mode","catalog-list"},{"ok",true},{"read_only",true},
        {"source_commit",JIXELLIGHT_GIT_COMMIT},{"engine",ProcessingPlan::EngineVersion},
        {"catalog",base.absolutePath()},{"count",qint64(photos.size())},{"versions",versions}};
    return true;
}

bool readCatalog(const QString &path, const QString &destination, const QString &space, const QString &format,
                 const ExportNaming &naming,const QStringList &keys, QVector<ExportJob> *jobs, QString *error) {
    if (!QStringList{"png","jpeg","jpg","tif","tiff","webp"}.contains(format)) { *error="Catalog format must be JPEG, PNG, TIFF or WebP"; return false; }
    if (destination.trimmed().isEmpty() || !QFileInfo(destination).isDir()) { *error="Choose an existing --output-dir for catalog exports"; return false; }
    QVector<ProjectDatabase::SavedPhoto> photos;
    if (!readCatalogVersions(path,keys,&photos,error)) return false;
    if (photos.isEmpty() || photos.size()>1000) { *error="Catalog export requires 1 to 1000 saved versions"; return false; }
    const QDir base(QFileInfo(path).absoluteFilePath()), output(QFileInfo(destination).absoluteFilePath());
    const auto extension=format=="jpeg" ? QStringLiteral("jpg") : format=="tiff" ? QStringLiteral("tif") : format;
    ExportNames names;
    if (!naming.pattern.isEmpty()) {
        QVector<ExportNameSource> sources; QHash<QString,QString> captured;
        for (const auto &photo : photos) {
            const auto source=base.absoluteFilePath(photo.path); auto time=photo.timeline.captureTime;
            if (exportTemplateNeedsCaptureTime(naming.pattern) && !photo.timeline.captureChecked) {
                if (!captured.contains(source)) captured.insert(source,PhotoTimeline::cameraTime(MetadataReader::read(source).value("captureTime").toString()));
                time=captured.value(source);
            }
            sources.push_back({source,photo.versionName,time});
        }
        names=planExportNames(sources,naming,format,output.entryList(QDir::AllEntries|QDir::Hidden|QDir::System|QDir::NoDotAndDotDot));
        if (!names.error.isEmpty()) { *error=names.error; return false; }
    }
    for (qsizetype i=0;i<photos.size();++i) {
        const auto &photo=photos[i]; const auto source=base.absoluteFilePath(photo.path);
        const auto filename=naming.pattern.isEmpty()
            ? QString("%1_%2_JixelLight.%3").arg(i+1,6,10,QChar('0')).arg(QFileInfo(source).completeBaseName().left(120)).arg(extension)
            : names.names[i];
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
        if (!QStringList{"png","jpg","jpeg","tif","tiff","webp"}.contains(suffix)) return fail("Destination must be JPEG, PNG, TIFF or WebP");
        const auto codec=suffix=="tif" ? QByteArray("tiff") : suffix.toLatin1();
        if (suffix!="jpg" && suffix!="jpeg" && !QImageWriter::supportedImageFormats().contains(codec))
            return fail("Required image format plugin is unavailable: "+QString::fromLatin1(codec));
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
    const bool rendered=(suffix=="jpg" || suffix=="jpeg") ? exportJpegTiled(source.image,state,temporary,space,92,{},error,{}, {},raw,float(base))
        : exportRaster(source.image,state,temporary,space,suffix=="png" ? RasterFormat::Png16 : suffix=="webp" ? RasterFormat::WebP8 : RasterFormat::Tiff16,92,{},error,raw,float(base));
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
    parser.addOption({"list-catalog","List saved catalog version keys and annotations without reading image files. Requires --catalog."});
    parser.addOption({"catalog-key","Select an exact saved catalog key; repeat for multiple versions. Saved catalog order is retained.","key"});
    parser.addOption({"output-dir","Existing output directory for --catalog.","directory"});
    parser.addOption({"format","Catalog output format: png, jpeg, tiff, webp (JPEG/WebP quality 92).","format","png"});
    parser.addOption({"name-template","Catalog filename stem: {name}, {version}, {seq}, {seq:1}..{seq:9}, {capture_date}, {capture_time}. Empty keeps existing names.","template"});
    parser.addOption({"sequence-start","First sequence number for --name-template (1–999999999).","number","1"});
    parser.addOption({"space","Output ICC space: srgb, display-p3, adobe-rgb, prophoto-rgb.","space","srgb"});
    parser.addPositionalArgument("source","Input photograph.");
    parser.addPositionalArgument("destination","New JPEG, 16-bit PNG/TIFF or 8-bit WebP path.");
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
            {"selected_keys",QJsonObject{{"option","--catalog-key"},{"repeatable",true},{"maximum_keys",1000},
                {"matching","exact saved catalog_key; case-sensitive"},{"order","saved catalog order"},
                {"invalid_selection","reject whole plan before output"}}},
            {"listing",QJsonObject{{"option","--list-catalog"},{"source_metadata_reads",false},
                {"contents","saved version identities, source paths, names, curation, tags and timeline"},
                {"empty_catalog","valid empty list"}}},
            {"maximum_versions",1000},{"optional_commands","applied to each saved snapshot without changing catalog"},
            {"name_template",QJsonObject{{"tokens",QJsonArray{"name","version","seq","seq:1..9","capture_date","capture_time"}},
                {"maximum_characters",160},{"sequence_range",QJsonArray{1,999999999}},
                {"capture_date","yyyyMMdd, recorded camera wall clock"},{"capture_time","HHmmss, recorded camera wall clock"},
                {"missing_capture_time","reject whole plan"},{"collision","reject whole plan"},{"original_version","Original"}}}};
        QTextStream(stdout)<<QJsonDocument(schema).toJson(); return 0;
    }
    if (!ColorManagement::keys().contains(parser.value("space"))) return fail("Unknown output color space");
    const auto paths=parser.positionalArguments();
    const bool batch=parser.isSet("batch"), catalog=parser.isSet("catalog"), multi=batch||catalog;
    QVector<ExportJob> jobs; QString error;
    if (batch && catalog) return fail("Choose either --batch or --catalog");
    if (!catalog && (parser.isSet("output-dir") || parser.isSet("format") || parser.isSet("name-template") || parser.isSet("sequence-start")
        || parser.isSet("list-catalog") || parser.isSet("catalog-key")))
        return fail("--output-dir, --format, --name-template, --sequence-start, --list-catalog and --catalog-key require --catalog");
    if (catalog) {
        if (!paths.isEmpty()) return fail("Catalog cannot be combined with positional paths");
        if (parser.isSet("list-catalog")) {
            if (parser.isSet("output-dir") || parser.isSet("format") || parser.isSet("name-template") || parser.isSet("sequence-start")
                || parser.isSet("commands") || parser.isSet("space")) return fail("--list-catalog cannot be combined with export settings or commands");
            QJsonObject report;
            if (!listCatalog(parser.value("catalog"),parser.values("catalog-key"),&report,&error)) return fail(error);
            QTextStream(stdout)<<QJsonDocument(report).toJson(); return 0;
        }
        bool validSequence=false; const auto sequence=parser.value("sequence-start").toInt(&validSequence);
        if (!validSequence || sequence<1 || sequence>999999999) return fail("Sequence start must be within 1–999999999");
        if (parser.isSet("sequence-start") && parser.value("name-template").isEmpty()) return fail("--sequence-start requires a nonempty --name-template");
        if (!readCatalog(parser.value("catalog"),parser.value("output-dir"),parser.value("space"),parser.value("format").toLower(),
                         {parser.value("name-template"),sequence},parser.values("catalog-key"),&jobs,&error)) return fail(error);
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
