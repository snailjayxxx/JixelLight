#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include <QApplication>
#include <QClipboard>
#include <QMimeData>
#include <QInputMethodEvent>
#include <QDropEvent>
#include <QVariantMap>
#include <QQuickTextDocument>
#include <QTextDocument>
#include <QTextCursor>
#include <cstdio>
#include <QQuickWindow>
#include <QQuickItem>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QFile>
#include <QFileInfo>
#include <QDir>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QJsonDocument>
#include <QJsonArray>
#include <QMouseEvent>
#include <QKeySequence>
#include <qpa/qwindowsysteminterface.h>

namespace {
bool testClipboardIsIsolated() {
    return QGuiApplication::platformName()==QStringLiteral("offscreen") ||
        (qEnvironmentVariable("GITHUB_ACTIONS")==QStringLiteral("true") && qEnvironmentVariable("JIXELLIGHT_CI_ISOLATED_CLIPBOARD")==QStringLiteral("1"));
}
QQuickItem *findItem(QQuickItem *root,const QString &name) {
    if(!root)return nullptr;if(root->objectName()==name)return root;
    for(auto *child:root->childItems())if(auto *found=findItem(child,name))return found;
    return nullptr;
}
QByteArray hashFile(const QString &path) {
    QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};
    QCryptographicHash h(QCryptographicHash::Sha256);h.addData(&f);return h.result().toHex();
}
class RecipeClipboardRaceProxy final:public QObject {
    Q_OBJECT
    Q_PROPERTY(bool qaMutateAfterSnapshot READ qaMutateAfterSnapshot WRITE setQaMutateAfterSnapshot)
public:
    explicit RecipeClipboardRaceProxy(QObject *source,QObject *parent=nullptr):QObject(parent),original(source) {}
    bool qaMutateAfterSnapshot()const{return mutate;}
    void setQaMutateAfterSnapshot(bool value){mutate=value;}
    Q_INVOKABLE QVariantMap snapshot() {
        if(!testClipboardIsIsolated()||!original)return {{"status","unavailable"}};
        QVariantMap result;
        if(!QMetaObject::invokeMethod(original,"snapshot",Q_RETURN_ARG(QVariantMap,result)))return {{"status","unavailable"}};
        if(mutate&&result.value("status")==QStringLiteral("ok"))QGuiApplication::clipboard()->setText(QString(4194307,QChar(' ')));
        return result;
    }
private:
    QObject *original=nullptr;
    bool mutate=false;
};
class RecipeProbe final:public QObject {
public:
    RecipeProbe(PhotoController *controller,QQuickWindow *window,QString output):QObject(controller),c(controller),w(window),report(std::move(output)) {
        clock.start();originalProvider=w->property("recipeClipboardAdapter").value<QObject*>();provider=new RecipeClipboardRaceProxy(originalProvider,this);timer=new QTimer(this);timer->setInterval(40);
        dialog=w->findChild<QObject*>("editRecipeDialog");
        source=files.filePath("owned-recipe.png");
        QImage image(128,96,QImage::Format_RGBA64);image.fill(QColor(64,100,150));image.setColorSpace(QColorSpace(QColorSpace::SRgb));
        if(!originalProvider||!files.isValid()||!dialog||!image.save(source)||!QFileInfo(report).absoluteDir().mkpath(".")) {
            QTimer::singleShot(0,this,[this]{finish("Recipe probe setup failed");});return;
        }
        sourceHash=hashFile(source);w->resize(1180,720);c->setLanguage("en_US");c->setExactScopes(false);
        connect(timer,&QTimer::timeout,this,[this]{tick();});timer->start();
    }
private:
    PhotoController *c;QPointer<QQuickWindow>w;QPointer<QObject>dialog;RecipeClipboardRaceProxy *provider=nullptr;QObject *originalProvider=nullptr;QString report,source,project;
    QTemporaryDir files;QByteArray sourceHash;QTimer*timer=nullptr;QElapsedTimer clock;
    QJsonArray rows,captures,keys;QJsonObject before,applied;QVariantList history;int phase=0,settle=0,negativeIndex=0,focus=0;bool done=false,clipboardExercised=false,simulatedRaceObserved=false;
    QQuickItem *find(const QString &s)const{return w?findItem(w->contentItem(),s):nullptr;}
    bool visible()const{return dialog&&dialog->property("visible").toBool();}
    QJsonObject state()const{return c->gpuPlan(true).state.toJson();}
    bool unchanged()const{return state()==before&&c->editHistory()==history&&hashFile(source)==sourceHash;}
    void progress(const char *tag, int amount=-1) const {
        std::fprintf(stderr,"recipe-progress phase=%d elapsed_ms=%lld tag=%s amount=%d\n",phase,static_cast<long long>(clock.elapsed()),tag,amount);
        std::fflush(stderr);
    }
    void next(int n){phase=n;settle=0;progress("next");}
    bool click(QQuickItem *target) {
        progress("click-begin");
        if(!w||!target||!target->isVisible()||!target->isEnabled())return false;
        const QPointF p=target->mapToScene({target->width()/2,target->height()/2});
        for(auto kind:{QEvent::MouseButtonPress,QEvent::MouseButtonRelease}) {
            QMouseEvent e(kind,p,p,w->mapToGlobal(p),Qt::LeftButton,kind==QEvent::MouseButtonPress?Qt::LeftButton:Qt::NoButton,Qt::NoModifier);
            QCoreApplication::sendEvent(w,&e);
        }
        return true;
    }
    void key(int code,const QString &text={},Qt::KeyboardModifiers modifiers=Qt::NoModifier) {
        const bool consumed=QWindowSystemInterface::handleShortcutEvent(w,1000+keys.size(),code,modifiers,0,0,0,text);
        if(!consumed)QWindowSystemInterface::handleKeyEvent(w,2000+keys.size(),QEvent::KeyPress,code,modifiers,text);
        QWindowSystemInterface::handleKeyEvent(w,3000+keys.size(),QEvent::KeyRelease,code,modifiers,text);
        QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);
        keys.append(QJsonObject{{"key",code},{"modifiers",int(modifiers)},{"shortcut_consumed",consumed}});
    }
    bool record(const QString &name,bool good,QJsonObject extra={}) {
        progress("record",good ? 1 : 0);
        extra.insert("case",name);extra.insert("passed",good);extra.insert("dialog_visible",visible());
        extra.insert("state_exposure",c->exposure());extra.insert("history_size",c->editHistory().size());
        rows.append(extra);if(!good)finish(name);return good;
    }
    bool capture(const QString &label) {
        const auto path=report+"."+label+".png";const auto image=w->grabWindow();if(image.isNull()||!image.save(path))return false;
        captures.append(QJsonObject{{"label",label},{"path",path},{"sha256",QString::fromLatin1(hashFile(path))},{"width",image.width()},{"height",image.height()},{"actual_dpr",w->effectiveDevicePixelRatio()}});return true;
    }
    bool layout() {
        for(const auto &n:{"editRecipeText","editRecipeApply","editRecipeCancel"}) {
            auto *q=find(QString::fromLatin1(n));if(!q||!q->isVisible()||q->width()<1||q->height()<1)return false;
            const auto pos=q->mapToScene({0,0});if(pos.x()<0||pos.y()<0||pos.x()+q->width()>w->width()+1||pos.y()+q->height()>w->height()+1)return false;
        }
        return dialog->property("width").toDouble()<=w->width()-31&&dialog->property("height").toDouble()<=w->height()-39;
    }
    bool snapshotCases() {
        if(!testClipboardIsIsolated()){finish("Refuse clipboard test outside offscreen or isolated CI");return false;}
        auto *area=find("editRecipeText");auto *clipboard=QGuiApplication::clipboard();if(!area||!clipboard){finish("Snapshot test setup unavailable");return false;}
        const auto bindings=QKeySequence::keyBindings(QKeySequence::Paste);if(bindings.isEmpty()){finish("Snapshot paste binding unavailable");return false;}const auto seq=bindings.first()[0];
        const QString exact(1048576,QChar('x'));setText(QString{});area->forceActiveFocus();clipboard->setText(exact);
        progress("snapshot-exact-begin",exact.size());key(seq.key(),{},seq.keyboardModifiers());progress("snapshot-exact-end",exact.size());
        auto *note=find("editRecipePreviewNote");
        if(!record("snapshot-exact-1Mi-accepted",unchanged()&&dialog->property("recipeText").toString()==exact&&area->property("text").toString()==QString(4096,QChar('x'))&&note&&note->property("text").toString().contains("truncated")))return false;
        clipboardExercised=true;
        if(!capture("truncated-preview")){finish("Truncated preview capture failed");return false;}
        const QString over(1048577,QChar('x'));clipboard->setText(over);key(seq.key(),{},seq.keyboardModifiers());auto *error=find("editRecipeError");
        if(!record("snapshot-over-1Mi-rejected-preserving-previous",unchanged()&&dialog->property("recipeText").toString()==exact&&error&&error->property("text").toString()==QStringLiteral("The recipe text is too long.")))return false;
        const QString flood(1048576,QChar(0x00a0));clipboard->setText(flood);progress("snapshot-nbsp-flood-begin",flood.size());key(seq.key(),{},seq.keyboardModifiers());progress("snapshot-nbsp-flood-end",flood.size());
        const QString floodPreview=QStringLiteral("\\u00A0").repeated(4096);
        if(!record("preview-bounds-escaped-nbsp-flood",unchanged()&&dialog->property("recipeText").toString()==flood&&area->property("text").toString()==floodPreview&&note&&note->property("text").toString().contains("truncated")))return false;
        const QString boundary=QString(4095,QChar('a'))+QString::fromUtf8("😀")+QStringLiteral("b");clipboard->setText(boundary);key(seq.key(),{},seq.keyboardModifiers());
        if(!record("preview-truncation-keeps-surrogate-pair-intact",unchanged()&&dialog->property("recipeText").toString()==boundary&&area->property("text").toString()==QString(4095,QChar('a'))&&note&&note->property("text").toString().contains("truncated")))return false;
        const QString unicode=QString::fromUtf8("雪😀")+QChar(0x00a0)+QChar(0x2028)+QChar(0x2029);clipboard->setText(unicode);
        if(!click(find("editRecipePaste"))){finish("Paste button unavailable");return false;}
        const QString preview=area->property("text").toString();
        if(!record("button-snapshot-retains-unicode-escaped-preview",unchanged()&&dialog->property("recipeText").toString()==unicode&&preview==QString::fromUtf8("雪😀")+QStringLiteral("\\u00A0\\u2028\\u2029"),{{"snapshot",dialog->property("recipeText").toString()},{"preview",preview}}))return false;
        auto *binary=new QMimeData;binary->setData("application/octet-stream",QByteArray("owned"));clipboard->setMimeData(binary);area->forceActiveFocus();key(seq.key(),{},seq.keyboardModifiers());error=find("editRecipeError");const auto *mime=clipboard->mimeData();
        if(!record("snapshot-nontext-preserves-previous",unchanged()&&dialog->property("recipeText").toString()==unicode&&error&&error->property("text").toString()==QStringLiteral("Clipboard unavailable.")&&mime&&!mime->hasText()&&mime->data("application/octet-stream")==QByteArray("owned")))return false;
        area->forceActiveFocus();key(Qt::Key_X,"x");key(Qt::Key_Delete);
        if(!record("readonly-keyboard-cannot-mutate-snapshot",unchanged()&&dialog->property("recipeText").toString()==unicode&&area->property("text").toString()==preview))return false;
        QInputMethodEvent ime;ime.setCommitString(QStringLiteral("IME"));QCoreApplication::sendEvent(area,&ime);
        if(!record("readonly-direct-ime-cannot-mutate-snapshot",unchanged()&&dialog->property("recipeText").toString()==unicode&&area->property("text").toString()==preview,{{"event_accepted",ime.isAccepted()}}))return false;
        QMimeData drag;drag.setText(QStringLiteral("drop"));QDropEvent drop(QPointF(5,5),Qt::CopyAction,&drag,Qt::LeftButton,Qt::NoModifier);QCoreApplication::sendEvent(area,&drop);
        if(!record("readonly-direct-drop-cannot-mutate-snapshot",unchanged()&&dialog->property("recipeText").toString()==unicode&&area->property("text").toString()==preview,{{"event_accepted",drop.isAccepted()}}))return false;
        clipboard->setText(QStringLiteral("other"));const bool invoked=QMetaObject::invokeMethod(area,"paste");
        if(!record("readonly-direct-paste-cannot-mutate-snapshot",unchanged()&&invoked&&dialog->property("recipeText").toString()==unicode&&area->property("text").toString()==preview))return false;
        const QString invalid=QStringLiteral("[")+QChar(0x00a0)+QStringLiteral("{\"command\":\"develop.set\",\"parameter\":\"exposure\",\"value\":1.5}]");
        clipboard->setText(invalid);area->forceActiveFocus();key(seq.key(),{},seq.keyboardModifiers());const bool exactRaw=dialog->property("recipeText").toString()==invalid;
        const bool clicked=click(find("editRecipeApply"));error=find("editRecipeError");
        if(!record("invalid-nbsp-json-whitespace-rejected-atomically",exactRaw&&clicked&&visible()&&unchanged()&&error&&error->isVisible()&&!error->property("text").toString().isEmpty(),{{"snapshot",dialog->property("recipeText").toString()},{"error_text",error?error->property("text").toString():QString{}}}))return false;
        const QString carriage=QStringLiteral("[")+QChar(0x000d)+QStringLiteral("{\"command\":\"look.reset\"}]");
        clipboard->setText(carriage);
        if(!click(find("editRecipePaste"))){finish("Carriage-return paste button unavailable");return false;}
        const QString crPreview=area->property("text").toString();
        const QString crExpected=QStringLiteral("[\\u000D{\"command\":\"look.reset\"}]");
        if(!record("preview-carriage-return-visible",unchanged()&&dialog->property("recipeText").toString()==carriage&&crPreview==crExpected,{{"snapshot",dialog->property("recipeText").toString()},{"preview",crPreview},{"expected_preview",crExpected}}))return false;
        clipboard->clear();setText(QString{});return true;
    }
    bool paste(const QString &text) {
        if(!testClipboardIsIsolated()){finish("Refuse clipboard test outside offscreen or isolated CI");return false;}
        setText(QString{});auto *area=find("editRecipeText");auto *clipboard=QGuiApplication::clipboard();
        if(!area||!clipboard){finish("Offscreen clipboard unavailable");return false;}
        area->forceActiveFocus();provider->setQaMutateAfterSnapshot(text.size()==177);if(text.size()==177)w->setProperty("recipeClipboardAdapter",QVariant::fromValue<QObject*>(provider));progress("offscreen-clipboard-put-begin",text.size());clipboard->setText(text);progress("offscreen-clipboard-put-end",text.size());
        const auto bindings=QKeySequence::keyBindings(QKeySequence::Paste);if(bindings.isEmpty()){finish("Paste key binding unavailable");return false;}
        const auto seq=bindings.first()[0];progress("offscreen-paste-key-begin",text.size());key(seq.key(),{},seq.keyboardModifiers());progress("offscreen-paste-key-end",text.size());
        const QString actual=area->property("text").toString();const bool raceCorrect=text.size()!=177||clipboard->text().size()==4194307;const bool correct=(text.size()>1048576 ? actual.isEmpty() : actual==text)&&raceCorrect;clipboard->clear();provider->setQaMutateAfterSnapshot(false);w->setProperty("recipeClipboardAdapter",QVariant::fromValue<QObject*>(originalProvider));
        if(!correct){finish("Clipboard paste gate result mismatch");return false;}if(text.size()==177&&raceCorrect)simulatedRaceObserved=true;return true;
    }
    void setText(const QString &text){progress("setText-begin",text.size());dialog->setProperty("recipeText",text);progress("setText-end",text.size());}
    void open(){QMetaObject::invokeMethod(dialog,"open");}
    void finish(const QString &error) {
        if(done)return;done=true;if(timer)timer->stop();
        const bool passed=error.isEmpty()&&rows.size()==34&&hashFile(source)==sourceHash&&clock.elapsed()<60000;
        if(!passed)capture("failure");
        QJsonObject out{{"passed",passed},{"error",error},{"phase",phase},{"elapsed_ms",clock.elapsed()},{"deadline_ms",60000},
            {"actual_App_UI",true},{"owned_small_fixture_not_real_Sony",true},{"source_pixels",128*96},{"source_unchanged",hashFile(source)==sourceHash},
            {"source_sha256",QString::fromLatin1(sourceHash)},{"raw_compiled_stamp",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"settings_isolated",qApp->property("jixellightSmokeSettingsIsolated").toBool()},{"physical_OS_activation_verified",false},
            {"QPA_controlled_focus",true},{"focus_injections",focus},{"actual_dpr",w->effectiveDevicePixelRatio()},{"records",rows},{"captures",captures},{"key_events",keys},
            {"production_accepted",false},{"full_F6_registry_or_MCP_complete",false},{"clipboard_isolated_for_test",testClipboardIsIsolated()},{"native_qclipboard_tested",clipboardExercised&&QGuiApplication::platformName()!=QStringLiteral("offscreen")},{"offscreen_qclipboard_tested",clipboardExercised&&QGuiApplication::platformName()==QStringLiteral("offscreen")},{"physical_input_verified",false},{"actual_qpa",QGuiApplication::platformName()},{"candidate_production_bridge_integrated",true},{"qa_race_simulation_executed",simulatedRaceObserved},{"production_accepted",false}};
        QFile f(report);if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(out).toJson());
        qInfo().noquote()<<QString::fromUtf8(QJsonDocument(out).toJson(QJsonDocument::Compact));QCoreApplication::exit(passed?0:1);
    }
    void tick() {
        if(done||!w)return;if(clock.elapsed()>=60000){finish("Recipe GUI deadline");return;}
        if(qGuiApp->applicationState()!=Qt::ApplicationActive) {QWindowSystemInterface::handleApplicationStateChanged(Qt::ApplicationActive);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);++focus;return;}
        if(!w->isActive()){QWindowSystemInterface::handleFocusWindowChanged(w);QWindowSystemInterface::sendWindowSystemEvents(QEventLoop::AllEvents);++focus;return;}
        if(++settle<3)return;
        if(phase==0) {
            if(!c->importFile(QUrl::fromLocalFile(source))){finish("Import owned recipe fixture");return;}next(1);return;
        }
        if(!c->previewReady()||c->loading()||c->rendering())return;
        if(phase==1) {c->setExposure(.25);c->finishInteraction();next(2);return;}
        if(phase==2) {
            before=state();history=c->editHistory();
            if(!click(find("projectMenuButton"))){finish("Real project menu button unavailable");return;}next(3);return;
        }
        if(phase==3) {if(!click(find("editRecipeMenuAction"))){finish("Real recipe menu action unavailable");return;}next(4);return;}
        if(phase==4) {
            if(!visible()||!layout()||!record("actual-menu-opens-bounded-english-recipe",unchanged())||!capture("english-compact"))return;
            auto *text=find("editRecipeText");text->forceActiveFocus();key(Qt::Key_G,"g");key(Qt::Key_D,"d");key(Qt::Key_Space," ");key(Qt::Key_BracketLeft,"[");key(Qt::Key_BracketRight,"]");
            if(!record("readonly-preview-owns-navigation-and-canvas-keys",unchanged()&&visible()&&w->property("workspaceIndex").toInt()==1&&text->property("text").toString().isEmpty()&&dialog->property("recipeText").toString().isEmpty()))return;
            const auto seq=QKeySequence::keyBindings(QKeySequence::Undo).first()[0];key(seq.key(),{},seq.keyboardModifiers());
            if(!record("textfield-undo-preserves-photo-history",unchanged()&&visible()))return;
            if(!snapshotCases())return;
            next(5);return;
        }
        if(phase==5) {
            const QStringList names{"malformed-json","object-instead-of-array","empty-array","atomic-invalid-second-command","unknown-command-field","nonfinite-number","oversize-text","too-many-commands"};
            QString text;
            if(negativeIndex==0)text="[";
            if(negativeIndex==1)text="{}";
            if(negativeIndex==2)text="[]";
            if(negativeIndex==3)text=R"([{"command":"develop.set","parameter":"exposure","value":1.5},{"command":"unsupported"}])";
            if(negativeIndex==4)text=R"([{"command":"develop.set","parameter":"exposure","value":1.5,"unexpected":1}])";
            if(negativeIndex==5)text=R"([{"command":"develop.set","parameter":"exposure","value":1e309}])";
            if(negativeIndex==6)text=QString(4194305,QChar(' '))+"[]";
            if(negativeIndex==7){QJsonArray a;for(int i=0;i<10001;++i)a.append(QJsonObject{{"command","look.reset"}});text=QString::fromUtf8(QJsonDocument(a).toJson(QJsonDocument::Compact));}
            if(negativeIndex==6){if(!paste(text))return;} else setText(text);
            if(negativeIndex!=6&&!click(find("editRecipeApply"))){finish("Apply control unavailable for refusal");return;}
            const auto error=find("editRecipeError");
            if(negativeIndex==6&&(!error||error->property("text").toString()!=QStringLiteral("The recipe text is too long."))){finish("Oversize paste feedback missing");return;}
            if(!record(names[negativeIndex],unchanged()&&visible()&&error&&error->isVisible()&&!error->property("text").toString().isEmpty()))return;
            if(++negativeIndex<names.size()){settle=0;return;}
            if(!capture("invalid-command")){finish("Error capture");return;}next(6);return;
        }
        if(phase==6) {
            setText(R"([{"command":"develop.set","parameter":"exposure","value":1.5}])");
            if(!click(find("editRecipeCancel"))||!record("cancel-valid-recipe-is-readonly",unchanged()))return;open();next(7);return;
        }
        if(phase==7) {
            setText(R"([{"command":"develop.set","parameter":"exposure","value":1.5}])");key(Qt::Key_Escape);next(8);return;
        }
        if(phase==8) {if(!record("escape-valid-recipe-is-readonly",!visible()&&unchanged()))return;open();next(9);return;}
        if(phase==9) {
            if(!paste(R"([{"command":"develop.set","parameter":"exposure","value":0.75},{"command":"hsl.set","band":2,"component":"saturation","value":17},{"command":"geometry.rotate","quarterTurns":1}])"))return;
            if(!click(find("editRecipeApply"))){finish("Valid apply control unavailable");return;}next(10);return;
        }
        if(phase==10) {
            applied=state();
            if(!record("three-commands-apply-as-one-history-step",!visible()&&c->exposure()==.75&&c->editHistory().size()==history.size()+1&&applied!=before))return;
            if(!click(find("undoEditButton"))){finish("Actual Undo unavailable");return;}next(11);return;
        }
        if(phase==11) {
            if(!record("actual-undo-restores-entire-recipe",state()==before&&c->canRedo()))return;
            if(!click(find("redoEditButton"))){finish("Actual Redo unavailable");return;}next(12);return;
        }
        if(phase==12) {
            if(!record("actual-redo-restores-entire-recipe",state()==applied))return;
            open();next(13);return;
        }
        if(phase==13) {
            if(!c->createVirtualCopy("Recipe independent copy")){finish("Virtual copy target control");return;}next(14);return;
        }
        if(phase==14) {
            auto *apply=find("editRecipeApply"),*error=find("editRecipeError");
            if(!record("same-source-virtual-copy-invalidates-target",visible()&&apply&&!apply->isEnabled()&&error&&error->isVisible()&&state()==applied))return;
            if(!click(find("editRecipeCancel"))){finish("Stale recipe cancel unavailable");return;}
            c->setLanguage("zh_CN");open();next(15);return;
        }
        if(phase==15) {
            if(!layout()||!capture("chinese-compact")){finish("Chinese recipe layout");return;}
            setText(R"([{"command":"develop.set","parameter":"exposure","value":1.25}])");
            if(!click(find("editRecipeApply"))){finish("Chinese Apply unavailable");return;}next(16);return;
        }
        if(phase==16) {
            const bool copyChanged=c->exposure()==1.25;c->selectPhoto(0);
            if(!record("chinese-current-copy-edit-preserves-original",copyChanged&&state()==applied))return;
            if(!c->createProject(QUrl::fromLocalFile(files.path()),"RecipePersistence")||!c->flushEdits()){finish("Recipe persistence write");return;}
            project=c->projectPath();if(!c->openProject(QUrl::fromLocalFile(project))){finish("Recipe persistence reopen");return;}next(17);return;
        }
        if(phase==17) {
            if(!record("project-reopen-preserves-replayed-edits-and-undo",state()==applied&&c->canUndo()))return;
            before=state();history=c->editHistory();open();next(18);return;
        }
        if(phase==18) {
            if(!testClipboardIsIsolated()){finish("Refuse clipboard test outside offscreen or isolated CI");return;}
            QJsonArray commands;for(int i=0;i<9999;++i)commands.append(QJsonObject{{"command","look.reset"}});
            commands.append(QJsonObject{{"command","develop.set"},{"parameter","exposure"},{"value",1.75}});
            const QString batch=QString::fromUtf8(QJsonDocument(commands).toJson(QJsonDocument::Compact));
            auto *area=find("editRecipeText");auto *clipboard=QGuiApplication::clipboard();if(!area||!clipboard||batch.size()>1048576){finish("Maximum batch setup failed");return;}
            setText(QString{});area->forceActiveFocus();clipboard->setText(batch);const auto seq=QKeySequence::keyBindings(QKeySequence::Paste).first()[0];
            progress("max-batch-paste-begin",batch.size());key(seq.key(),{},seq.keyboardModifiers());progress("max-batch-paste-end",batch.size());
            const bool exact=dialog->property("recipeText").toString()==batch;clipboard->clear();
            if(!record("ten-thousand-command-snapshot-exact",unchanged()&&visible()&&exact,{{"commands",commands.size()},{"utf16_units",batch.size()}}))return;
            progress("max-batch-apply-begin",commands.size());if(!click(find("editRecipeApply"))){finish("Maximum batch Apply unavailable");return;}progress("max-batch-apply-end",commands.size());next(19);return;
        }
        if(phase==19) {
            if(!record("ten-thousand-commands-apply-one-history-step",!visible()&&c->exposure()==1.75&&c->editHistory().size()==history.size()+1&&state()!=before))return;
            if(!click(find("undoEditButton"))){finish("Maximum batch Undo unavailable");return;}next(20);return;
        }
        if(phase==20) {
            if(!record("ten-thousand-commands-undo-atomically",state()==before&&c->canRedo()))return;
            finish("");return;
        }
    }
};
}
void startEditRecipeClipboardSmokeRun(PhotoController *c,QQuickWindow *w,const QString &path){new RecipeProbe(c,w,path);}


#include "EditRecipeClipboardSmokeRun.moc"
