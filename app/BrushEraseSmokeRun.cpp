#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include "app/SmokeCaptureCoordinates.h"
#include "core/pipeline/BrushRenderRequest.h"
#include <QQuickWindow>
#include <QCoreApplication>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTemporaryDir>
#include <QElapsedTimer>
#include <QTimer>
#include <QBuffer>
#include <QSaveFile>
#include <QFile>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QJsonDocument>
#include <QJsonArray>
#include <QColorSpace>
#include <QRgba64>
#include <QDir>
#include <memory>
#include <filesystem>
#include <algorithm>

namespace {
QQuickItem *findItem(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *child:item->childItems())if(auto *result=findItem(child,name))return result;
    return nullptr;
}
bool exposed(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled()||item->width()<=0||item->height()<=0)return false;
    const auto point=item->mapToScene({item->width()/2,item->height()/2});
    if(!QRectF(0,0,window->width(),window->height()).contains(point))return false;
    for(auto *parent=item->parentItem();parent;parent=parent->parentItem())
        if(parent->clip()&&!parent->contains(parent->mapFromScene(point)))return false;
    return true;
}
void mouse(QQuickWindow *window,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,point,window->mapToGlobal(point.toPoint()),button,buttons,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&event);
}
void key(QQuickWindow *window,int code) {
    QKeyEvent down(QEvent::KeyPress,code,Qt::NoModifier),up(QEvent::KeyRelease,code,Qt::NoModifier);
    QCoreApplication::sendEvent(window,&down);QCoreApplication::sendEvent(window,&up);
}
bool click(QQuickWindow *window,QQuickItem *item) {
    if(!exposed(window,item))return false;const auto point=item->mapToScene({item->width()/2,item->height()/2});
    mouse(window,QEvent::MouseButtonPress,point,Qt::LeftButton,Qt::LeftButton);
    mouse(window,QEvent::MouseButtonRelease,point,Qt::LeftButton,Qt::NoButton);return true;
}
bool select(QQuickWindow *window,const QString &name,int index) {
    auto *item=findItem(window->contentItem(),name);if(!click(window,item))return false;
    key(window,Qt::Key_Home);for(int i=0;i<index;++i)key(window,Qt::Key_Down);key(window,Qt::Key_Return);
    return item->property("currentIndex").toInt()==index;
}
QByteArray hashFile(const QString &path) {
    QFile file(path);if(!file.open(QIODevice::ReadOnly))return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);return hash.addData(&file)?hash.result().toHex():QByteArray{};
}
QJsonObject masks(PhotoController *c){return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();}
int history(PhotoController *c) {
    const auto rows=c->editHistory();for(int i=0;i<rows.size();++i)if(rows[i].toMap()["current"].toBool())return i;return -1;
}
int delta(QColor a,QColor b){return std::max({std::abs(a.red()-b.red()),std::abs(a.green()-b.green()),std::abs(a.blue()-b.blue())});}
int plotInk(const QImage &image,QQuickWindow *window,QQuickItem *plot) {
    if(!plot)return 0;const auto a=plot->mapToScene({0,0}),b=plot->mapToScene({plot->width(),plot->height()});
    const auto rect=SmokeCaptureCoordinates::region(image,QRectF(a,b),window->effectiveDevicePixelRatio());int count=0;
    for(int y=rect.top();!rect.isEmpty()&&y<rect.y()+rect.height();y+=2)for(int x=rect.left();x<rect.x()+rect.width();x+=2) {
        const auto color=image.pixelColor(x,y);if(color.green()>70&&color.green()>color.red()+12&&color.green()>color.blue()+5)++count;
    }
    return count;
}
void startErase(PhotoController *c,QQuickWindow *window,const QString &requestedSource,const QString &report,bool realSony) {
    const auto owned=std::make_shared<QTemporaryDir>();const QString source=realSony?requestedSource:owned->filePath("eraser-owned.png");
    const auto nativePath=[](const QString &path) {
#ifdef Q_OS_WIN
        return std::filesystem::path(path.toStdWString());
#else
        return std::filesystem::path(path.toStdString());
#endif
    };
    const auto aliasesSource=[&](const QString &output) {
        std::error_code pathError;
        return QFileInfo(source).absoluteFilePath()==QFileInfo(output).absoluteFilePath()
            ||std::filesystem::equivalent(nativePath(source),nativePath(output),pathError);
    };
    bool aliases=aliasesSource(report);
    for(const auto *label:{"before","painted","erased","undo","selected","deleted","reopen","final","english","failure"})
        aliases=aliases||aliasesSource(report+"."+label+"-controls.png");
    if(aliases) {
        // Refuse before any report/capture write, including symlink aliases.
        QTimer::singleShot(0,c,[]{QCoreApplication::exit(2);});return;
    }
    const QSize size=realSony?QSize(7028,4688):QSize(2203,1207);const qint64 pixels=qint64(size.width())*size.height();
    const int deadline=realSony?60000:45000;
    struct State {
        QElapsedTimer clock;int phase=0,settle=0,initialHistory=0,ink=0,paintDelta=0,eraseDelta=0,undoDelta=999,redoDelta=999;
        bool done=false,noLayer=false,nativePaint=false,nativeErase=false,transient=false,escape=false,oneUndo=false,display=false;
        bool undoRedo=false,selectedDelete=false,reopen=false,loadedCancel=false,scopeControls=false,scopes=false,english=false;
        QString layer,project;QByteArray sourceHash;QJsonObject initial,painted,erased,captures;QColor baseline,paintedColor;
    };
    auto s=std::make_shared<State>();s->clock.start();auto *timer=new QTimer(c);timer->setInterval(50);
    const auto item=[=](const QString &name){return findItem(window->contentItem(),name);};
    const auto capture=[=](const QString &label,const QImage &image) {
        auto *panel=item("developRightPanel");if(image.isNull()||!panel||!panel->isVisible())return false;
        const auto a=panel->mapToScene({0,0}),b=panel->mapToScene({panel->width(),panel->height()});
        const auto rect=SmokeCaptureCoordinates::region(image,QRectF(a,b),window->effectiveDevicePixelRatio());
        if(rect.isEmpty())return false;
        // Full-window PNG bytes stay in memory; only the controls crop is saved.
        QByteArray full;QBuffer buffer(&full);if(!buffer.open(QIODevice::WriteOnly)||!image.save(&buffer,"PNG"))return false;
        const auto path=report+"."+label+"-controls.png";if(!image.copy(rect).save(path))return false;
        s->captures[label]=QJsonObject{{"sha256",QString::fromLatin1(QCryptographicHash::hash(full,QCryptographicHash::Sha256).toHex())},
            {"controls_sha256",QString::fromLatin1(hashFile(path))},{"width",image.width()},{"height",image.height()},
            {"controls_width",rect.width()},{"controls_height",rect.height()},{"device_scale",window->effectiveDevicePixelRatio()}};
        return true;
    };
    const auto finish=[=](const QString &error) {
        if(s->done)return;s->done=true;timer->stop();if(!error.isEmpty())capture("failure",window->grabWindow());
        const auto budget=BrushRenderBudget::sharedDefault()->snapshot();
        const bool admitted=budget.limit==BrushRenderBudget::DefaultBytes&&budget.peak>0&&budget.peak<=budget.limit&&budget.charged<=budget.limit;
        const bool unchanged=!s->sourceHash.isEmpty()&&hashFile(source)==s->sourceHash;
        const bool shape=c->brushFrameSize()==size&&c->currentIsRaw()==realSony;
        const bool isolated=QCoreApplication::instance()->property("jixellightSmokeSettingsIsolated").toBool();
        const bool controls=exposed(window,item("brushModeSelector"))&&exposed(window,item("brushExposureSlider"));
        const bool required=s->noLayer&&s->nativePaint&&s->nativeErase&&s->transient&&s->escape&&s->oneUndo&&s->display
            &&s->undoRedo&&s->selectedDelete&&s->reopen&&s->loadedCancel&&s->scopeControls&&s->scopes&&s->english;
        const bool passed=error.isEmpty()&&required&&isolated&&unchanged&&shape&&admitted&&controls&&s->clock.elapsed()<deadline
            &&(!qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")||c->gpuActive());
        const QJsonObject data{{"passed",passed},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},
            {"elapsed_ms",s->clock.elapsed()},{"deadline_ms",deadline},{"phase",s->phase},{"error",error},
            {"synthetic_fixture",!realSony},{"source_override",false},{"source_kind",realSony?"public-sony-arw":"owned-rgba64-png"},
            {"source_pixels",qint64(c->brushFrameSize().width())*c->brushFrameSize().height()},
            {"source_width",c->brushFrameSize().width()},{"source_height",c->brushFrameSize().height()},{"expected_source_pixels",pixels},
            {"source_bytes",QFileInfo(source).size()},{"source_sha256",QString::fromLatin1(s->sourceHash)},
            {"source_is_raw",c->currentIsRaw()},{"source_unchanged",unchanged},{"settings_isolated",isolated},
            {"erase_no_layer_refusal",s->noLayer},{"native_paint",s->nativePaint},{"native_erase_mode",s->nativeErase},
            {"live_uncommitted",s->transient},{"escape_cancel",s->escape},{"one_undo",s->oneUndo},{"display_changed",s->display},
            {"paint_rgb_delta",s->paintDelta},{"erase_rgb_delta",s->eraseDelta},{"undo_rgb_delta",s->undoDelta},{"redo_rgb_delta",s->redoDelta},
            {"undo_redo",s->undoRedo},{"selected_erase_delete",s->selectedDelete},{"project_reopen",s->reopen},
            {"loaded_layer_cancel",s->loadedCancel},{"scope_controls",s->scopeControls},{"full_scopes",s->scopes},
            {"scope_pixels",qint64(c->scopesPixelCount())},{"scope_plot_pixels",qint64(c->scopePlotPixels())},{"scope_display_ink_samples",s->ink},
            {"controls_visible",controls},{"english_controls_visible",s->english},{"budget_admitted",admitted},{"operation_budget_bytes",budget.limit},
            {"operation_peak_reserved_bytes",budget.peak},{"operation_live_charged_bytes",budget.charged},
            {"gpu_active",c->gpuActive()},{"backend",c->processingBackend()},{"producer","cpu-reference"},
            {"native_gpu_brush",false},{"native_float_raw_decoder",false},{"photographs_published",false},{"captures",s->captures}};
        QSaveFile output(report);const auto bytes=QJsonDocument(data).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit();
        const int code=passed&&saved?0:1;QTimer::singleShot(0,c,[code]{QCoreApplication::exit(code);});
    };
    QObject::connect(timer,&QTimer::timeout,c,[=,keepAlive=owned] {
        Q_UNUSED(keepAlive); // Keep the owned source/project until the timer is destroyed.
        if(s->clock.elapsed()>=deadline){finish("Native eraser GUI exceeded its original bounded deadline");return;}
        if(!c->brushReady())return;auto *canvas=item("photoCanvas"),*overlay=item("brushCanvasOverlay");
        if(!canvas||!overlay||!canvas->property("brushDisplayReady").toBool())return;
        if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive())return;
        if(c->brushFrameSize()!=size||c->currentIsRaw()!=realSony){finish("Source is not at required full native dimensions/type");return;}
        const auto point=[&](double x){return overlay->mapToScene({overlay->width()*x,overlay->height()*.6});};
        const auto read=[&](QColor *color) {
            const auto image=window->grabWindow();*color=SmokeCaptureCoordinates::sample(image,point(.5),window->effectiveDevicePixelRatio());
            return color->isValid()?image:QImage{};
        };
        const auto gesture=[&](bool cancel) {
            mouse(window,QEvent::MouseButtonPress,point(.3),Qt::LeftButton,Qt::LeftButton);
            for(int i=1;i<=8;++i)mouse(window,QEvent::MouseMove,point(.3+.4*i/8),Qt::NoButton,Qt::LeftButton);
            const bool active=c->brushDrawing();if(cancel)key(window,Qt::Key_Escape);
            mouse(window,QEvent::MouseButtonRelease,point(.7),Qt::LeftButton,Qt::NoButton);return active;
        };
        if(s->phase==0) {
            s->initial=masks(c);s->initialHistory=history(c);
            if(!s->initial.isEmpty()||!click(window,item("editBrushMaskCanvas"))){finish("Initial eraser document/tool unavailable");return;}
            s->phase=1;s->settle=0;
        }else if(s->phase==1) {
            if(++s->settle<3)return;if(!select(window,"brushModeSelector",1)){finish("Cannot choose native Erase");return;}s->phase=2;
        }else if(s->phase==2) {
            auto *pointer=item("brushPointerArea");const bool forbidden=pointer&&pointer->property("cursorShape").toInt()==int(Qt::ForbiddenCursor);
            const bool active=gesture(false);s->noLayer=forbidden&&!active&&!c->brushDrawing()&&masks(c)==s->initial&&history(c)==s->initialHistory;
            if(!s->noLayer||!select(window,"brushModeSelector",0)){finish("Erase without an existing layer was not refused");return;}
            s->phase=3;s->settle=0;
        }else if(s->phase==3) {
            if(++s->settle<3)return;const auto image=read(&s->baseline);if(image.isNull())return;
            const int maximum=std::max({s->baseline.red(),s->baseline.green(),s->baseline.blue()});
            if(maximum<30||maximum>245||!capture("before",image)){finish("Native baseline probe/capture unavailable");return;}
            const bool active=gesture(true);s->escape=active&&!c->brushDrawing()&&masks(c)==s->initial&&history(c)==s->initialHistory;
            s->nativePaint=gesture(false);s->painted=masks(c);const auto doc=c->gpuPlan(true).state.brushMasks;
            if(!s->escape||!s->nativePaint||history(c)!=s->initialHistory+1||doc.layers.size()!=1||doc.layers[0].strokes.size()!=1
                ||doc.layers[0].strokes[0].mode!=BrushMaskGeometry::StrokeMode::Paint){finish("Native Paint is not a single guarded gesture");return;}
            s->layer=doc.layers[0].id;s->phase=4;s->settle=0;
        }else if(s->phase==4) {
            const auto image=read(&s->paintedColor);if(image.isNull())return;s->paintDelta=delta(s->baseline,s->paintedColor);if(s->paintDelta<3)return;
            if(!capture("painted",image)||!select(window,"brushModeSelector",1)){finish("Cannot capture Paint or select Erase");return;}
            s->nativeErase=true;s->phase=5;s->settle=0;
        }else if(s->phase==5) {
            if(++s->settle<3)return;mouse(window,QEvent::MouseButtonPress,point(.3),Qt::LeftButton,Qt::LeftButton);
            for(int i=1;i<=8;++i)mouse(window,QEvent::MouseMove,point(.3+.4*i/8),Qt::NoButton,Qt::LeftButton);
            s->transient=c->brushDrawing()&&masks(c)==s->painted&&history(c)==s->initialHistory+1;
            mouse(window,QEvent::MouseButtonRelease,point(.7),Qt::LeftButton,Qt::NoButton);s->erased=masks(c);
            const auto doc=c->gpuPlan(true).state.brushMasks;s->oneUndo=history(c)==s->initialHistory+2&&doc.layers.size()==1
                &&doc.layers[0].id==s->layer&&doc.layers[0].strokes.size()==2&&doc.layers[0].strokes[1].mode==BrushMaskGeometry::StrokeMode::Erase
                &&s->erased["schema"].toInt()==2;
            if(!s->transient||!s->oneUndo){finish("Native Erase leaked transient state or is not one Undo");return;}s->phase=6;
        }else if(s->phase==6) {
            QColor color;const auto image=read(&color);if(image.isNull())return;s->eraseDelta=delta(color,s->paintedColor);
            if(s->eraseDelta<3||delta(color,s->baseline)>2)return;s->display=true;
            if(!capture("erased",image)||!click(window,item("undoEditButton"))){finish("Cannot capture Erase or click Undo");return;}s->phase=7;
        }else if(s->phase==7) {
            QColor color;const auto image=read(&color);if(image.isNull())return;s->undoDelta=delta(color,s->paintedColor);if(s->undoDelta>2)return;
            if(masks(c)!=s->painted||history(c)!=s->initialHistory+1||!capture("undo",image)||!click(window,item("redoEditButton"))){finish("Native Undo did not restore painted intent");return;}s->phase=8;
        }else if(s->phase==8) {
            QColor color;const auto image=read(&color);if(image.isNull())return;s->redoDelta=delta(color,s->baseline);if(s->redoDelta>2)return;
            s->undoRedo=masks(c)==s->erased&&history(c)==s->initialHistory+2;
            const bool active=gesture(true);s->escape=s->escape&&active&&!c->brushDrawing()&&masks(c)==s->erased&&history(c)==s->initialHistory+2;
            if(!s->undoRedo||!s->escape||!select(window,"brushStrokeSelector",2)){finish("Native Redo/Escape/erase selection failed");return;}s->phase=9;s->settle=0;
        }else if(s->phase==9) {
            if(++s->settle<3)return;
            if(masks(c)!=s->erased||history(c)!=s->initialHistory+2||!capture("selected",window->grabWindow())
                ||!click(window,item("removeSelectedBrushStroke"))){finish("Native erase selection altered intent or cannot be deleted");return;}s->phase=10;
        }else if(s->phase==10) {
            QColor color;const auto image=read(&color);if(image.isNull()||delta(color,s->paintedColor)>2)return;
            s->selectedDelete=masks(c)==s->painted&&history(c)==s->initialHistory+3;
            if(!s->selectedDelete||!capture("deleted",image)||!click(window,item("undoEditButton"))){finish("Selected eraser deletion did not recover visible schema1 Paint");return;}s->phase=11;
        }else if(s->phase==11) {
            if(masks(c)!=s->erased||history(c)!=s->initialHistory+2||!c->flushEdits()
                ||!c->openProject(QUrl::fromLocalFile(s->project))){finish("Cannot persist/reopen the eraser project");return;}s->phase=12;s->settle=0;
        }else if(s->phase==12) {
            if(++s->settle<3)return;QColor color;const auto image=read(&color);if(image.isNull()||delta(color,s->baseline)>2)return;
            s->reopen=masks(c)==s->erased;
            if(!s->reopen||!capture("reopen",image)||!click(window,item("editBrushMaskCanvas"))){finish("Reopened project lost visible eraser intent");return;}s->phase=13;s->settle=0;
        }else if(s->phase==13) {
            if(++s->settle<3)return;if(!select(window,"brushLayerSelector",1)||!select(window,"brushModeSelector",1)){finish("Cannot select the reopened eraser target");return;}
            const auto cursor=history(c);const bool active=gesture(true);s->loadedCancel=active&&!c->brushDrawing()&&masks(c)==s->erased&&history(c)==cursor;
            if(!s->loadedCancel||!select(window,"scopeModeChoice",1)){finish("Loaded eraser Escape or native waveform choice failed");return;}s->phase=14;s->settle=0;
        }else if(s->phase==14) {
            if(++s->settle<3)return;if(!c->exactScopes()&&!click(window,item("exactScopesCheck"))){finish("Cannot enable native exact scopes");return;}
            s->scopeControls=c->scopeMode()=="waveform"&&c->exactScopes();if(!s->scopeControls){finish("Native scopes did not enter exact waveform mode");return;}s->phase=15;
        }else if(s->phase==15) {
            auto *plot=item("scopePlotImage");if(!c->scopePlotCurrent()||c->scopesPixelCount()!=quint64(pixels)||c->scopePlotPixels()!=quint64(pixels)
                ||!plot||plot->property("status").toInt()!=1||plot->property("source").toUrl()!=QUrl(c->scopePlotUrl()))return;
            const auto image=window->grabWindow();s->ink=plotInk(image,window,plot);if(s->ink<64)return;
            s->scopes=masks(c)==s->erased&&c->scopeMode()=="waveform"&&c->exactScopes();
            if(!s->scopes||!capture("final",image)){finish("Final eraser scopes/capture are not current");return;}
            c->setLanguage("en_US");window->resize(1180,720);s->phase=16;s->settle=0;
        }else if(s->phase==16) {
            if(++s->settle<3||!c->scopePlotCurrent()||c->scopesPixelCount()!=quint64(pixels)
                ||c->scopePlotPixels()!=quint64(pixels))return;
            auto *mode=item("brushModeSelector");auto *panel=item("developRightPanel");auto *scope=item("scopeModeChoice");auto *status=item("scopeStatusLabel");
            if(!mode||!panel||panel->property("brushRevealPending").toBool()||!exposed(window,mode)
                ||!exposed(window,item("brushExposureSlider")))return;
            s->english=c->language()=="en_US"&&window->width()==1180&&window->height()==720
                &&mode->property("currentIndex").toInt()==1&&mode->property("displayText").toString()=="Erase"&&masks(c)==s->erased
                &&scope&&scope->property("currentIndex").toInt()==1&&scope->property("displayText").toString()=="Luma waveform"
                &&status&&status->property("text").toString().contains("Full resolution");
            if(!s->english||!capture("english",window->grabWindow())){finish("Compact English eraser controls are not current/visible");return;}finish({});
        }
    });
    if(!owned->isValid()){finish("Cannot create owned QA directory");return;}
    if(!realSony) {
        QImage image(size,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
        for(int y=0;y<size.height();++y)for(int x=0;x<size.width();++x)
            reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(4000+x*17,6000+y*23,8000+(x+y)*9,65535);
        if(!image.save(source)){finish("Cannot create owned eraser fixture");return;}
    }
    s->sourceHash=hashFile(source);
    if(realSony&&(QFileInfo(source).suffix().compare("arw",Qt::CaseInsensitive)!=0||QFileInfo(source).size()!=69120000||s->sourceHash!="aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d")) {
        finish("Pinned public Sony ARW missing or SHA differs; no fallback permitted");return;
    }
    if(s->sourceHash.isEmpty()||!c->importFile(QUrl::fromLocalFile(source))||!c->createProject(QUrl::fromLocalFile(owned->path()),"EraserNative")) {
        finish("Cannot import source/create owned eraser project");return;
    }
    s->project=c->projectPath();timer->start();
}
}
void startBrushEraseCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &report) {startErase(c,window,{},report,false);}
void startSonyBrushEraseCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &source,const QString &report) {startErase(c,window,source,report,true);}
