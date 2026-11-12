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
        const auto description=CommandRegistry::schema()["workflow_commands"].toArray();QCOMPARE(description.size(),1);
        QCOMPARE(description[0].toObject(),CommandRegistry::photoSelectionSchema());
        QCOMPARE(CommandSession::schema()["commands"].toArray()[1].toObject(),CommandRegistry::photoSelectionSchema());
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
