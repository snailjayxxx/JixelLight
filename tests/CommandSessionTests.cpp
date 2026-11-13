#include <QtTest>
#include <QDir>
#include "core/commands/CommandSession.h"
#include "core/pipeline/ProcessingPlan.h"

class CommandSessionTests:public QObject {
    Q_OBJECT
    static QJsonObject manifest(const QJsonArray &commands){return {{"schema",1},{"engine",ProcessingPlan::EngineVersion},{"commands",commands}};}
    static QJsonObject photoImport(const QString &id="a",const QString &path="original.png"){return {{"command","photo.import"},{"photo",id},{"source",path}};}
    static QJsonObject save(const QString &path="output.png"){return {{"command","export.run"},{"destination",path}};}
    static QJsonObject ev(double value){return {{"command","develop.set"},{"parameter","exposure"},{"value",value}};}
    static CommandSession::SourceContext source(){return [](const QString &path,bool,QString *)->std::optional<CommandRegistry::LookContext>{return CommandRegistry::LookContext{path.endsWith(".ARW"),{}};};}
    static QString base(){return QDir::rootPath()+"session";}
    static QJsonObject describedCommand(const QJsonArray &descriptions,const QString &name){
        QJsonObject result;for(const auto &entry:descriptions)if(entry.toObject()["name"].toString()==name){
            if(!result.isEmpty())return {};result=entry.toObject();
        }return result;
    }
private slots:
    void exportSnapshotsAndHistoryStayPerPhoto(){
        CommandSession result;QString error;
        const QJsonArray commands{photoImport(),ev(1),save("first.png"),photoImport("b","second.png"),ev(-1),save("second-out.png"),
            QJsonObject{{"command","photo.select"},{"photo","a"}},QJsonObject{{"command","history.undo"}},save("original-out.png"),
            QJsonObject{{"command","history.redo"}},save("redone.png")};
        QVERIFY2(CommandSession::compile(manifest(commands),base(),source(),&result,&error),qPrintable(error));
        QCOMPARE(result.photoCount,2);QCOMPARE(result.commandCount,11);QCOMPARE(result.exports.size(),4);
        QCOMPARE(result.exports[0].state.exposure,1.);QCOMPARE(result.exports[1].state.exposure,-1.);QCOMPARE(result.exports[2].state.exposure,0.);QCOMPARE(result.exports[3].state.exposure,1.);
        QCOMPARE(result.exports[0].photo,QString("a"));QCOMPARE(result.exports[1].photo,QString("b"));
        QCOMPARE(result.exports[0].destination,base()+"/first.png");QCOMPARE(result.exports[1].source,base()+"/second.png");
        QVERIFY(result.chargedStateBytes>0);QVERIFY(result.chargedStateBytes<CommandSession::MaximumStateBytes);
    }
    void editBatchUsesOneDesktopUndoStepAndRegistryRules(){
        const QJsonArray edits{ev(.7),QJsonObject{{"command","mask.luminance.range"},{"lower",.2},{"upper",1.2},{"feather",.3}},
            QJsonObject{{"command","mask.luminance.set"},{"parameter","exposure"},{"value",1.5}},QJsonObject{{"command","mask.luminance.enable"},{"enabled",true}}};
        CommandSession result;QString error;
        QVERIFY2(CommandSession::compile(manifest({photoImport(),QJsonObject{{"command","edit.batch"},{"commands",edits}},save("edited.png"),QJsonObject{{"command","history.undo"}},save()}),base(),source(),&result,&error),qPrintable(error));
        AdjustmentState oracle;QVERIFY(CommandRegistry::executeBatch(oracle,edits));QCOMPARE(result.exports[0].state.toJson(),oracle.toJson());
        QCOMPARE(result.exports[1].state.toJson(),AdjustmentState{}.toJson());QCOMPARE(result.commandCount,9);
    }
    void lateFailuresLeaveCallerOutputUnchanged(){
        const QJsonArray badTail{QJsonObject{{"command","history.redo"}},QJsonObject{{"command","photo.select"},{"photo","missing"}},
            QJsonObject{{"command","develop.set"},{"parameter","missing"},{"value",2}},QJsonObject{{"command","edit.batch"},{"commands",QJsonArray{ev(2),QJsonObject{{"command","bad"}}}}},
            QJsonObject{{"command","history.undo"},{"extra",true}},QJsonObject{{"command","photo.import"},{"photo","a"},{"source","other.png"}},
            QJsonObject{{"command","export.run"},{"destination","bad.png"},{"space","unknown"}}};
        for(const auto &tail:badTail){CommandSession result;result.photoCount=71;result.commandCount=72;QString error;
            QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),tail}),base(),source(),&result,&error));QVERIFY(!error.isEmpty());QCOMPARE(result.photoCount,71);QCOMPARE(result.commandCount,72);QVERIFY(result.exports.isEmpty());}
    }
    void strictManifestTypesPathsAndModesAreRequired(){
        const auto valid=manifest({photoImport(),save()});
        for(const auto &field:QStringList{"schema","engine","commands"}){auto bad=valid;bad.remove(field);CommandSession result;QVERIFY(!CommandSession::compile(bad,base(),source(),&result));}
        auto bad=valid;bad["extra"]=true;CommandSession result;QVERIFY(!CommandSession::compile(bad,base(),source(),&result));
        bad=valid;bad["schema"]=true;QVERIFY(!CommandSession::compile(bad,base(),source(),&result));
        bad=valid;bad["engine"]="other-engine";QVERIFY(!CommandSession::compile(bad,base(),source(),&result));
        for(const auto &commands:QList<QJsonArray>{{},{save()},{photoImport()},{photoImport("bad id"),save()},{photoImport("a",QString("bad\nsource")),save()},
            {photoImport(),QJsonObject{{"command","edit.batch"},{"commands",QJsonArray{}}},save()},{photoImport(),QJsonObject{{"command","history.undo"}},save()}})
            QVERIFY(!CommandSession::compile(manifest(commands),base(),source(),&result));
        QVERIFY(!CommandSession::compile(valid,"relative",source(),&result));
        QVERIFY(!CommandSession::compile(valid,base(),{},&result));
    }
    void sourceMetadataIsRequestedOnlyForItsOwnAsShotEdits(){
        QStringList requested;const auto contexts=[&](const QString &path,bool metadata,QString *)->std::optional<CommandRegistry::LookContext>{
            if(metadata)requested<<path;
            return CommandRegistry::LookContext{path.endsWith(".ARW"),{{"sonyLook",QVariantMap{{"autoEligible",true},{"code",path.endsWith("first.ARW")?"FL":"PT"},{"parameters",QVariantMap{{"fade",2}}}}}}};};
        CommandSession result;QString error;
        const auto manual=QJsonObject{{"command","look.mode"},{"mode","manual"}};
        QVERIFY2(CommandSession::compile(manifest({photoImport("a","first.ARW"),manual,save("a.png"),photoImport("b","second.ARW"),manual,save("b.png")}),base(),contexts,&result,&error),qPrintable(error));
        QCOMPARE(requested,QStringList({base()+"/first.ARW",base()+"/second.ARW"}));QCOMPARE(result.exports[0].state.look.code,QString("FL"));QCOMPARE(result.exports[1].state.look.code,QString("PT"));
        const auto unavailable=[](const QString &,bool,QString *why)->std::optional<CommandRegistry::LookContext>{*why="offline";return {};};
        QVERIFY(!CommandSession::compile(manifest({photoImport(),save()}),base(),unavailable,&result,&error));QVERIFY(error.contains("offline"));
        QVERIFY(!CommandSession::compile(manifest({photoImport(),QJsonObject{{"command","look.mode"},{"mode","as_shot"}},save()}),base(),source(),&result));
    }
    void photoSelectionUsesSharedExactIdentityValidationAndPreservesCallerOutput(){
        CommandSession result;QString error;int reads=0;
        const auto context=[&](const QString &,bool,QString *)->std::optional<CommandRegistry::LookContext>{++reads;return CommandRegistry::LookContext{};};
        const auto select=QJsonObject{{"command","photo.select"},{"photo","A"}};
        QVERIFY(CommandSession::compile(manifest({photoImport("A"),ev(.5),photoImport("a","peer.png"),ev(-.5),select,save()}),base(),context,&result,&error));
        QCOMPARE(reads,2);QCOMPARE(result.exports[0].photo,QString("A"));QCOMPARE(result.exports[0].state.exposure,.5);
        const auto previous=result.exports[0].state.toJson();
        for(const auto &bad:QList<QJsonObject>{
            {{"command","photo.select"},{"photo",true}},{{"command","photo.select"},{"photo",""}},
            {{"command","photo.select"},{"photo",QString(32769,'a')}},{{"command","photo.select"},{"photo","A"},{"index",0}},
            {{"command","photo.select"},{"photo","a"}},{{"command","photo.select"},{"photo","0"}}}) {
            QVERIFY(!CommandSession::compile(manifest({photoImport("A"),save(),bad}),base(),context,&result,&error));
            QCOMPARE(result.exports[0].state.toJson(),previous);QCOMPARE(result.exports[0].photo,QString("A"));
        }
        QString key="unchanged";QVERIFY(!CommandRegistry::decodePhotoSelection({{"command","photo.select"},{"photo",1}},&key));QCOMPARE(key,QString("unchanged"));
        const auto description=CommandRegistry::schema()["workflow_commands"].toArray();
        QCOMPARE(describedCommand(description,"photo.select"),CommandRegistry::photoSelectionSchema());
        QCOMPARE(describedCommand(CommandSession::schema()["commands"].toArray(),"photo.select"),CommandRegistry::photoSelectionSchema());
    }
    void photoImportValidationSeparatesNativeCatalogPathsFromSessionAliasesWithoutReadingSources(){
        using Scope=CommandRegistry::ImportScope;CommandRegistry::PhotoImport request;QString error;
        const auto native=QJsonObject{{"command","photo.import"},{"source","relative.png"}};
        QVERIFY(CommandRegistry::decodePhotoImport(native,Scope::Desktop,&request));QCOMPARE(request.source,QString("relative.png"));QVERIFY(request.photo.isEmpty());
        QVERIFY(!CommandRegistry::decodePhotoImport(native,Scope::Session,&request));
        const auto transient=photoImport("a","relative.png");QVERIFY(CommandRegistry::decodePhotoImport(transient,Scope::Session,&request));QCOMPARE(request.photo,QString("a"));
        QVERIFY(!CommandRegistry::decodePhotoImport(transient,Scope::Desktop,&request));
        CommandSession previous;previous.photoCount=71;int reads=0;
        const auto context=[&](const QString &,bool,QString *)->std::optional<CommandRegistry::LookContext>{++reads;return CommandRegistry::LookContext{};};
        for(const auto &bad:QList<QJsonObject>{photoImport("a\n"),photoImport(" a"),photoImport(QString(65,'a')),photoImport("a",QString("bad\nsource")),
            QJsonObject{{"command","photo.import"},{"photo","a"},{"source",true}},QJsonObject{{"command","photo.import"},{"photo","a"},{"source","ok.png"},{"extra",1}}}) {
            request={"sentinel-source","sentinel-photo"};QVERIFY(!CommandRegistry::decodePhotoImport(bad,Scope::Session,&request));
            QCOMPARE(request.source,QString("sentinel-source"));QCOMPARE(request.photo,QString("sentinel-photo"));
            QVERIFY(!CommandSession::compile(manifest({bad,save()}),base(),context,&previous,&error));QCOMPARE(previous.photoCount,71);QCOMPARE(reads,0);
        }
        auto newline=native;newline["source"]="native\nname.png";
        QVERIFY(CommandRegistry::decodePhotoImport(newline,Scope::Desktop,&request));
        auto nul=native;nul["source"]=QString("bad")+QChar(u'\0')+"name.png";QVERIFY(!CommandRegistry::decodePhotoImport(nul,Scope::Desktop,&request));
        QVERIFY(CommandRegistry::decodePhotoImport(photoImport(QString(64,'a')),Scope::Session,&request));
        QCOMPARE(describedCommand(CommandSession::schema()["commands"].toArray(),"photo.import"),CommandRegistry::photoImportSchema(Scope::Session));
        const auto imports=describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),"photo.import");
        QCOMPARE(imports["desktop"].toObject(),CommandRegistry::photoImportSchema(Scope::Desktop));QCOMPARE(imports["session"].toObject(),CommandRegistry::photoImportSchema(Scope::Session));
    }
    void exportValidationSharesDefaultsAndScopeWhileRejectingOverrideInjectionAtomically(){
        using Scope=CommandRegistry::ExportScope;CommandRegistry::CurrentExport request;
        QVERIFY(CommandRegistry::decodeCurrentExport(save(),Scope::Session,&request));QCOMPARE(request.space,QString("srgb"));QCOMPARE(request.quality,92);
        const auto quality=QJsonObject{{"command","export.run"},{"destination","output.png"},{"space","display-p3"},{"quality",100}};
        QVERIFY(CommandRegistry::decodeCurrentExport(quality,Scope::Desktop,&request));QCOMPARE(request.quality,100);QVERIFY(!CommandRegistry::decodeCurrentExport(quality,Scope::Session,&request));
        CommandSession output;output.photoCount=71;
        for(const auto &bad:QList<QJsonObject>{quality,QJsonObject{{"command","export.run"},{"destination",true}},
            QJsonObject{{"command","export.run"},{"destination",""}},QJsonObject{{"command","export.run"},{"destination","bad\nname.png"}},
            QJsonObject{{"command","export.run"},{"destination","output.png"},{"space","unknown"}},QJsonObject{{"command","export.run"},{"destination","output.png"},{"overwrite",true}}}) {
            request={"sentinel","sentinel",7};QVERIFY(!CommandRegistry::decodeCurrentExport(bad,Scope::Session,&request));QCOMPARE(request.destination,QString("sentinel"));QCOMPARE(request.quality,7);
            QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),bad}),base(),source(),&output));QCOMPARE(output.photoCount,71);QVERIFY(output.exports.isEmpty());
        }
        auto native=save("native\nname.png");QVERIFY(CommandRegistry::decodeCurrentExport(native,Scope::Desktop,&request));
        const auto forms=describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),"export.run");
        QCOMPARE(forms["desktop"].toObject(),CommandRegistry::currentExportSchema(Scope::Desktop));QCOMPARE(forms["session"].toObject(),CommandRegistry::currentExportSchema(Scope::Session));
        QCOMPARE(describedCommand(CommandSession::schema()["commands"].toArray(),"export.run"),CommandRegistry::currentExportSchema(Scope::Session));
    }
    void desktopBatchExportHasExplicitSelectionAndCannotWidenOfflineSessions(){
        CommandRegistry::BatchExport request;const QJsonObject all{{"command","export.batch"},{"folder","out"}};
        QVERIFY(CommandRegistry::decodeBatchExport(all,&request));QVERIFY(request.all);QVERIFY(request.photos.isEmpty());QCOMPARE(request.quality,92);QCOMPARE(request.sequenceStart,1);
        auto selected=all;selected["photos"]=QJsonArray{"b","a"};selected["format"]="png";selected["sequenceStart"]=9;
        QVERIFY(CommandRegistry::decodeBatchExport(selected,&request));QVERIFY(!request.all);QCOMPARE(request.photos,QStringList({"b","a"}));QCOMPARE(request.sequenceStart,9);
        for(const auto &photos:QList<QJsonValue>{QJsonArray{},QJsonArray{"a","a"},QJsonArray{false},QString("all")}) {
            auto bad=all;bad["photos"]=photos;request.folder="sentinel";QVERIFY(!CommandRegistry::decodeBatchExport(bad,&request));QCOMPARE(request.folder,QString("sentinel"));
        }
        CommandSession result;result.photoCount=91;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),all}),base(),source(),&result));
        QCOMPARE(result.photoCount,91);QVERIFY(result.exports.isEmpty());
        QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),"export.batch"),CommandRegistry::batchExportSchema());
        QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),"export.batch").isEmpty());
    }
    void desktopTransferImportsRejectMalformedInputAndCannotRetireSessionSources(){
        CommandRegistry::PhotoTransfer request;
        for(const auto &name:QStringList{"photo.copy","photo.move"}) {
            const QJsonObject command{{"command",name},{"sources",QJsonArray{"a.png","b.png"}},{"folder","out"}};
            QVERIFY(CommandRegistry::decodePhotoTransfer(command,&request));QCOMPARE(request.move,name=="photo.move");QCOMPARE(request.sources,QStringList({"a.png","b.png"}));QCOMPARE(request.sequenceStart,1);
            for(const auto &sources:QList<QJsonValue>{QJsonArray{},QJsonArray{"a","a"},QJsonArray{true},QString("a.png")}) {
                auto bad=command;bad["sources"]=sources;request.folder="sentinel";QVERIFY(!CommandRegistry::decodePhotoTransfer(bad,&request));QCOMPARE(request.folder,QString("sentinel"));
            }
            CommandSession result;result.photoCount=73;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,73);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::photoTransferSchema(name=="photo.move"));
            QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void desktopCurationSharesCatalogNormalizationWithoutGrantingSessionWrites(){
        const QJsonObject command{{"command","catalog.curate"},{"target","selection"},{"field","keywords"},{"value","z, a,z"}};
        CommandRegistry::Curation request;QVERIFY(CommandRegistry::decodeCuration(command,&request));QVERIFY(request.selection);QCOMPARE(request.text,QString("a,z"));
        auto bad=command;bad["target"]="all";request.field="sentinel";QVERIFY(!CommandRegistry::decodeCuration(bad,&request));QCOMPARE(request.field,QString("sentinel"));
        CommandSession result;result.photoCount=61;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,61);QVERIFY(result.exports.isEmpty());
        QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),"catalog.curate"),CommandRegistry::curationSchema());
        QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),"catalog.curate").isEmpty());
    }
    void desktopVirtualVersionNamesAreStrictAndSessionsCannotMutateCatalogVersions(){
        CommandRegistry::VirtualVersion request;QVERIFY(CommandRegistry::decodeVirtualVersion({{"command","version.create"}},&request));QVERIFY(request.name.isEmpty());
        QVERIFY(CommandRegistry::decodeVirtualVersion({{"command","version.rename"},{"name"," Copy "}},&request));QCOMPARE(request.name,QString("Copy"));
        for(const auto &bad:QList<QJsonObject>{{{"command","version.create"},{"name",true}},{{"command","version.rename"},{"name"," "}},{{"command","version.remove"},{"name","Copy"}}}) {
            request.name="sentinel";QVERIFY(!CommandRegistry::decodeVirtualVersion(bad,&request));QCOMPARE(request.name,QString("sentinel"));
        }
        for(const auto &name:QStringList{"version.create","version.rename","version.remove"}) {
            QJsonObject command{{"command",name}};if(name=="version.rename")command["name"]="Copy";CommandSession result;result.photoCount=81;
            QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,81);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::virtualVersionSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void namedPresetCommandsValidateNamesWithoutOfflineStoreSideEffects(){
        CommandRegistry::Preset request;QVERIFY(CommandRegistry::decodePreset({{"command","preset.import"},{"source","preset.json"}},&request));QVERIFY(request.name.isEmpty());
        for(const auto &bad:QList<QJsonObject>{{{"command","preset.apply"},{"name"," A "}},{{"command","preset.rename"},{"name","A"},{"replacement",false}},{{"command","preset.export"},{"name","A"},{"destination",""}}}) {
            request.name="sentinel";QVERIFY(!CommandRegistry::decodePreset(bad,&request));QCOMPARE(request.name,QString("sentinel"));
        }
        for(const auto &name:QStringList{"preset.save","preset.apply","preset.remove","preset.rename","preset.replace","preset.export","preset.import"}) {
            QJsonObject command{{"command",name},{"name","A"}};if(name=="preset.rename")command["replacement"]="B";if(name=="preset.export")command["destination"]="preset.json";if(name=="preset.import")command["source"]="preset.json";
            QVERIFY(CommandRegistry::decodePreset(command,&request));CommandSession result;result.photoCount=82;
            QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,82);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::presetSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void controllerClipboardAndSelectionTransfersCannotEnterOfflineSessions(){
        CommandRegistry::Transfer request;QVERIFY(CommandRegistry::decodeAdjustmentTransfer({{"command","edit.paste"}},&request));QCOMPARE(request.groups,AdjustmentTransfer::allGroups());
        for(const auto &bad:QList<QJsonObject>{{{"command","edit.copy"},{"groups",QJsonArray{}}},{{"command","edit.paste"},{"groups",QJsonArray{"exposure","exposure"}}},{{"command","edit.sync"},{"target","current"}}}) {
            request.groups={"sentinel"};QVERIFY(!CommandRegistry::decodeAdjustmentTransfer(bad,&request));QCOMPARE(request.groups,QStringList{"sentinel"});
        }
        for(const auto &name:QStringList{"edit.copy","edit.paste","edit.sync"}) {
            QJsonObject command{{"command",name}};if(name=="edit.sync")command["target"]="all";QVERIFY(CommandRegistry::decodeAdjustmentTransfer(command,&request));
            CommandSession result;result.photoCount=83;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,83);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::adjustmentTransferSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void desktopProjectLifecycleCannotWriteCatalogsThroughOfflineSessions(){
        CommandRegistry::Project request;QVERIFY(CommandRegistry::decodeProject({{"command","project.create"},{"folder","parent"}},&request));QCOMPARE(request.name,QString("JixelLight Project"));
        QVERIFY(CommandRegistry::decodeProject({{"command","project.create"},{"folder","parent"},{"name"," Name "}},&request));QCOMPARE(request.name,QString("Name"));
        for(const auto &bad:QList<QJsonObject>{{{"command","project.create"},{"folder","parent"},{"name",".."}},{{"command","project.open"},{"path",true}}}){request.path="sentinel";QVERIFY(!CommandRegistry::decodeProject(bad,&request));QCOMPARE(request.path,QString("sentinel"));}
        for(const auto &name:QStringList{"project.create","project.open"}) {
            QJsonObject command{{"command",name},{name=="project.create"?"folder":"path","project.jlp"}};QVERIFY(CommandRegistry::decodeProject(command,&request));CommandSession result;result.photoCount=84;
            QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,84);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::projectSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void desktopOperationCancellationCannotReachQueuesFromOfflineSessions(){
        for(const auto &name:QStringList{"export.cancel","photo.transfer.cancel"}) {
            const QJsonObject command{{"command",name},{"operation_id","c59c39df-320d-4b8b-89ac-9307454d0208"}};
            CommandRegistry::OperationCancel request;QVERIFY(CommandRegistry::decodeOperationCancel(command,&request));
            CommandSession result;result.photoCount=88;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,88);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::operationCancelSchema(name=="photo.transfer.cancel"));
            QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void desktopProofActionsCannotAlterEarlierExportsOrSessionEditState(){
        for(const auto &command:QList<QJsonObject>{
            {{"command","proof.enable"},{"enabled",true}},{{"command","proof.target"},{"target","custom"}},
            {{"command","proof.profile"},{"source","owned.icc"}},{{"command","proof.intent"},{"intent",2}},
            {{"command","proof.paper"},{"enabled",true}},{{"command","proof.gamut-warning"},{"enabled",true}},
            {{"command","proof.black-point"},{"enabled",false}},{{"command","proof.refresh"}}}) {
            CommandRegistry::Proof request;QVERIFY(CommandRegistry::decodeProof(command,&request));
            CommandSession result;result.photoCount=87;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,87);QVERIFY(result.exports.isEmpty());
            const auto name=command["command"].toString();QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::proofSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void desktopRelinkActionsCannotRetargetCatalogsFromOfflineSessions(){
        for(const auto &command:QList<QJsonObject>{
            {{"command","photo.relink.request"},{"version","owned-version"},{"candidate","owned.bmp"},{"catalog_token","12"}},
            {{"command","photo.relink.confirm"},{"request_id","24"},{"catalog_token","12"},{"acknowledge_unknown_identity",true}},
            {{"command","photo.relink.cancel"},{"request_id","24"},{"catalog_token","12"}}}) {
            CommandRegistry::Relink request;QVERIFY(CommandRegistry::decodeRelink(command,&request));
            CommandSession result;result.photoCount=86;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,86);QVERIFY(result.exports.isEmpty());
            const auto name=command["command"].toString();QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::relinkSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void desktopXmpActionsCannotWriteSidecarsOrOpenReviewsFromOfflineSessions(){
        for(const auto &name:QStringList{"xmp.export","xmp.review"}) {
            const QJsonObject command{{"command",name},{name=="xmp.export"?"destination":"source","owned.xmp"}};
            CommandRegistry::Xmp request;QVERIFY(CommandRegistry::decodeXmp(command,&request));QCOMPARE(request.path,QString("owned.xmp"));
            CommandSession result;result.photoCount=85;QVERIFY(!CommandSession::compile(manifest({photoImport(),save(),command}),base(),source(),&result));QCOMPARE(result.photoCount,85);QVERIFY(result.exports.isEmpty());
            QCOMPARE(describedCommand(CommandRegistry::schema()["workflow_commands"].toArray(),name),CommandRegistry::xmpSchema(name));QVERIFY(describedCommand(CommandSession::schema()["commands"].toArray(),name).isEmpty());
        }
    }
    void nestedCommandsAndRetainedPayloadHaveIndependentBounds(){
        QJsonArray edits;for(int i=0;i<9999;++i)edits.append(ev(i%2));CommandSession result;QString error;
        QVERIFY(!CommandSession::compile(manifest({photoImport(),QJsonObject{{"command","edit.batch"},{"commands",edits}},save()}),base(),source(),&result,&error));QVERIFY(error.contains("command limit"));
        // A compact document can retain many distinct large brush snapshots.
        // It must stop at the separate state-payload budget, even below10000 commands.
        BrushMaskGeometry::Stroke stroke;stroke.dabs.resize(4096,{{.1234567890123,.8765432109876},.7654321098765});
        BrushMaskDocument::Document brush;brush.layers.push_back({"a",true,.7,{stroke,stroke,stroke,stroke}});
        QJsonObject document;QVERIFY(BrushMaskDocument::toJson(brush,&document));
        const auto replace=QJsonObject{{"command","mask.brush.replace"},{"document",document}};
        QVERIFY2(CommandSession::compile(manifest({photoImport(),replace,save()}),base(),source(),&result,&error),qPrintable(error));
        QJsonArray large{photoImport(),replace};for(int i=0;i<256;++i)large.append(save(QString("snapshot-%1.png").arg(i)));
        const auto previous=result.exports[0].state.toJson();
        QVERIFY(!CommandSession::compile(manifest(large),base(),source(),&result,&error));QVERIFY2(error.contains("state budget"),qPrintable(error));
        QCOMPARE(result.exports[0].state.toJson(),previous);
    }
};
QTEST_GUILESS_MAIN(CommandSessionTests)
#include "CommandSessionTests.moc"
