#include "core/commands/CommandRegistry.h"
#include "core/cache/SourceCache.h"
#include "core/export/JpegExporter.h"
#include "core/export/PngExporter.h"
#include "core/look/LookProfiles.h"
#include "core/raw/RawDecoder.h"
#include <QGuiApplication>
#include <QCommandLineParser>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QTextStream>

int main(int argc, char **argv) {
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM")) qputenv("QT_QPA_PLATFORM","offscreen");
    QGuiApplication app(argc,argv);
    app.setApplicationName("JixelLight CLI");
    QCommandLineParser parser;
    parser.setApplicationDescription("Offline CPU reference export using the JixelLight engine. Existing destinations are never overwritten.");
    parser.addHelpOption();
    parser.addOption({"schema","Print supported command schema."});
    parser.addOption({"commands","JSON array of develop.set commands.","file"});
    parser.addOption({"space","Output ICC space: srgb, display-p3, adobe-rgb, prophoto-rgb.","space","srgb"});
    parser.addPositionalArgument("source","Input photograph.");
    parser.addPositionalArgument("destination","New JPEG or 16-bit PNG path.");
    parser.process(app);
    auto fail=[](const QString &error) { QTextStream(stderr) << error << '\n'; return 2; };
    if (parser.isSet("schema")) { QTextStream(stdout)<<QJsonDocument(CommandRegistry::schema()).toJson(); return 0; }
    if (!ColorManagement::keys().contains(parser.value("space"))) return fail("Unknown output color space");
    const auto paths=parser.positionalArguments();
    if(paths.size()!=2)return fail("Expected source and destination");
    const QFileInfo destination(paths[1]);
    if(destination.exists() || destination.isSymLink())return fail("Destination already exists; choose a new path");
    const auto suffix=destination.suffix().toLower();
    if(suffix!="png" && suffix!="jpg" && suffix!="jpeg")return fail("Destination must be JPEG or PNG");
    AdjustmentState state;
    const bool raw=RawDecoder::isRawFile(paths[0]);
    if(raw)state.look.mode="as-shot";
    if(parser.isSet("commands")) {
        QFile file(parser.value("commands"));
        if(!file.open(QIODevice::ReadOnly))return fail(file.errorString());
        if(file.size()>1024*1024)return fail("Commands file exceeds 1 MiB");
        QJsonParseError parseError;
        const auto document=QJsonDocument::fromJson(file.readAll(),&parseError);
        if(parseError.error!=QJsonParseError::NoError || !document.isArray())return fail("Commands must be a valid JSON array");
        for(const auto &entry:document.array()) {
            QString error;
            if(!entry.isObject() || !CommandRegistry::execute(state,entry.toObject(),&error))return fail(error.isEmpty()?QStringLiteral("Invalid command"):error);
        }
    }
    SourceCache cache;
    const auto source=loadSource(cache,paths[0],{});
    if(source.image.isNull())return fail(source.error);
    state=LookProfiles::resolveAsShot(state,source.metadata,raw);
    const double base=source.metadata.value("rawBaseExposureStops",0).toDouble();
    QString error;
    const auto space=ColorManagement::fromKey(parser.value("space"));
    const bool ok=suffix=="png" ? exportPng16(source.image,state,paths[1],space,{},&error,raw,float(base))
                              : exportJpegTiled(source.image,state,paths[1],space,92,{},&error,{}, {},raw,float(base));
    if(!ok)return fail(error);
    QTextStream(stdout)<<QJsonDocument(QJsonObject{{"ok",true},{"backend","cpu-reference"},{"destination",destination.absoluteFilePath()},{"space",parser.value("space")},{"adjustments",state.toJson()}}).toJson();
    return 0;
}
