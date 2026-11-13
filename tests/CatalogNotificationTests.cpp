#include <QtTest>
#include <QCryptographicHash>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QScopeGuard>
#include <QJsonArray>
#include <QJsonDocument>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QUuid>
#include <thread>
#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/commands/NamedPresets.h"
#include "diagnostics/ActionTrace.h"

namespace {
QByteArray sourceHash(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
}

class CatalogNotificationTests final:public QObject {
    Q_OBJECT
    QTemporaryDir m_settings;
    static QJsonObject photoValue(const PhotoController::PhotoEntry &p) {
        return {{"path",p.path},{"state",p.state.toJson()},{"history",p.history.toJson()},{"rating",p.rating},{"flag",p.flag},
            {"keywords",QJsonArray::fromStringList(p.tags.keywords)},{"label",p.tags.label},{"albums",QJsonArray::fromStringList(p.tags.albums)},
            {"copy",p.copyKey},{"version",p.versionName},{"timeline",p.timeline.toJson()}};
    }
    static QMap<QString,QJsonObject> model(const PhotoController &c) {
        QMap<QString,QJsonObject> rows;for(const auto &p:c.m_photos)rows.insert(p.storageKey(),photoValue(p));return rows;
    }
    static QMap<QString,QJsonObject> stored(const QVector<ProjectDatabase::SavedPhoto> &saved) {
        QMap<QString,QJsonObject> rows;
        for(const auto &p:saved){const auto key=p.copyKey.isEmpty()?(p.sourceKey.isEmpty()?p.path:p.sourceKey):p.copyKey;
            rows.insert(key,{{"path",p.path},{"state",p.adjustments.toJson()},{"history",p.history.toJson()},{"rating",p.rating},{"flag",p.flag},
                {"keywords",QJsonArray::fromStringList(p.tags.keywords)},{"label",p.tags.label},{"albums",QJsonArray::fromStringList(p.tags.albums)},
                {"copy",p.copyKey},{"version",p.versionName},{"timeline",p.timeline.toJson()}});}
        return rows;
    }
    static QJsonObject workState(const PhotoController &c) {
        QJsonObject rows;const auto values=model(c);for(auto it=values.cbegin();it!=values.cend();++it)rows[it.key()]=it.value();
        QJsonArray jobs,timers;
        for(auto value:QList<quint64>{c.m_loader->sequence(),c.m_prepare->sequence(),c.m_render->sequence(),c.m_scopeJob->sequence(),c.m_inputScopeJob->sequence(),c.m_fullScopeJob->sequence(),c.m_plotJob->sequence(),c.m_referenceJob->sequence(),c.m_calibrationJob->sequence(),c.m_identityJob->sequence(),c.m_catalogDatesJob->sequence()})jobs.append(QString::number(value));
        for(auto *timer:{&c.m_saveTimer,&c.m_saveMaxTimer,&c.m_refineTimer,&c.m_exactTimer,&c.m_prefetchTimer,&c.m_catalogDateTimer,&c.m_plotTimer,&c.m_budgetRetryTimer,&c.m_brushPreviewRetryTimer,&c.m_brushPreviewScheduleTimer,&c.m_softProofDelay,&c.m_softProofDeadline,&c.m_diagnosticTimer})timers.append(timer->isActive());
        return {{"rows",rows},{"status",c.statusMessage()},{"language",c.language()},{"project",c.projectPath()},{"current",c.currentIndex()},
            {"selection",QJsonArray::fromVariantList(c.selectedIndices())},{"revision",QString::number(c.m_requestedRevision)},{"generation",QString::number(c.m_prepareGeneration)},
            {"photo",QString::number(c.m_photoEpoch)},{"catalog",QString::number(c.m_catalogEpoch)},{"clipboard",c.m_clipboard.toJson()},{"clipboard_name",c.m_clipboardName},
            {"clipboard_present",c.m_hasClipboard},{"jobs",jobs},{"timers",timers},{"relink_id",QString::number(c.m_nextRelinkId)},{"relink_busy",c.m_relinkBusy},{"relink_preview",QJsonObject::fromVariantMap(c.m_relinkPreview)},{"reference_revision",QString::number(c.m_referenceRevision)},{"dirty_edits",int(c.m_dirtyEdits.size())},{"dirty_history",int(c.m_dirtyHistories.size())},{"dirty_tags",int(c.m_dirtyTags.size())},{"dirty_curation",int(c.m_dirtyCuration.size())}};
    }
private slots:
    void initTestCase() {
        QVERIFY(m_settings.isValid());QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setApplicationName("JixelLightCatalogOwner-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        QSettings::setDefaultFormat(QSettings::IniFormat);QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,m_settings.path());
        QSettings settings;QVERIFY(settings.fileName().startsWith(m_settings.path()+"/"));
    }
    void clipboardNotificationAcceptedCloseKeepsTerminalStatus() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(64,48,QImage::Format_RGBA64);image.fill(QColor(80,120,160));
        const auto path=dir.filePath("clipboard.png");QVERIFY(image.save(path));const auto original=sourceHash(path);
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"ClipboardOwner"));c.setExposure(.75);c.finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);QVERIFY(c.flushEdits());
        bool notified=false,closed=false;QString stoppedStatus;quint64 stoppedRevision=0;
        QObject::connect(&c,&PhotoController::adjustmentClipboardChanged,&c,[&]{if(notified)return;notified=true;closed=c.beginClose();stoppedStatus=c.statusMessage();stoppedRevision=c.m_requestedRevision;});
        c.copyAdjustments();QVERIFY(notified);QVERIFY(closed);QVERIFY(c.m_closing);
        QCOMPARE(c.statusMessage(),stoppedStatus);QCOMPARE(c.m_requestedRevision,stoppedRevision);
        ProjectDatabase snapshot;QVector<ProjectDatabase::SavedPhoto> rows;QVERIFY(snapshot.readSnapshot(c.projectPath(),&rows));QCOMPARE(rows.size(),1);QCOMPARE(rows[0].adjustments.exposure,.75);QCOMPARE(sourceHash(path),original);
    }
    void publicationSignalsMayCloseOrDestroyOwner_data() {
        QTest::addColumn<QString>("action");QTest::addColumn<QString>("context");QTest::addColumn<bool>("closeOnly");
        const auto rows=[](const QString &action,const QStringList &contexts){for(const auto &context:contexts)for(bool closeOnly:{true,false})QTest::newRow(qPrintable(action+"-"+context+(closeOnly?"-close":"-destroy")))<<action<<context<<closeOnly;};
        rows("copy",{"clipboard","status"});for(const auto &action:{"sync-selection","sync-all"})rows(action,{"library","status"});
        rows("version-create",{"library","status"});rows("version-rename",{"library"});rows("version-remove",{"library","current","status"});
        for(const auto &target:{"current","selection"}){
            for(const auto &field:{"rating","flag"})rows(QString("curate-")+target+"-"+field,{"library","curation"});
            for(const auto &field:{"keywords","label","album_add","album_remove"})rows(QString("curate-")+target+"-"+field,{"library"});
        }
        rows("selection",{"library"});rows("preset-save",{"presets","status"});rows("preset-remove",{"presets"});rows("preset-replace",{"presets","status"});rows("preset-rename",{"presets"});
        rows("preset-import",{"presets","status"});rows("preset-import-named",{"presets","status"});rows("preset-export",{"status"});rows("preset-apply",{"history","status"});
        rows("language",{"language","activity","status"});rows("project-create",{"project","status","relink"});
        rows("project-open",{"history","project","library","current","curation","metadata","preview","gpu","scopes","adjustments","backend","plot","geometry","activity","reference","relink","status"});
    }
    void publicationSignalsMayCloseOrDestroyOwner() {
        QFETCH(QString,action);QFETCH(QString,context);QFETCH(bool,closeOnly);QTemporaryDir dir;QVERIFY(dir.isValid());
        const auto presetPath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("develop-presets.json");QVERIFY(presetPath.contains(QCoreApplication::applicationName()));QVERIFY(!QFileInfo::exists(presetPath));
        const auto presetCleanup=qScopeGuard([&]{QFile::remove(presetPath);});
        const auto a=dir.filePath("a.png"),b=dir.filePath("b.png");QImage image(64,48,QImage::Format_RGBA64);image.fill(QColor(80,120,160));QVERIFY(image.save(a));image.fill(QColor(160,80,120));QVERIFY(image.save(b));const auto hashA=sourceHash(a),hashB=sourceHash(b);
        ProcessedImageProvider provider;auto owner=std::make_unique<PhotoController>(&provider);owner->setGpuEnabled(false);QVERIFY(owner->importFile(QUrl::fromLocalFile(a)));QVERIFY(owner->importFile(QUrl::fromLocalFile(b)));
        owner->selectPhoto(1);owner->setExposure(-.5);owner->finishInteraction();owner->selectPhoto(0);owner->setExposure(.75);owner->finishInteraction();
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&owner->m_sourceIsFull&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);
        QVERIFY(owner->createProject(QUrl::fromLocalFile(dir.path()),"Before"));const auto initialProject=owner->projectPath();
        if(action.startsWith("sync-")){owner->selectPhoto(1);QVERIFY(owner->createVirtualCopy("Unselected"));owner->selectPhoto(0);QVERIFY(owner->setPhotoSelection({0,1}));}
        if(action=="version-rename"||action=="version-remove"){QVERIFY(owner->createVirtualCopy("Before"));}
        if(action.startsWith("curate-")){QVERIFY(owner->setPhotoSelection({0,1}));if(action.endsWith("album_remove"))QVERIFY(owner->addSelectionToAlbum("Album"));}
        const auto presetSource=dir.filePath("preset.jixelpreset.json"),presetOutput=dir.filePath("exported.jixelpreset.json");
        if(action.startsWith("preset-")&&action!="preset-save"){
            owner->setExposure(.25);owner->finishInteraction();QVERIFY(owner->saveNamedPreset("Before"));QVERIFY(owner->exportNamedPreset("Before",QUrl::fromLocalFile(presetSource)));owner->setExposure(.75);owner->finishInteraction();
            if(action=="preset-import")QVERIFY(owner->removeNamedPreset("Before"));
        }
        QString openedProject;
        if(action=="project-open"){
            QVERIFY(owner->createProject(QUrl::fromLocalFile(dir.path()),"Opened"));openedProject=owner->projectPath();owner->setExposure(1.25);owner->finishInteraction();QVERIFY(owner->flushEdits());QVERIFY(owner->openProject(QUrl::fromLocalFile(initialProject)));owner->selectPhoto(0);
        }
        QTRY_VERIFY_WITH_TIMEOUT(owner->brushReady()&&!owner->rendering()&&!owner->m_prepare->busy()&&!owner->m_render->busy(),15000);QVERIFY(owner->flushEdits());
        const auto targetKey=owner->m_photos[owner->m_currentIndex].storageKey();const auto presetHash=sourceHash(presetSource);const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("catalog_owner_marker",{{"id",marker}});
        QPointer<PhotoController> alive(owner.get());bool notified=false,closed=false,closeFinished=false;int afterCloseEvents=0;QJsonObject stopped;QMap<QString,QJsonObject> expected;QString project;
        const auto monitor=[&]{if(closeFinished)++afterCloseEvents;};
        for(auto signal:{&PhotoController::libraryChanged,&PhotoController::curationChanged,&PhotoController::historyChanged,&PhotoController::presetsChanged,&PhotoController::projectChanged,&PhotoController::currentIndexChanged,&PhotoController::currentMetadataChanged,&PhotoController::previewUrlChanged,&PhotoController::gpuFrameChanged,&PhotoController::scopesChanged,&PhotoController::adjustmentsChanged,&PhotoController::backendChanged,&PhotoController::scopePlotChanged,&PhotoController::previewGeometryChanged,&PhotoController::activityChanged,&PhotoController::referenceChanged,&PhotoController::relinkChanged,&PhotoController::languageChanged,&PhotoController::statusMessageChanged})QObject::connect(owner.get(),signal,owner.get(),monitor);
        const auto terminal=[&]{if(notified||!owner)return;notified=true;expected=model(*owner);project=owner->projectPath();ActionTrace::instance().record("catalog_owner_terminal",{{"id",marker}});
            if(closeOnly){closed=owner->beginClose();if(owner)stopped=workState(*owner);closeFinished=closed;}else owner.reset();};
        QMetaObject::Connection connection;
        if(context=="clipboard")connection=QObject::connect(owner.get(),&PhotoController::adjustmentClipboardChanged,owner.get(),terminal);
        else if(context=="library")connection=QObject::connect(owner.get(),&PhotoController::libraryChanged,owner.get(),terminal);
        else if(context=="curation")connection=QObject::connect(owner.get(),&PhotoController::curationChanged,owner.get(),terminal);
        else if(context=="history")connection=QObject::connect(owner.get(),&PhotoController::historyChanged,owner.get(),terminal);
        else if(context=="presets")connection=QObject::connect(owner.get(),&PhotoController::presetsChanged,owner.get(),terminal);
        else if(context=="project")connection=QObject::connect(owner.get(),&PhotoController::projectChanged,owner.get(),terminal);
        else if(context=="current")connection=QObject::connect(owner.get(),&PhotoController::currentIndexChanged,owner.get(),terminal);
        else if(context=="metadata")connection=QObject::connect(owner.get(),&PhotoController::currentMetadataChanged,owner.get(),terminal);
        else if(context=="preview")connection=QObject::connect(owner.get(),&PhotoController::previewUrlChanged,owner.get(),terminal);
        else if(context=="gpu")connection=QObject::connect(owner.get(),&PhotoController::gpuFrameChanged,owner.get(),terminal);
        else if(context=="scopes")connection=QObject::connect(owner.get(),&PhotoController::scopesChanged,owner.get(),terminal);
        else if(context=="adjustments")connection=QObject::connect(owner.get(),&PhotoController::adjustmentsChanged,owner.get(),terminal);
        else if(context=="backend")connection=QObject::connect(owner.get(),&PhotoController::backendChanged,owner.get(),terminal);
        else if(context=="plot")connection=QObject::connect(owner.get(),&PhotoController::scopePlotChanged,owner.get(),terminal);
        else if(context=="geometry")connection=QObject::connect(owner.get(),&PhotoController::previewGeometryChanged,owner.get(),terminal);
        else if(context=="activity")connection=QObject::connect(owner.get(),&PhotoController::activityChanged,owner.get(),terminal);
        else if(context=="reference")connection=QObject::connect(owner.get(),&PhotoController::referenceChanged,owner.get(),terminal);
        else if(context=="relink")connection=QObject::connect(owner.get(),&PhotoController::relinkChanged,owner.get(),terminal);
        else if(context=="language")connection=QObject::connect(owner.get(),&PhotoController::languageChanged,owner.get(),terminal);
        else connection=QObject::connect(owner.get(),&PhotoController::statusMessageChanged,owner.get(),terminal);
        const auto disconnect=qScopeGuard([&]{QObject::disconnect(connection);});bool accepted=true;int count=-1;QJsonObject command;
        if(action=="copy")command={{"command","edit.copy"}};
        else if(action.startsWith("sync-"))command={{"command","edit.sync"},{"groups",QJsonArray{"exposure"}},{"target",action=="sync-all"?"all":"selection"}};
        else if(action.startsWith("version-")){command={{"command","version."+action.mid(8)}};if(action!="version-remove")command["name"]="After";}
        else if(action.startsWith("curate-")){const auto parts=action.split('-');const auto field=parts[2];command={{"command","catalog.curate"},{"target",parts[1]},{"field",field},{"value",field=="rating"?QJsonValue(3):QJsonValue(QString(field=="flag"?"pick":field=="keywords"?"one, two":field=="label"?"blue":"Album"))}};}
        else if(action.startsWith("preset-")){
            const auto operation=action=="preset-import-named"?QString("import"):action.mid(7);command={{"command","preset."+operation}};
            if(operation=="import"){command["source"]=presetSource;if(action=="preset-import-named")command["name"]="Imported";}
            else{command["name"]=action=="preset-save"?"Saved":"Before";if(operation=="rename")command["replacement"]="Renamed";if(operation=="export")command["destination"]=presetOutput;}
        }else if(action=="project-create")command={{"command","project.create"},{"folder",dir.path()},{"name","Created"}};
        else if(action=="project-open")command={{"command","project.open"},{"path",openedProject}};
        if(action=="selection")accepted=owner->setPhotoSelection({1});
        else if(action=="language")owner->setLanguage(owner->language()=="en_US"?"zh_CN":"en_US");
        else if(action.startsWith("sync-")){count=owner->executeAdjustmentTransfer(command);accepted=count>=0;}
        else accepted=owner->executeCommand(command.toVariantMap());
        QVERIFY(accepted);QVERIFY(notified);if(action.startsWith("sync-"))QCOMPARE(count,action=="sync-all"?2:1);
        if(closeOnly){QVERIFY(closed);QVERIFY(owner&&alive&&owner->m_closing);QTRY_VERIFY_WITH_TIMEOUT(!owner->m_prepare->busy()&&!owner->m_render->busy()&&!owner->m_scopeJob->busy()&&!owner->m_inputScopeJob->busy()&&!owner->m_fullScopeJob->busy()&&!owner->m_plotJob->busy(),5000);QCOMPARE(workState(*owner),stopped);QCOMPARE(afterCloseEvents,0);}
        else{QVERIFY(!owner);QVERIFY(!alive);}
        ProjectDatabase snapshot;QVector<ProjectDatabase::SavedPhoto> saved;QVERIFY(snapshot.readSnapshot(project,&saved));QCOMPARE(stored(saved),expected);QCOMPARE(sourceHash(a),hashA);QCOMPARE(sourceHash(b),hashB);
        if(!presetHash.isEmpty())QCOMPARE(sourceHash(presetSource),presetHash);
        bool started=false,terminalSeen=false;int traced=0;
        for(const auto &value:ActionTrace::instance().snapshot()){const auto event=value.toObject();if(event["action"]=="catalog_owner_marker"&&event["id"]==marker)started=true;else if(event["action"]=="catalog_owner_terminal"&&event["id"]==marker)terminalSeen=true;
            else if(started&&((!command.isEmpty()&&event["command"].toObject()==command)||(action=="language"&&event["action"]=="language_changed"))){QVERIFY(!terminalSeen);++traced;if(action=="copy"||action.startsWith("preset-")||action=="version-rename"||action=="version-remove")QCOMPARE(event["photo_key"].toString(),targetKey);if(action=="version-create"){QVERIFY(event["photo_key"].toString().startsWith("jixel-copy:"));QVERIFY(expected.contains(event["photo_key"].toString()));}if(action.startsWith("sync-"))QCOMPARE(event["source_key"].toString(),targetKey);}}
        QVERIFY(started);QVERIFY(terminalSeen);QCOMPARE(traced,action=="selection"?0:1);
        if(action=="preset-save")QVERIFY(QFileInfo::exists(presetPath));if(action=="preset-export")QVERIFY(QFileInfo::exists(presetOutput));
        if(action.startsWith("preset-")){
            NamedPresets presets(presetPath);QString error;AdjustmentState state;QVERIFY(presets.load(&error));
            const auto name=action=="preset-save"?QString("Saved"):action=="preset-rename"?QString("Renamed"):action=="preset-import-named"?QString("Imported"):QString("Before");
            if(action=="preset-remove")QVERIFY(!presets.get(name,&state));else{QVERIFY(presets.get(name,&state));QCOMPARE(state.exposure,action=="preset-save"||action=="preset-replace"?.75:.25);}
        }
    }
    void foreignAndClosedEntryCannotChangeCatalogClipboardPresetsOrLanguage() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(64,48,QImage::Format_RGBA64);image.fill(Qt::gray);const auto path=dir.filePath("entries.png");QVERIFY(image.save(path));
        const auto presetPath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("develop-presets.json");QVERIFY(presetPath.contains(QCoreApplication::applicationName()));QVERIFY(!QFileInfo::exists(presetPath));const auto cleanup=qScopeGuard([&]{QFile::remove(presetPath);});
        ProcessedImageProvider provider;PhotoController c(&provider);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));QTRY_VERIFY_WITH_TIMEOUT(c.brushReady()&&c.m_sourceIsFull&&!c.rendering()&&!c.m_prepare->busy()&&!c.m_render->busy(),15000);
        QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Entries"));QVERIFY(c.flushEdits());const auto original=sourceHash(path);auto before=workState(c);QList<bool> accepted;int sync=0;
        const auto attempted=[&]{accepted={c.setPhotoSelection({}),c.createVirtualCopy("Foreign"),c.renameCurrentVirtualCopy("Foreign"),c.removeCurrentVirtualCopy(),c.saveNamedPreset("Foreign"),c.applyNamedPreset("Foreign"),c.removeNamedPreset("Foreign"),c.renameNamedPreset("Foreign","Other"),c.replaceNamedPreset("Foreign"),c.exportNamedPreset("Foreign",QUrl::fromLocalFile(dir.filePath("foreign.json"))),c.createProject(QUrl::fromLocalFile(dir.path()),"Foreign"),c.openProject(QUrl::fromLocalFile(c.projectPath())),c.executeCurationCommand({{"command","catalog.curate"},{"target","current"},{"field","rating"},{"value",5}})};
            c.copyAdjustments();sync=c.syncAdjustmentGroups({"exposure"},false);c.setLanguage(c.language()=="en_US"?"zh_CN":"en_US");};
        std::thread foreign(attempted);foreign.join();QVERIFY(std::none_of(accepted.begin(),accepted.end(),[](bool value){return value;}));QCOMPARE(sync,-1);QCOMPARE(workState(c),before);QVERIFY(!QFileInfo::exists(presetPath));QVERIFY(!QFileInfo::exists(dir.filePath("foreign.json")));QVERIFY(!QFileInfo::exists(dir.filePath("Foreign.jlp")));
        QVERIFY(c.beginClose());before=workState(c);attempted();QVERIFY(std::none_of(accepted.begin(),accepted.end(),[](bool value){return value;}));QCOMPARE(sync,-1);QCOMPARE(workState(c),before);QCOMPARE(sourceHash(path),original);
    }
    void reentrantCopyKeepsChronologicalFrozenTargets() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(64,48,QImage::Format_RGBA64);image.fill(Qt::gray);const auto a=dir.filePath("a.png"),b=dir.filePath("b.png");QVERIFY(image.save(a));QVERIFY(image.save(b));
        PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(a)));QVERIFY(c.importFile(QUrl::fromLocalFile(b)));c.setExposure(.75);c.selectPhoto(1);c.setExposure(-.5);c.selectPhoto(0);
        const auto first=c.library()[0].toMap()["id"].toString(),second=c.library()[1].toMap()["id"].toString();const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("nested_copy_marker",{{"id",marker}});bool nested=false;
        const auto connection=QObject::connect(&c,&PhotoController::adjustmentClipboardChanged,&c,[&]{if(nested)return;nested=true;c.selectPhoto(1);c.copyAdjustments();});c.copyAdjustments();QObject::disconnect(connection);
        QVERIFY(nested);QCOMPARE(c.m_clipboard.exposure,-.5);QCOMPARE(c.adjustmentClipboardName(),QString("b.png"));QStringList targets;bool after=false;
        for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="nested_copy_marker"&&row["id"]==marker)after=true;else if(after&&row["action"]=="copy_adjustments")targets.append(row["photo_key"].toString());}QCOMPARE(targets,QStringList({first,second}));
    }
    void reentrantPresetRecordsOriginalPhotoBeforeNestedReplacement() {
        QTemporaryDir dir;QVERIFY(dir.isValid());const auto presetPath=QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("develop-presets.json");QVERIFY(presetPath.contains(QCoreApplication::applicationName()));QVERIFY(!QFileInfo::exists(presetPath));const auto cleanup=qScopeGuard([&]{QFile::remove(presetPath);});
        QImage image(64,48,QImage::Format_RGBA64);image.fill(Qt::gray);const auto a=dir.filePath("a.png"),b=dir.filePath("b.png");QVERIFY(image.save(a));QVERIFY(image.save(b));PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(a)));QVERIFY(c.importFile(QUrl::fromLocalFile(b)));c.setExposure(.75);c.selectPhoto(1);c.setExposure(-.5);c.selectPhoto(0);
        const auto first=c.library()[0].toMap()["id"].toString(),second=c.library()[1].toMap()["id"].toString();const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("nested_preset_marker",{{"id",marker}});bool nested=false,replaced=false;
        const auto connection=QObject::connect(&c,&PhotoController::presetsChanged,&c,[&]{if(nested)return;nested=true;c.selectPhoto(1);replaced=c.replaceNamedPreset("Saved");});const bool accepted=c.saveNamedPreset("Saved");QObject::disconnect(connection);QVERIFY(accepted);QVERIFY(nested);QVERIFY(replaced);
        NamedPresets presets(presetPath);QString error;AdjustmentState state;QVERIFY(presets.load(&error));QVERIFY(presets.get("Saved",&state));QCOMPARE(state.exposure,-.5);QStringList targets,commands;bool after=false;
        for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="nested_preset_marker"&&row["id"]==marker)after=true;else if(after&&row["action"]=="preset_command"){targets.append(row["photo_key"].toString());commands.append(row["command"].toObject()["command"].toString());}}QCOMPARE(targets,QStringList({first,second}));QCOMPARE(commands,QStringList({"preset.save","preset.replace"}));
    }
    void reentrantProjectOpenKeepsBothAcceptedCatalogTargets() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(64,48,QImage::Format_RGBA64);image.fill(Qt::gray);const auto path=dir.filePath("project.png");QVERIFY(image.save(path));PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));c.setExposure(.75);c.finishInteraction();QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"First"));const auto first=c.projectPath();c.setExposure(1.25);c.finishInteraction();QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Second"));const auto second=c.projectPath();QVERIFY(c.flushEdits());QVERIFY(c.openProject(QUrl::fromLocalFile(first)));
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("nested_project_marker",{{"id",marker}});bool nested=false,reopened=false;
        const auto connection=QObject::connect(&c,&PhotoController::projectChanged,&c,[&]{if(nested)return;nested=true;reopened=c.openProject(QUrl::fromLocalFile(first));});const bool accepted=c.openProject(QUrl::fromLocalFile(second));QObject::disconnect(connection);QVERIFY(accepted);QVERIFY(nested);QVERIFY(reopened);QCOMPARE(c.projectPath(),first);QCOMPARE(c.exposure(),.75);QStringList targets;bool after=false;
        for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="nested_project_marker"&&row["id"]==marker)after=true;else if(after&&row["action"]=="project_command")targets.append(row["command"].toObject()["path"].toString());}QCOMPARE(targets,QStringList({second,first}));
    }
    void reentrantVirtualCreationKeepsNewerSelectionAndBothVersions() {
        QTemporaryDir dir;QVERIFY(dir.isValid());QImage image(64,48,QImage::Format_RGBA64);image.fill(Qt::gray);const auto path=dir.filePath("versions.png");QVERIFY(image.save(path));PhotoController c(nullptr);c.setGpuEnabled(false);QVERIFY(c.importFile(QUrl::fromLocalFile(path)));c.setExposure(.75);c.finishInteraction();QVERIFY(c.createProject(QUrl::fromLocalFile(dir.path()),"Versions"));
        const auto marker=QUuid::createUuid().toString(QUuid::WithoutBraces);ActionTrace::instance().record("nested_version_marker",{{"id",marker}});bool nested=false,created=false;
        const auto connection=QObject::connect(&c,&PhotoController::libraryChanged,&c,[&]{if(nested)return;nested=true;created=c.createVirtualCopy("Nested");});const bool accepted=c.createVirtualCopy("Outer");QObject::disconnect(connection);QVERIFY(accepted);QVERIFY(nested);QVERIFY(created);QCOMPARE(c.library().size(),3);QCOMPARE(c.m_photos[c.m_currentIndex].versionName,QString("Nested"));QVERIFY(c.flushEdits());
        ProjectDatabase snapshot;QVector<ProjectDatabase::SavedPhoto> saved;QVERIFY(snapshot.readSnapshot(c.projectPath(),&saved));QCOMPARE(stored(saved),model(c));QStringList names,keys;bool after=false;
        for(const auto &value:ActionTrace::instance().snapshot()){const auto row=value.toObject();if(row["action"]=="nested_version_marker"&&row["id"]==marker)after=true;else if(after&&row["action"]=="virtual_copy_created"){names.append(row["version"].toString());keys.append(row["photo_key"].toString());}}QCOMPARE(names,QStringList({"Outer","Nested"}));QCOMPARE(keys.size(),2);QVERIFY(keys[0]!=keys[1]);
    }
};
QTEST_MAIN(CatalogNotificationTests)
#include "CatalogNotificationTests.moc"
