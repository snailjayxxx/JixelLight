#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "diagnostics/PerformanceRecorder.h"
#include "diagnostics/LoggingEngine.h"
#include <QCoreApplication>
#include <QQuickWindow>
#include <QQuickItem>
#include <QTimer>
#include <QSaveFile>
#include <QJsonDocument>
#include <QJSValue>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <exiv2/exiv2.hpp>
#include <memory>
#include <cmath>

namespace {
void resizeForSmoke(QQuickWindow *window,int width,int height) {
    window->resize(width,height);
    // The offscreen platform can change native geometry without notifying
    // QML size bindings. Publish the actual dimensions in this test helper;
    // regular desktop resize notifications may already have arrived.
    window->widthChanged(window->width());
    window->heightChanged(window->height());
}
QQuickItem *visualChild(QQuickItem *item, const QString &name) {
    if (!item) return nullptr;
    if (item->objectName()==name) return item;
    for (auto *child : item->childItems()) if (auto *found=visualChild(child,name)) return found;
    return nullptr;
}
int historyCursor(const PhotoController *controller) {
    const auto history=controller->editHistory();
    for (int i=0;i<history.size();++i) if (history[i].toMap()["current"].toBool()) return i;
    return -1;
}
QVariantMap objectMap(QObject *object,const char *property) {
    if (!object) return {};
    const auto value=object->property(property);
    return value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
}
bool clickItem(QQuickWindow *window,QQuickItem *item) {
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    const auto point=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    const auto global=window->mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,point,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,point,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&release); return true;
}
bool revealItem(QQuickItem *item) {
    if (!item) return false;
    for (auto *parent=item->parentItem();parent;parent=parent->parentItem()) {
        if (parent->metaObject()->indexOfProperty("contentY")<0) continue;
        const auto y=item->mapToItem(parent,QPointF(0,item->height()/2)).y();
        const double maximum=std::max(0.0,parent->property("contentHeight").toDouble()-parent->height());
        parent->setProperty("contentY",std::clamp(parent->property("contentY").toDouble()+y-parent->height()/2,0.0,maximum));
        return true;
    }
    return false;
}
bool clickSlider(QQuickWindow *window,QQuickItem *item,double fraction) {
    if (!item || !item->isVisible() || !item->isEnabled()) return false;
    const auto point=item->mapToScene(QPointF(item->width()*fraction,item->height()/2));
    const auto global=window->mapToGlobal(point.toPoint());
    QMouseEvent press(QEvent::MouseButtonPress,point,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&press);
    QMouseEvent release(QEvent::MouseButtonRelease,point,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&release); return true;
}
}

void startDiagnosticSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath) {
    struct State { QElapsedTimer elapsed; int phase=0; quint64 revision=0; qint64 sourceKey=0; QSize size; QJsonObject adjustments; bool finished=false; };
    const auto state=std::make_shared<State>();state->elapsed.start();
    const auto fixture=std::make_shared<QTemporaryDir>();
    auto *timer=new QTimer(controller);timer->setInterval(50);
    const auto finish=[=](const QString &path,const QString &error) {
        if(state->finished)return;
        state->finished=true;timer->stop();
        const bool gpuRequired=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool passed=!path.isEmpty()&&error.isEmpty()&&state->elapsed.elapsed()<15000&&(!gpuRequired||controller->gpuActive());
        const QJsonObject report{{"passed",passed},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"engine",ProcessingPlan::EngineVersion},{"elapsed_ms",state->elapsed.elapsed()},
            {"gpu_active",controller->gpuActive()},{"backend",controller->processingBackend()},
            {"bundle",path},{"error",error},{"parameter_revision",qint64(state->revision)},
            {"source_cache_key",QString::number(state->sourceKey)},
            {"width",state->size.width()},{"height",state->size.height()},{"adjustments",state->adjustments}};
        QSaveFile output(reportPath);
        const auto data=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(data)==data.size()&&output.commit();
        QCoreApplication::exit(passed&&saved?0:1);
    };
    QObject::connect(controller,&PhotoController::diagnosticFinished,timer,[=](const QString &path){finish(path,{});});
    QObject::connect(timer,&QTimer::timeout,controller,[=] {
        if(!fixture->isValid()){finish({},QStringLiteral("Diagnostic fixture unavailable"));return;}
        if(state->elapsed.elapsed()>=15000){finish({},QStringLiteral("Diagnostic integration timed out"));return;}
        const bool ready=controller->previewReady()&&!controller->loading()&&!controller->rendering()&&!controller->gpuSource().isNull();
        if(!ready)return;
        if(state->phase==0) {
            controller->setExposure(.4);controller->setVignetteAmount(-.75);state->phase=1;
        } else if(state->phase==1) {
            if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!controller->gpuActive())return;
            state->revision=controller->renderRevision();state->sourceKey=controller->gpuSource().cacheKey();
            state->size=controller->gpuSource().size();state->adjustments=controller->gpuPlan(true).state.toJson();
            state->phase=2;
            if(!controller->requestBugReport())finish({},QStringLiteral("Diagnostic request refused"));
        }
    });
    window->setProperty("workspaceIndex",1);
    QImage image(64,48,QImage::Format_RGB32);
    for(int y=0;y<48;++y)for(int x=0;x<64;++x)image.setPixel(x,y,qRgb(32+x*3,16+y*4,48+(x+y)%128));
    const QString source=fixture->filePath("diagnostic-owned.png");
    if(!fixture->isValid()||!image.save(source)||!controller->importFile(QUrl::fromLocalFile(source)))
        finish({},QStringLiteral("Cannot create diagnostic fixture"));
    else timer->start();
}

void startSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath, const QString &screenshotPath) {
    struct State { QElapsedTimer elapsed; int phase=0, edits=0; quint64 croppedScopePixels=0, straightenedScopePixels=0; double straightenDegrees=0; bool straightenPassed=false; bool lookEnabled=false; bool gridVisited=false, filmstripPresent=false, presetActionsPresent=false, restoredDevelop=false, curationPassed=false, catalogPassed=false, catalogDatesPassed=false, copiesPassed=false, historyPassed=false, geometryPassed=false, interactiveCropPassed=false; };
    auto state=std::make_shared<State>();state->elapsed.start();
    auto cropTrace=std::make_shared<QJsonObject>();
    auto scopePlots=std::make_shared<QJsonObject>();
    auto copyTrace=std::make_shared<QJsonObject>();
    auto transferTrace=std::make_shared<QJsonObject>();
    auto correctionTrace=std::make_shared<QJsonObject>();
    auto vignetteTrace=std::make_shared<QJsonObject>();
    auto exportNamesTrace=std::make_shared<QJsonObject>();
    auto importFixture=std::make_shared<QTemporaryDir>();
    auto *timer=new QTimer(controller);timer->setInterval(50);
    QObject::connect(timer,&QTimer::timeout,controller,[=] {
        const bool ready=controller->previewReady() && !controller->loading() && !controller->rendering();
        bool complete=false;
        if(state->phase==0 && ready) {
            // Every alpha.8 RAW smoke exercises the new look and independent reference UI.
            state->lookEnabled=controller->currentIsRaw();
            if(state->lookEnabled) {
                controller->setLookCode("FL");controller->setLookParameter("sharpness",5);controller->setLookParameter("clarity",4);
                controller->setLookParameter("fade",2);controller->setShowCameraReference(true);
            }
            state->phase=1;
        }
        else if(state->phase==1) {
            controller->setExposure((state->edits%11-5)/10.0);
            controller->setSaturation(state->edits%25);
            controller->setColorMix(5,1,state->edits%19-9);
            ++state->edits;
            if(controller->library().size()>1 && (state->edits==10 || state->edits==20)) controller->selectPhoto(state->edits==10?1:0);
            if(state->edits>=30) {
                controller->setExposure(.4);controller->setSaturation(15);controller->setContrast(10);
                controller->finishInteraction();state->phase=2;
            }
        } else if(state->phase==2 && ready) {
            // Exercise new curation metadata without changing the RAW pixels.
            controller->setRating(4);
            controller->setFlag(QStringLiteral("pick"));
            state->curationPassed = controller->currentRating() == 4
                && controller->currentFlag() == QStringLiteral("pick");
            state->filmstripPresent = window->findChild<QQuickItem *>(QStringLiteral("jixelMainFilmstrip")) != nullptr;
            // Verify that the new Library page can be instantiated while
            // preserving the current photo, then return to the GPU canvas.
            const auto exposure = controller->exposure();
            state->catalogPassed = controller->setPhotoSelection({controller->currentIndex()})
                && controller->setSelectionKeywords("fusion, smoke")
                && controller->addSelectionToAlbum("Smoke validation")
                && controller->addSelectionToAlbum("Second album")
                && controller->setSelectionLabel("blue")
                && controller->exposure() == exposure;
            resizeForSmoke(window,1180,720);
            window->setProperty("workspaceIndex", 0);
            state->phase=20;
        } else if(state->phase==20) {
            auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"));
            auto *grid = window->findChild<QQuickItem *>(QStringLiteral("libraryPhotoGrid"));
            state->gridVisited = workspace && grid;
            if (state->gridVisited) {
                workspace->setProperty("filterMode", 1); // Picks
                state->curationPassed = state->curationPassed && grid->property("count").toInt() == 1;
            }
            if (workspace) {
                workspace->setProperty("searchText", "SMOKE");
                workspace->setProperty("filterAlbum", "Smoke validation");
                workspace->setProperty("filterLabel", "blue");
                workspace->setProperty("sortMode", 2);
            }
            state->phase=201;
        } else if(state->phase==201) {
            auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"));
            auto *grid = window->findChild<QQuickItem *>(QStringLiteral("libraryPhotoGrid"));
            state->catalogPassed = state->catalogPassed && workspace && grid
                && grid->property("count").toInt() == 1 && grid->height() > 100;
            const auto albumFilter = window->findChild<QQuickItem *>(QStringLiteral("catalogAlbumFilter"));
            state->catalogPassed = state->catalogPassed && albumFilter && albumFilter->property("count").toInt() == 3;
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath + ".library.png");
            if (workspace) workspace->setProperty("searchText", "no-matching-photo-xyz");
            state->phase=202;
        } else if(state->phase==202) {
            auto *grid = window->findChild<QQuickItem *>(QStringLiteral("libraryPhotoGrid"));
            state->catalogPassed = state->catalogPassed && grid && grid->property("count").toInt() == 0;
            const auto originalIndex = controller->currentIndex();
            const auto originalCount = controller->library().size();
            const auto originalExposure = controller->exposure();
            const auto source = controller->currentFile();
            state->copiesPassed = controller->createVirtualCopy("Smoke version") && !controller->canUndo()
                && controller->currentFile() == source;
            state->copiesPassed = state->copiesPassed && controller->renameCurrentVirtualCopy("Smoke renamed")
                && controller->library()[controller->currentIndex()].toMap().value("versionName").toString() == "Smoke renamed"
                && controller->createVirtualCopy("Temporary smoke version") && controller->removeCurrentVirtualCopy()
                && controller->library().size() == originalCount+1;
            const auto copyIndex = controller->currentIndex();
            controller->setExposure(-.6); controller->finishInteraction();
            controller->selectPhoto(originalIndex);
            state->copiesPassed = state->copiesPassed && controller->exposure() == originalExposure;
            controller->selectPhoto(copyIndex);
            state->copiesPassed = state->copiesPassed && controller->exposure() == -.6;
            controller->setExposure(originalExposure); controller->finishInteraction();
            if (auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"))) {
                workspace->setProperty("filterMode",0); workspace->setProperty("searchText",QString());
                workspace->setProperty("filterAlbum",QString()); workspace->setProperty("filterLabel","none"); workspace->setProperty("sortMode",4);
            }
            state->phase=203;
        } else if(state->phase==203 || state->phase==204) {
            auto *workspace = window->findChild<QQuickItem *>(QStringLiteral("libraryWorkspace"));
            const auto rows = workspace ? workspace->property("visiblePhotos").value<QJSValue>().toVariant().toList() : QVariantList{};
            const bool sorted = !rows.isEmpty() && rows[0].toMap().value("index").toInt() == controller->currentIndex();
            if (state->phase==203) {
                state->catalogDatesPassed = sorted;
                if (workspace) workspace->setProperty("sortMode",5);
                state->phase=204; return;
            }
            state->catalogDatesPassed = state->catalogDatesPassed && sorted;
            for (const auto &row : controller->library()) state->catalogDatesPassed = state->catalogDatesPassed && row.toMap().value("captureChecked").toBool();
            const auto sort = window->findChild<QQuickItem *>(QStringLiteral("catalogSortMode"));
            state->catalogDatesPassed = state->catalogDatesPassed && sort && sort->property("count").toInt() == 6;
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath + ".versions.png");
            resizeForSmoke(window,1540,920);
            window->setProperty("workspaceIndex", 1);
            state->phase=21;
        } else if(state->phase==21) {
            if(auto *canvas=window->findChild<QQuickItem *>(QStringLiteral("photoCanvas"))) {
                canvas->setProperty("zoom", 1.0);
                state->restoredDevelop=true;
                const auto actions=window->findChild<QQuickItem *>(QStringLiteral("namedPresetActions"));
                if (actions) QMetaObject::invokeMethod(actions,"clicked",Qt::DirectConnection);
                state->phase=211;
            }
        } else if(state->phase==211) {
            const auto menu=window->findChild<QObject *>(QStringLiteral("namedPresetMenu"));
            state->presetActionsPresent=menu && menu->property("visible").toBool();
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath + ".presets.png");
            if (menu) QMetaObject::invokeMethod(menu,"close",Qt::DirectConnection);
            state->phase=22;
        } else if(state->phase==22 && ready) {
            const double originalExposure = controller->exposure();
            controller->setExposure(1.2); controller->finishInteraction();
            controller->undo();
            state->historyPassed = std::abs(controller->exposure()-originalExposure)<1e-9 && controller->canRedo();
            controller->redo();
            state->historyPassed = state->historyPassed && std::abs(controller->exposure()-1.2)<1e-9;
            controller->undo();
            controller->rotatePhoto(1);
            state->phase=23;
        } else if(state->phase==23 && ready) {
            state->geometryPassed = controller->geometry().value("quarterTurns").toInt()==1;
            const auto button=window->findChild<QQuickItem *>(QStringLiteral("beginInteractiveCrop"));
            if (button) QMetaObject::invokeMethod(button,"clicked",Qt::DirectConnection);
            state->interactiveCropPassed=controller->cropEditing();
            cropTrace->insert("began",controller->cropEditing());
            state->phase=231;
        } else if(state->phase==231 && ready) {
            const auto overlay=window->findChild<QQuickItem *>(QStringLiteral("interactiveCropOverlay"));
            // Repeater delegates live in the visual tree, which can differ
            // from QObject ownership used by findChild.
            const auto handle=visualChild(overlay,QStringLiteral("cropHandle4"));
            state->interactiveCropPassed=state->interactiveCropPassed && overlay && handle && overlay->isVisible();
            cropTrace->insert("overlay",overlay!=nullptr); cropTrace->insert("handle",handle!=nullptr);
            cropTrace->insert("visible",overlay && overlay->isVisible());
            if (handle && overlay) {
                // Full-frame corner centers lie on the canvas clip edge. Hit
                // the visible interior of the handle, as a real pointer would.
                const auto origin=handle->mapToScene(QPointF(handle->width()/2-3,handle->height()/2-3));
                const auto destination=origin-QPointF(overlay->width()/4,overlay->height()/4);
                cropTrace->insert("origin_x",origin.x()); cropTrace->insert("origin_y",origin.y());
                auto send=[&](QEvent::Type type,QPointF position,Qt::MouseButton button,Qt::MouseButtons buttons) {
                    QMouseEvent event(type,position,window->mapToGlobal(position.toPoint()),button,buttons,Qt::NoModifier);
                    QCoreApplication::sendEvent(window,&event);
                };
                send(QEvent::MouseButtonPress,origin,Qt::LeftButton,Qt::LeftButton);
                send(QEvent::MouseMove,destination,Qt::NoButton,Qt::LeftButton);
                send(QEvent::MouseButtonRelease,destination,Qt::LeftButton,Qt::NoButton);
            }
            state->phase=232;
        } else if(state->phase==232 && ready) {
            const auto overlay=window->findChild<QQuickItem *>(QStringLiteral("interactiveCropOverlay"));
            const auto selection=overlay ? overlay->property("selection").toRectF() : QRectF{};
            cropTrace->insert("width",selection.width()); cropTrace->insert("height",selection.height());
            state->interactiveCropPassed=state->interactiveCropPassed && selection.width()>.5 && selection.width()<.99
                && selection.height()>.5 && selection.height()<.99;
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".crop.png");
            const auto apply=window->findChild<QQuickItem *>(QStringLiteral("applyInteractiveCrop"));
            if (apply) QMetaObject::invokeMethod(apply,"clicked",Qt::DirectConnection);
            state->interactiveCropPassed=state->interactiveCropPassed && !controller->cropEditing();
            cropTrace->insert("applied",!controller->cropEditing());
            state->phase=233;
        } else if(state->phase==233 && ready) {
            state->interactiveCropPassed=state->interactiveCropPassed && controller->geometry().value("width").toDouble()<1;
            controller->undo();
            state->interactiveCropPassed=state->interactiveCropPassed && controller->geometry().value("width").toDouble()==1
                && controller->geometry().value("quarterTurns").toInt()==1;
            controller->setCrop(0,0,.5,1);
            controller->setExactScopes(true);
            state->phase=24;
        } else if(state->phase==24 && ready) {
            const auto meta=controller->currentMetadata();
            const auto expected=(meta.value("pixelWidth").toULongLong()+1)/2 * meta.value("pixelHeight").toULongLong();
            if(controller->scopesPixelCount()==expected) {
                state->croppedScopePixels=controller->scopesPixelCount();
                state->geometryPassed = state->geometryPassed && controller->geometry().value("width").toDouble()==.5;
                controller->undo(); // crop
                controller->undo(); // rotation
                state->geometryPassed = state->geometryPassed && controller->geometry().value("quarterTurns").toInt()==0
                    && controller->geometry().value("width").toDouble()==1;
                state->phase=25;
            }
        } else if(state->phase==25 && ready) {
            auto *slider=window->findChild<QQuickItem *>(QStringLiteral("straightenAngle"));
            if (slider && slider->isVisible() && slider->isEnabled()) {
                const auto point=slider->mapToScene(QPointF(slider->width()*.56,slider->height()/2));
                const auto global=window->mapToGlobal(point.toPoint());
                QMouseEvent press(QEvent::MouseButtonPress,point,point,global,Qt::LeftButton,Qt::LeftButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&press);
                QMouseEvent release(QEvent::MouseButtonRelease,point,point,global,Qt::LeftButton,Qt::NoButton,Qt::NoModifier);
                QCoreApplication::sendEvent(window,&release);
            }
            controller->finishInteraction();
            state->straightenDegrees=controller->geometry().value("straighten").toDouble();
            state->straightenPassed=std::abs(state->straightenDegrees)>0 && std::abs(state->straightenDegrees)<15;
            state->phase=26;
        } else if(state->phase==26 && ready) {
            const auto meta=controller->currentMetadata(); GeometryState g; g.straighten=state->straightenDegrees;
            const auto size=g.straightenedSize({meta.value("pixelWidth").toInt(),meta.value("pixelHeight").toInt()});
            if (controller->scopesPixelCount()==quint64(size.width())*size.height()) {
                state->straightenedScopePixels=controller->scopesPixelCount();
                if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".straighten.png");
                controller->undo(); state->straightenPassed=state->straightenPassed && !controller->geometry().contains("straighten");
                state->phase=3;
            }
        } else if(state->phase==3 && ready) { controller->setExactScopes(true);state->phase=4; }
        else if(state->phase==4 && ready) {
            const auto meta=controller->currentMetadata();
            if (controller->scopesPixelCount()==meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong()) {
                controller->setScopeMode("waveform"); state->phase=41;
            }
        } else if ((state->phase==41 || state->phase==42 || state->phase==43) && ready && controller->scopePlotCurrent()) {
            const auto meta=controller->currentMetadata();
            const auto expected=meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong();
            const auto view=window->findChild<QQuickItem *>(QStringLiteral("scopePlotView"));
            const auto mode=controller->scopeMode();
            scopePlots->insert(mode,view && view->isVisible() && !controller->scopePlotUrl().isEmpty() && controller->scopePlotPixels()==expected);
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+"."+mode+".png");
            if (state->phase==41) controller->setScopeMode("parade");
            else if (state->phase==42) controller->setScopeMode("vectorscope");
            else controller->setScopeMode("histogram");
            ++state->phase;
        } else if (state->phase==44 && ready) {
            auto *menu=window->findChild<QObject *>(QStringLiteral("importActionsMenu"));
            copyTrace->insert("menu_opened",menu && QMetaObject::invokeMethod(menu,"open"));
            state->phase=45;
        } else if (state->phase==45 && ready) {
            auto *menu=window->findChild<QObject *>(QStringLiteral("importActionsMenu"));
            auto *item=window->findChild<QQuickItem *>(QStringLiteral("copyImportAction"));
            copyTrace->insert("menu_visible",menu && menu->property("visible").toBool() && item && item->isVisible() && item->isEnabled());
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".import-menu.png");
            if (menu) QMetaObject::invokeMethod(menu,"close");
            QImage fixture(64,48,QImage::Format_RGB32); fixture.fill(QColor("#658797"));
            const bool prepared=importFixture->isValid() && QDir(importFixture->path()).mkdir("copies") && fixture.save(importFixture->filePath("copy-test.png"));
            bool datedPrepared=fixture.save(importFixture->filePath("dated-test.jpg"));
            try {
                auto image=Exiv2::ImageFactory::open(importFixture->filePath("dated-test.jpg").toStdString());
                if (!image.get()) datedPrepared=false;
                else { image->readMetadata(); auto exif=image->exifData(); exif["Exif.Photo.DateTimeOriginal"]="2025:01:02 03:04:05";
                    image->setExifData(exif); image->writeMetadata(); }
            } catch (...) { datedPrepared=false; }
            copyTrace->insert("dated_prepared",datedPrepared);
            copyTrace->insert("before",controller->library().size());
            copyTrace->insert("prepared",prepared);
            controller->copyImportRequested({QUrl::fromLocalFile(importFixture->filePath("copy-test.png"))});
            state->phase=451;
        } else if (state->phase==451 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("copyImportDialog"));
            copyTrace->insert("dialog_visible",dialog && dialog->property("visible").toBool());
            if (dialog) {
                dialog->setProperty("customNames",false);
                copyTrace->insert("custom_clicked",clickItem(window,visualChild(window->contentItem(),"copyImportCustomNames")));
                dialog->setProperty("pattern",QString("session_{seq:4}_{name}"));
                dialog->setProperty("sequenceStart",7);
                dialog->setProperty("destination",QUrl::fromLocalFile(importFixture->filePath("copies")));
                const auto value=dialog->property("preview");
                const auto preview=value.canConvert<QJSValue>() ? value.value<QJSValue>().toVariant().toMap() : value.toMap();
                const auto rows=preview["rows"].toList();
                copyTrace->insert("preview_valid",preview["valid"].toBool() && rows.size()==1 && rows[0].toMap()["destination"].toString()=="session_0007_copy-test.png");
            }
            state->phase=452;
        } else if (state->phase==452 && ready) {
            if (auto *dialog=window->findChild<QObject *>(QStringLiteral("copyImportDialog"))) {
                dialog->setProperty("sources",QVariantList{QUrl::fromLocalFile(importFixture->filePath("dated-test.jpg"))});
                dialog->setProperty("pattern",QString("{capture_date}_{capture_time}_{seq:4}_{name}"));
            }
            state->phase=4521;
        } else if (state->phase==4521 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("copyImportDialog"));
            const auto preview=objectMap(dialog,"preview");
            if (preview["pending"].toBool() && state->elapsed.elapsed()<60000) return;
            const auto rows=preview["rows"].toList();
            copyTrace->insert("capture_preview",preview["valid"].toBool() && rows.size()==1
                && rows[0].toMap()["destination"].toString()=="20250102_030405_0007_dated-test.jpg");
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".import-dialog.png");
            copyTrace->insert("language",controller->language()); controller->setLanguage("en_US");
            resizeForSmoke(window,1180,720);
            state->phase=453;
        } else if (state->phase==453 && ready) {
            if (auto *dialog=window->findChild<QObject *>(QStringLiteral("copyImportDialog"))) {
                const auto parent=dialog->property("parent").value<QQuickItem *>();
                copyTrace->insert("dialog_x",dialog->property("x").toDouble());
                copyTrace->insert("dialog_y",dialog->property("y").toDouble());
                copyTrace->insert("parent_width",parent ? parent->width() : -1);
                copyTrace->insert("parent_height",parent ? parent->height() : -1);
                copyTrace->insert("window_width",window->width());
                copyTrace->insert("window_height",window->height());
                copyTrace->insert("host_width",dialog->property("hostWidth").toDouble());
                copyTrace->insert("host_height",dialog->property("hostHeight").toDouble());
                copyTrace->insert("qml_window_width",window->property("width").toDouble());
                copyTrace->insert("dialog_centered",std::abs(dialog->property("x").toDouble()-(window->width()-dialog->property("width").toDouble())/2)<=1
                    && std::abs(dialog->property("y").toDouble()-(window->height()-dialog->property("height").toDouble())/2)<=1);
            }
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".import-dialog-en.png");
            if (auto *dialog=window->findChild<QObject *>(QStringLiteral("copyImportDialog")))
                dialog->setProperty("sources",QVariantList{QUrl::fromLocalFile(importFixture->filePath("copy-test.png"))});
            state->phase=4531;
        } else if (state->phase==4531 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("copyImportDialog"));
            const auto preview=objectMap(dialog,"preview");
            if (preview["pending"].toBool() && state->elapsed.elapsed()<60000) return;
            auto *apply=visualChild(window->contentItem(),"copyImportApply");
            copyTrace->insert("missing_capture_blocked",!preview["valid"].toBool() && !preview["error"].toString().isEmpty()
                && apply && !apply->isEnabled() && !controller->copyImportBusy());
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".import-dialog-missing-date.png");
            if (dialog) dialog->setProperty("sources",QVariantList{QUrl::fromLocalFile(importFixture->filePath("dated-test.jpg"))});
            state->phase=4532;
        } else if (state->phase==4532 && ready) {
            const auto preview=objectMap(window->findChild<QObject *>(QStringLiteral("copyImportDialog")),"preview");
            if (preview["pending"].toBool() && state->elapsed.elapsed()<60000) return;
            copyTrace->insert("started",clickItem(window,visualChild(window->contentItem(),"copyImportApply")));
            controller->setLanguage(copyTrace->value("language").toString()); resizeForSmoke(window,1540,920); state->phase=46;
        } else if (state->phase==46 && ready && !controller->copyImportBusy()) {
            QFile source(importFixture->filePath("dated-test.jpg")),destination(importFixture->filePath("copies/20250102_030405_0007_dated-test.jpg"));
            copyTrace->insert("content_equal",source.open(QIODevice::ReadOnly) && destination.open(QIODevice::ReadOnly) && source.readAll()==destination.readAll());
            copyTrace->insert("catalog_added",controller->library().size()==copyTrace->value("before").toInt()+1);
            transferTrace->insert("source_index",controller->currentIndex());
            transferTrace->insert("source_exposure",controller->exposure());
            controller->setPhotoSelection({controller->currentIndex(),controller->library().size()-1});
            transferTrace->insert("opened",clickItem(window,window->findChild<QQuickItem *>(QStringLiteral("openTransferSync"))));
            state->phase=47;
        } else if (state->phase==47 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("adjustmentTransferDialog"));
            auto *geometry=visualChild(window->contentItem(),QStringLiteral("transferGroup_geometry"));
            transferTrace->insert("default_geometry_excluded",geometry && !geometry->property("checked").toBool());
            transferTrace->insert("visible",dialog && dialog->property("visible").toBool() && dialog->property("targetCount").toInt()==1);
            transferTrace->insert("none_clicked",clickItem(window,visualChild(window->contentItem(),QStringLiteral("transferSelectNone"))));
            transferTrace->insert("exposure_clicked",clickItem(window,visualChild(window->contentItem(),QStringLiteral("transferGroup_exposure"))));
            state->phase=48;
        } else if (state->phase==48 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".sync-dialog.png");
            transferTrace->insert("language",controller->language());
            controller->setLanguage("en_US"); resizeForSmoke(window,1180,720); state->phase=481;
        } else if (state->phase==481 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".sync-dialog-en.png");
            transferTrace->insert("applied",clickItem(window,visualChild(window->contentItem(),QStringLiteral("transferApplyButton"))));
            controller->setLanguage(transferTrace->value("language").toString()); resizeForSmoke(window,1540,920);
            state->phase=49;
        } else if (state->phase==49 && ready) {
            auto *dialog=window->findChild<QObject *>(QStringLiteral("adjustmentTransferDialog"));
            transferTrace->insert("closed",dialog && !dialog->property("visible").toBool());
            controller->selectPhoto(controller->library().size()-1);
            const auto expected=transferTrace->value("source_exposure").toDouble();
            const bool changed=controller->exposure()==expected && controller->saturation()==0 && controller->canUndo();
            controller->undo(); const bool undone=controller->exposure()==0; controller->redo();
            transferTrace->insert("isolated_undo",changed && undone && controller->exposure()==expected);
            // Exercise the new manual controls on the imported PNG. The
            // existing RAW/full-resolution checks and 60 s deadline stay intact.
            state->phase=27;
        } else if (state->phase==27 && ready) {
            correctionTrace->insert("source_is_raw",controller->currentIsRaw());
            correctionTrace->insert("before",QJsonObject::fromVariantMap(controller->geometry()));
            correctionTrace->insert("history_size",controller->editHistory().size());
            correctionTrace->insert("toggle_revealed",revealItem(window->findChild<QQuickItem *>("geometryCorrectionsToggle")));
            state->phase=271;
        } else if (state->phase==271 && ready) {
            const auto slider=visualChild(window->contentItem(),"geometry_perspectiveHorizontal");
            correctionTrace->insert("opened",(slider && slider->isVisible()) || clickItem(window,window->findChild<QQuickItem *>("geometryCorrectionsToggle")));
            state->phase=272;
        } else if (state->phase==272 && ready) {
            bool controls=true;
            for (const auto *key : {"perspectiveHorizontal","perspectiveVertical","distortion","redCa","blueCa"})
                controls=controls && visualChild(window->contentItem(),QString("geometry_")+key);
            correctionTrace->insert("controls_found",controls);
            correctionTrace->insert("perspective_revealed",revealItem(visualChild(window->contentItem(),"geometry_perspectiveHorizontal")));
            state->phase=273;
        } else if (state->phase==273 && ready) {
            correctionTrace->insert("perspective_clicked",clickSlider(window,visualChild(window->contentItem(),"geometry_perspectiveHorizontal"),.57));
            correctionTrace->insert("perspective_changed",std::abs(controller->geometry()["perspectiveHorizontal"].toDouble())>.005);
            state->phase=274;
        } else if (state->phase==274 && ready) {
            correctionTrace->insert("ca_revealed",revealItem(visualChild(window->contentItem(),"geometry_redCa")));
            state->phase=275;
        } else if (state->phase==275 && ready) {
            correctionTrace->insert("ca_clicked",clickSlider(window,visualChild(window->contentItem(),"geometry_redCa"),.67));
            correctionTrace->insert("ca_changed",std::abs(controller->geometry()["redCa"].toDouble())>.05);
            const bool values=controller->setGeometryAdjustment("perspectiveVertical",-.04)
                && controller->setGeometryAdjustment("distortion",.08) && controller->setGeometryAdjustment("blueCa",-.8);
            controller->finishInteraction(); correctionTrace->insert("other_values_set",values); state->phase=276;
        } else if (state->phase==276 && ready) {
            const auto g=GeometryState::fromJson(QJsonObject::fromVariantMap(controller->geometry())); const auto meta=controller->currentMetadata();
            const auto size=g.correctedSize({meta["pixelWidth"].toInt(),meta["pixelHeight"].toInt()});
            if (controller->scopesPixelCount()==quint64(size.width())*size.height()
                && (controller->scopesStatus().contains("Full-resolution statistics") || controller->scopesStatus().contains(QStringLiteral("全分辨率统计")))) {
                correctionTrace->insert("full_scopes",true); correctionTrace->insert("scope_pixels",qint64(controller->scopesPixelCount()));
                correctionTrace->insert("settings",g.toJson());
                if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".corrections.png");
                correctionTrace->insert("language",controller->language()); controller->setLanguage("en_US"); resizeForSmoke(window,1180,720); state->phase=277;
            }
        } else if (state->phase==277 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".corrections-en.png");
            controller->setLanguage(correctionTrace->value("language").toString()); resizeForSmoke(window,1540,920);
            const int edits=controller->editHistory().size()-correctionTrace->value("history_size").toInt();
            for (int i=0;i<edits;++i) controller->undo();
            correctionTrace->insert("undo_restored",edits>0 && QJsonObject::fromVariantMap(controller->geometry())==correctionTrace->value("before").toObject());
            if (auto *toggle=window->findChild<QQuickItem *>("geometryCorrectionsToggle")) QMetaObject::invokeMethod(toggle,"clicked");
            state->phase=28;
        } else if (state->phase==28 && ready) {
            vignetteTrace->insert("before",controller->gpuPlan(true).state.toJson());
            vignetteTrace->insert("before_cursor",historyCursor(controller));
            vignetteTrace->insert("source_is_raw",controller->currentIsRaw());
            vignetteTrace->insert("toggle_revealed",revealItem(window->findChild<QQuickItem *>("vignetteToggle")));
            state->phase=281;
        } else if (state->phase==281 && ready) {
            const auto slider=visualChild(window->contentItem(),"vignetteAmountSlider");
            vignetteTrace->insert("opened",(slider && slider->isVisible()) || clickItem(window,window->findChild<QQuickItem *>("vignetteToggle")));
            state->phase=282;
        } else if (state->phase==282 && ready) {
            bool controls=true;
            for (const auto *name : {"vignetteAmountSlider","vignetteMidpointSlider","vignetteFeatherSlider"})
                controls=controls && visualChild(window->contentItem(),name);
            vignetteTrace->insert("controls_found",controls);
            vignetteTrace->insert("amount_revealed",revealItem(visualChild(window->contentItem(),"vignetteAmountSlider")));
            state->phase=283;
        } else if (state->phase==283 && ready) {
            vignetteTrace->insert("amount_clicked",clickSlider(window,visualChild(window->contentItem(),"vignetteAmountSlider"),.2));
            vignetteTrace->insert("amount_changed",controller->vignetteAmount()<-.5);
            vignetteTrace->insert("midpoint_revealed",revealItem(visualChild(window->contentItem(),"vignetteMidpointSlider")));
            state->phase=284;
        } else if (state->phase==284 && ready) {
            vignetteTrace->insert("midpoint_clicked",clickSlider(window,visualChild(window->contentItem(),"vignetteMidpointSlider"),.35));
            vignetteTrace->insert("midpoint_changed",std::abs(controller->vignetteMidpoint()-.5)>.05);
            vignetteTrace->insert("feather_revealed",revealItem(visualChild(window->contentItem(),"vignetteFeatherSlider")));
            state->phase=285;
        } else if (state->phase==285 && ready) {
            vignetteTrace->insert("feather_clicked",clickSlider(window,visualChild(window->contentItem(),"vignetteFeatherSlider"),.65));
            vignetteTrace->insert("feather_changed",controller->vignetteFeather()<.9);
            controller->finishInteraction(); state->phase=286;
        } else if (state->phase==286 && ready) {
            const auto meta=controller->currentMetadata(); const auto expected=meta["pixelWidth"].toULongLong()*meta["pixelHeight"].toULongLong();
            if (expected>0 && controller->scopesPixelCount()==expected
                && (controller->scopesStatus().contains("Full-resolution statistics") || controller->scopesStatus().contains(QStringLiteral("全分辨率统计")))) {
                vignetteTrace->insert("full_scopes",true); vignetteTrace->insert("scope_pixels",qint64(expected));
                vignetteTrace->insert("active",controller->gpuPlan(true).state.toJson());
                if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".vignette.png");
                vignetteTrace->insert("language",controller->language()); controller->setLanguage("en_US"); resizeForSmoke(window,1180,720); state->phase=287;
            }
        } else if (state->phase==287 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".vignette-en.png");
            vignetteTrace->insert("reset_revealed",revealItem(window->findChild<QQuickItem *>("vignetteReset"))); state->phase=288;
        } else if (state->phase==288 && ready) {
            const auto before=vignetteTrace->value("before").toObject(); const auto active=vignetteTrace->value("active").toObject();
            vignetteTrace->insert("reset_clicked",clickItem(window,window->findChild<QQuickItem *>("vignetteReset")));
            const auto reset=controller->gpuPlan(true).state.toJson();
            const bool isolated=!reset.contains("vignette") && reset["exposure"]==before["exposure"] && reset["geometry"]==before["geometry"];
            controller->undo(); const bool undo=controller->gpuPlan(true).state.toJson()==active;
            controller->redo(); const bool redo=controller->gpuPlan(true).state.toJson()==reset;
            vignetteTrace->insert("isolated_reset_undo_redo",isolated && undo && redo);
            const int cursor=vignetteTrace->value("before_cursor").toInt(-1);
            while (controller->canUndo() && historyCursor(controller)>cursor) controller->undo();
            vignetteTrace->insert("undo_restored",controller->gpuPlan(true).state.toJson()==before && historyCursor(controller)==cursor);
            controller->setLanguage(vignetteTrace->value("language").toString()); resizeForSmoke(window,1540,920);
            if (auto *toggle=window->findChild<QQuickItem *>("vignetteToggle")) QMetaObject::invokeMethod(toggle,"clicked");
            revealItem(window->findChild<QQuickItem *>("straightenAngle"));
            state->phase=289;
        } else if (state->phase==289 && ready) {
            // The actual Copy test now publishes a dated JPEG. Keep an
            // independent undated fixture for the export refusal check.
            const auto before=controller->library().size();
            controller->importFiles({QUrl::fromLocalFile(importFixture->filePath("copy-test.png"))});
            exportNamesTrace->insert("missing_date_fixture_added",controller->library().size()==before+1);
            auto *menu=window->findChild<QObject *>("exportActionsMenu");
            exportNamesTrace->insert("before",controller->gpuPlan(true).state.toJson());
            exportNamesTrace->insert("before_cursor",historyCursor(controller));
            exportNamesTrace->insert("language",controller->language());
            exportNamesTrace->insert("menu_opened",menu && QMetaObject::invokeMethod(menu,"open"));
            state->phase=290;
        } else if (state->phase==290 && ready) {
            auto *menu=window->findChild<QObject *>("exportActionsMenu");
            exportNamesTrace->insert("menu_visible",menu && menu->property("visible").toBool());
            exportNamesTrace->insert("action_clicked",clickItem(window,window->findChild<QQuickItem *>("batchExportAction")));
            if (menu) QMetaObject::invokeMethod(menu,"close");
            state->phase=291;
        } else if (state->phase==291 && ready) {
            auto *dialog=window->findChild<QObject *>("exportSettingsDialog");
            auto *format=window->findChild<QQuickItem *>("batchExportFormat");
            exportNamesTrace->insert("dialog_visible",dialog && dialog->property("visible").toBool() && dialog->property("batchMode").toBool());
            if (dialog && format) {
                exportNamesTrace->insert("saved_pattern",dialog->property("nameTemplate").toString());
                exportNamesTrace->insert("saved_sequence",dialog->property("sequenceStart").toInt());
                exportNamesTrace->insert("saved_format",format->property("currentIndex").toInt());
                format->setProperty("currentIndex",1);
                dialog->setProperty("nameTemplate",QString("{name}_{version}_{seq:4}"));
                dialog->setProperty("sequenceStart",7);
                const auto preview=objectMap(dialog,"namingPreview"); const auto names=preview["names"].toStringList();
                exportNamesTrace->insert("preview",QJsonObject::fromVariantMap(preview));
                exportNamesTrace->insert("preview_valid",preview["valid"].toBool() && preview["count"].toInt()==controller->library().size()
                    && !names.isEmpty() && names[0].endsWith("_0007.png") && names.size()==std::min(3,int(controller->library().size())));
                exportNamesTrace->insert("continue_enabled",visualChild(window->contentItem(),"exportSettingsContinue")
                    && visualChild(window->contentItem(),"exportSettingsContinue")->isEnabled());
            }
            state->phase=292;
        } else if (state->phase==292 && ready) {
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".export-naming.png");
            controller->setLanguage("en_US"); resizeForSmoke(window,1180,720);
            state->phase=293;
        } else if (state->phase==293 && ready) {
            auto *dialog=window->findChild<QObject *>("exportSettingsDialog");
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".export-naming-en.png");
            if (dialog) {
                const auto x=dialog->property("x").toDouble(),y=dialog->property("y").toDouble();
                const auto w=dialog->property("width").toDouble(),h=dialog->property("height").toDouble();
                exportNamesTrace->insert("compact_fit",x>=0 && y>=0 && x+w<=window->width()+1 && y+h<=window->height()+1);
                const auto *choose=visualChild(window->contentItem(),"exportSettingsContinue"),*cancel=visualChild(window->contentItem(),"exportSettingsCancel");
                exportNamesTrace->insert("translated_buttons",choose && cancel && choose->property("text").toString()=="Choose folder…" && cancel->property("text").toString()=="Cancel");
                dialog->setProperty("nameTemplate",QString("../{name}"));
                auto *ok=visualChild(window->contentItem(),"exportSettingsContinue");
                exportNamesTrace->insert("invalid_template_blocked",!objectMap(dialog,"namingPreview")["valid"].toBool() && ok && !ok->isEnabled());
                dialog->setProperty("nameTemplate",QString("{capture_date}_{seq}"));
                exportNamesTrace->insert("missing_date_blocked",!objectMap(dialog,"namingPreview")["valid"].toBool() && ok && !ok->isEnabled());
            }
            state->phase=294;
        } else if (state->phase==294 && ready) {
            auto *dialog=window->findChild<QObject *>("exportSettingsDialog");
            if (!screenshotPath.isEmpty()) window->grabWindow().save(screenshotPath+".export-naming-invalid.png");
            if (dialog) {
                dialog->setProperty("nameTemplate",exportNamesTrace->value("saved_pattern").toString());
                dialog->setProperty("sequenceStart",exportNamesTrace->value("saved_sequence").toInt(1));
            }
            if (auto *format=window->findChild<QQuickItem *>("batchExportFormat")) format->setProperty("currentIndex",exportNamesTrace->value("saved_format").toInt());
            exportNamesTrace->insert("cancel_clicked",clickItem(window,visualChild(window->contentItem(),"exportSettingsCancel")));
            controller->setLanguage(exportNamesTrace->value("language").toString()); resizeForSmoke(window,1540,920);
            state->phase=295;
        } else if (state->phase==295 && ready) {
            auto *dialog=window->findChild<QObject *>("exportSettingsDialog");
            const bool closed=dialog && !dialog->property("visible").toBool();
            if (!closed && state->elapsed.elapsed()<60000) return;
            exportNamesTrace->insert("closed",closed);
            exportNamesTrace->insert("edits_unchanged",controller->gpuPlan(true).state.toJson()==exportNamesTrace->value("before").toObject()
                && historyCursor(controller)==exportNamesTrace->value("before_cursor").toInt(-1));
            exportNamesTrace->insert("no_export_queued",!controller->exportBusy());
            controller->selectPhoto(transferTrace->value("source_index").toInt()); state->phase=50;
        } else if (state->phase==50 && ready) {
            // Restoring the source starts a new asynchronous histogram. Preview
            // readiness alone does not mean its full-resolution counts arrived.
            const auto meta=controller->currentMetadata();
            const auto expected=meta.value("pixelWidth").toULongLong()*meta.value("pixelHeight").toULongLong();
            const bool scopesReady=expected>0 && controller->scopesPixelCount()==expected;
            transferTrace->insert("final_scopes",scopesReady);
            complete=scopesReady && scopePlots->value("waveform").toBool() && scopePlots->value("parade").toBool() && scopePlots->value("vectorscope").toBool();
        }
        if(state->lookEnabled)
            complete=complete && !controller->referenceBusy() && !controller->cameraReferenceUrl().isEmpty();
        if(!complete && state->elapsed.elapsed()<60000) return;
        timer->stop();
        const bool gpuRequired=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool workspaceOk=state->gridVisited && state->filmstripPresent && state->presetActionsPresent && state->restoredDevelop && state->curationPassed && state->catalogPassed && state->catalogDatesPassed && state->copiesPassed && state->historyPassed && state->geometryPassed && state->interactiveCropPassed && state->straightenPassed;
        const bool copyOk=copyTrace->value("menu_opened").toBool() && copyTrace->value("menu_visible").toBool() && copyTrace->value("started").toBool()
            && copyTrace->value("prepared").toBool() && copyTrace->value("dialog_visible").toBool() && copyTrace->value("custom_clicked").toBool()
            && copyTrace->value("dialog_centered").toBool() && copyTrace->value("preview_valid").toBool()
            && copyTrace->value("dated_prepared").toBool() && copyTrace->value("capture_preview").toBool() && copyTrace->value("missing_capture_blocked").toBool()
            && copyTrace->value("content_equal").toBool() && copyTrace->value("catalog_added").toBool();
        bool transferOk=true;
        for (const auto &key : {"opened","visible","default_geometry_excluded","none_clicked","exposure_clicked","applied","closed","isolated_undo","final_scopes"})
            transferOk=transferOk && transferTrace->value(key).toBool();
        bool correctionOk=true;
        for (const auto *key : {"toggle_revealed","opened","controls_found","perspective_revealed","perspective_clicked","perspective_changed","ca_revealed","ca_clicked","ca_changed","other_values_set","full_scopes","undo_restored"})
            correctionOk=correctionOk && correctionTrace->value(key).toBool();
        bool vignetteOk=true;
        for (const auto *key : {"toggle_revealed","opened","controls_found","amount_revealed","amount_clicked","amount_changed","midpoint_revealed","midpoint_clicked","midpoint_changed","feather_revealed","feather_clicked","feather_changed","full_scopes","reset_revealed","reset_clicked","isolated_reset_undo_redo","undo_restored"})
            vignetteOk=vignetteOk && vignetteTrace->value(key).toBool();
        bool exportNamesOk=true;
        for (const auto *key : {"missing_date_fixture_added","menu_opened","menu_visible","action_clicked","dialog_visible","preview_valid","continue_enabled","compact_fit",
                               "translated_buttons","invalid_template_blocked","missing_date_blocked","cancel_clicked","closed","edits_unchanged","no_export_queued"})
            exportNamesOk=exportNamesOk && exportNamesTrace->value(key).toBool();
        bool ok=complete && workspaceOk && copyOk && transferOk && correctionOk && vignetteOk && exportNamesOk && (!gpuRequired || controller->gpuActive());
        auto report=PerformanceRecorder::snapshot();
        report["look_validation_required"]=state->lookEnabled;
        report["ui_library_visited"]=state->gridVisited;
        report["ui_filmstrip_found"]=state->filmstripPresent;
        report["ui_preset_actions_found"]=state->presetActionsPresent;
        report["ui_develop_restored"]=state->restoredDevelop;
        report["ui_curation_passed"]=state->curationPassed;
        report["ui_catalog_passed"]=state->catalogPassed;
        report["ui_catalog_dates_passed"]=state->catalogDatesPassed;
        report["ui_virtual_copies_passed"]=state->copiesPassed;
        report["ui_history_passed"]=state->historyPassed;
        report["ui_geometry_passed"]=state->geometryPassed;
        report["ui_interactive_crop_passed"]=state->interactiveCropPassed;
        report["ui_interactive_crop_trace"]=*cropTrace;
        report["ui_scope_plots"]=*scopePlots;
        report["ui_copy_import_passed"]=copyOk;
        report["ui_copy_import_trace"]=*copyTrace;
        report["ui_selective_sync_passed"]=transferOk;
        report["ui_selective_sync_trace"]=*transferTrace;
        report["ui_geometry_corrections_passed"]=correctionOk;
        report["ui_geometry_corrections_trace"]=*correctionTrace;
        report["ui_vignette_passed"]=vignetteOk;
        report["ui_vignette_trace"]=*vignetteTrace;
        report["ui_export_naming_passed"]=exportNamesOk;
        report["ui_export_naming_trace"]=*exportNamesTrace;
        report["ui_straighten_passed"]=state->straightenPassed;
        report["ui_straighten_degrees"]=state->straightenDegrees;
        report["ui_straighten_scope_pixels"]=qint64(state->straightenedScopePixels);
        report["ui_geometry_crop_scope_pixels"]=qint64(state->croppedScopePixels);
        report["look"]=QJsonObject::fromVariantMap(controller->lookState());
        report["reference"]=QJsonObject::fromVariantMap(controller->cameraReferenceInfo());
        report["source_commit"]=QStringLiteral(JIXELLIGHT_GIT_COMMIT);
        report["build_version"]=QCoreApplication::applicationVersion();
        report["smoke_passed"]=ok;
        report["phase"]=state->phase;report["edits"]=state->edits;
        report["backend"]=controller->processingBackend();report["gpu_active"]=controller->gpuActive();
        report["preview_ready"]=controller->previewReady();report["scopes_mode"]=controller->scopesStatus();
        report["scope_pixels"]=qint64(controller->scopesPixelCount());report["render_revision"]=qint64(controller->renderRevision());
        report["elapsed_ms"]=state->elapsed.elapsed();
        if(!screenshotPath.isEmpty()) report["screenshot_saved"]=window->grabWindow().save(screenshotPath);
        QSaveFile output(reportPath);
        if(!output.open(QIODevice::WriteOnly) || output.write(QJsonDocument(report).toJson())<0 || !output.commit()) ok=false;
        LoggingEngine::flush();
        QCoreApplication::exit(ok?0:2);
    });
    timer->start();
}
