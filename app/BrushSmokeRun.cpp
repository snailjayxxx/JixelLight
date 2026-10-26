#include "app/SmokeRun.h"
#include "app/PhotoController.h"
#include <QQuickWindow>
#include <QQuickItem>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QCoreApplication>
#include <QTemporaryDir>
#include <QSaveFile>
#include <QFile>
#include <QJsonDocument>
#include <QCryptographicHash>
#include <QColorSpace>
#include <QRgba64>

namespace {
QQuickItem *child(QQuickItem *item,const QString &name) {
    if(!item)return nullptr;if(item->objectName()==name)return item;
    for(auto *c:item->childItems())if(auto *found=child(c,name))return found;return nullptr;
}
void mouse(QQuickWindow *window,QEvent::Type type,QPointF point,Qt::MouseButton button,Qt::MouseButtons buttons) {
    QMouseEvent event(type,point,point,window->mapToGlobal(point.toPoint()),button,buttons,Qt::NoModifier);QCoreApplication::sendEvent(window,&event);
}
bool click(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto p=item->mapToScene(QPointF(item->width()/2,item->height()/2));
    mouse(window,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);return true;
}
bool exposed(QQuickWindow *window,QQuickItem *item) {
    if(!item||!item->isVisible()||!item->isEnabled())return false;
    const auto point=item->mapToScene({item->width()/2,item->height()/2});
    if(!QRectF(0,0,window->width(),window->height()).contains(point))return false;
    for(auto *p=item->parentItem();p;p=p->parentItem())if(p->clip()&&!p->contains(p->mapFromScene(point)))return false;
    return true;
}
int cursor(PhotoController *c){const auto h=c->editHistory();for(int i=0;i<h.size();++i)if(h[i].toMap()["current"].toBool())return i;return -1;}
QJsonObject masks(PhotoController *c){return c->gpuPlan(true).state.toJson()["brushMasks"].toObject();}
QByteArray fileHash(const QString &path){QFile f(path);if(!f.open(QIODevice::ReadOnly))return {};QCryptographicHash h(QCryptographicHash::Sha256);return h.addData(&f)?h.result().toHex():QByteArray{};}
QColor sample(const QImage &image,QQuickWindow *window,QPointF point) {
    return image.pixelColor(std::clamp(int(point.x()*image.width()/window->width()),0,image.width()-1),
                            std::clamp(int(point.y()*image.height()/window->height()),0,image.height()-1));
}
}
void startBrushCanvasSmokeRun(PhotoController *c,QQuickWindow *window,const QString &reportPath) {
    struct State {
        QElapsedTimer clock;int phase=0,settle=0,history=0,delta=0;bool finished=false;
        bool transient=false,escape=false,oneUndo=false,display=false,undoRedo=false,reopen=false,zoom=false,scopes=false;
        bool settings=false;double exposure=1;QJsonObject painted;QColor baseline;QRectF frame;QByteArray sourceHash;
    };
    auto s=std::make_shared<State>();s->clock.start();auto dir=std::make_shared<QTemporaryDir>();
    auto *timer=new QTimer(c);timer->setInterval(50);const auto source=dir->filePath("owned-brush.png");
    const auto finish=[=](const QString &error) {
        if(s->finished)return;s->finished=true;timer->stop();const bool unchanged=fileHash(source)==s->sourceHash;
        const bool gpu=qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU");
        const bool ok=error.isEmpty()&&s->settings&&s->transient&&s->escape&&s->oneUndo&&s->display&&s->undoRedo&&s->reopen&&s->zoom&&s->scopes&&unchanged&&(!gpu||c->gpuActive());
        const QJsonObject report{{"passed",ok},{"source_commit",QStringLiteral(JIXELLIGHT_GIT_COMMIT)},{"elapsed_ms",s->clock.elapsed()},
            {"phase",s->phase},{"error",error},{"synthetic_fixture",true},{"source_pixels",2659021},{"source_unchanged",unchanged},
            {"live_uncommitted",s->transient},{"escape_cancel",s->escape},{"one_undo",s->oneUndo},{"display_changed",s->display},
            {"display_rgb_delta",s->delta},{"undo_redo",s->undoRedo},{"project_reopen",s->reopen},{"zoom_roi",s->zoom},
            {"settings_visible",s->settings},{"pointer_exposure",s->exposure},
            {"full_scopes",s->scopes},{"scope_pixels",qint64(c->scopesPixelCount())},{"gpu_active",c->gpuActive()},
            {"backend",c->processingBackend()},{"producer","cpu-reference"},{"native_gpu_brush",false},{"native_float_raw_decoder",false}};
        QSaveFile output(reportPath);const auto bytes=QJsonDocument(report).toJson(QJsonDocument::Indented);
        const bool saved=output.open(QIODevice::WriteOnly)&&output.write(bytes)==bytes.size()&&output.commit();QCoreApplication::exit(ok&&saved?0:1);
    };
    QObject::connect(timer,&QTimer::timeout,c,[=] {
        if(s->clock.elapsed()>=45000){finish("Brush pointer integration timed out");return;}
        if(!c->brushReady())return;
        if(qEnvironmentVariableIsSet("JIXELLIGHT_REQUIRE_GPU")&&!c->gpuActive())return;
        auto *canvas=child(window->contentItem(),"photoCanvas"),*overlay=child(window->contentItem(),"brushCanvasOverlay");
        if(!canvas||!overlay)return;
        const auto point=[&](double x,double y){return overlay->mapToScene({overlay->width()*x,overlay->height()*y});};
        if(s->phase==0) {
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas")))return;
            s->phase=1;s->settle=0;
        }else if(s->phase==1) {
            if(canvas->property("maskTool").toString()!="brush"||!overlay->isEnabled()||++s->settle<3)return;
            auto *selector=child(window->contentItem(),"brushLayerSelector"),*slider=child(window->contentItem(),"brushExposureSlider");
            if(!exposed(window,selector)||!exposed(window,slider)){finish("Brush tool did not reveal its editable controls");return;}
            const auto p=slider->mapToScene({slider->width()*.67,slider->height()/2});
            mouse(window,QEvent::MouseButtonPress,p,Qt::LeftButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,p,Qt::LeftButton,Qt::NoButton);
            s->exposure=slider->property("value").toDouble();s->settings=s->exposure>.5&&s->exposure<2&&std::abs(s->exposure-1)>.01&&masks(c).isEmpty();s->phase=9;return;
        }else if(s->phase==9) {
            s->history=cursor(c);mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            mouse(window,QEvent::MouseMove,point(.5,.6),Qt::NoButton,Qt::LeftButton);
            s->transient=c->brushDrawing()&&masks(c).isEmpty()&&cursor(c)==s->history;
            if(!s->transient){finish("Pointer press did not create a transient gesture");return;}
            QKeyEvent key(QEvent::KeyPress,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(window,&key);
            QKeyEvent release(QEvent::KeyRelease,Qt::Key_Escape,Qt::NoModifier);QCoreApplication::sendEvent(window,&release);
            mouse(window,QEvent::MouseButtonRelease,point(.5,.6),Qt::LeftButton,Qt::NoButton);
            s->escape=!c->brushDrawing()&&masks(c).isEmpty()&&cursor(c)==s->history;
            if(!s->escape){finish("Native Escape did not abandon the transient stroke");return;}
            s->phase=2;s->settle=0;
        }else if(s->phase==2) {
            if(++s->settle<3)return;
            const auto image=window->grabWindow();if(image.isNull()){finish("Cannot read baseline window");return;}
            s->baseline=sample(image,window,point(.5,.6));if(!image.save(reportPath+".before.png")){finish("Cannot save brush baseline");return;}
            mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            for(int i=1;i<=5;++i)mouse(window,QEvent::MouseMove,point(.25+.5*i/5,.6),Qt::NoButton,Qt::LeftButton);
            mouse(window,QEvent::MouseButtonRelease,point(.75,.6),Qt::LeftButton,Qt::NoButton);
            s->painted=masks(c);s->oneUndo=cursor(c)==s->history+1&&!s->painted.isEmpty();
            const auto doc=c->gpuPlan(true).state.brushMasks;
            s->settings=s->settings&&!doc.layers.empty()&&std::abs(doc.layers[0].exposureEv-s->exposure)<1e-9;
            if(!s->oneUndo){finish("Completed pointer stroke is not one undo entry");return;}
            c->setExactScopes(true);c->setScopeMode("waveform");s->phase=3;s->settle=0;
        }else if(s->phase==3) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            const auto image=window->grabWindow();if(image.isNull()){finish("Cannot read painted window");return;}
            const auto color=sample(image,window,point(.5,.6));s->delta=std::max({std::abs(color.red()-s->baseline.red()),std::abs(color.green()-s->baseline.green()),std::abs(color.blue()-s->baseline.blue())});
            s->display=s->delta>=3;s->scopes=true;
            if(!s->display||!image.save(reportPath+".painted.png")){finish("Actual painted display did not change");return;}
            if(!click(window,child(window->contentItem(),"undoEditButton"))){finish("Cannot click Undo");return;}
            s->phase=4;
        }else if(s->phase==4) {
            if(!masks(c).isEmpty()){finish("Native Undo retained the painted stroke");return;}
            if(!click(window,child(window->contentItem(),"redoEditButton"))){finish("Cannot click Redo");return;}
            s->phase=5;
        }else if(s->phase==5) {
            s->undoRedo=masks(c)==s->painted;
            if(!s->undoRedo||!c->createProject(QUrl::fromLocalFile(dir->path()),"BrushPointer")||!c->flushEdits()){finish("Cannot persist painted project");return;}
            const auto project=c->projectPath();if(!c->openProject(QUrl::fromLocalFile(project))){finish("Cannot reopen painted project");return;}
            s->phase=6;s->settle=0;
        }else if(s->phase==6) {
            if(++s->settle<3)return;s->reopen=masks(c)==s->painted;
            if(!s->reopen||!window->grabWindow().save(reportPath+".reopen.png")){finish("Reopened UI lost painted intent");return;}
            if(!click(window,child(window->contentItem(),"editBrushMaskCanvas"))){finish("Cannot re-enter brush tool");return;}
            c->setExactScopes(true);c->setScopeMode("waveform");
            canvas->setProperty("zoom",2.0);s->phase=7;s->settle=0;
        }else if(s->phase==7) {
            s->frame=c->previewFrameRect();if(s->frame.width()>=.98||++s->settle<3)return;
            mouse(window,QEvent::MouseButtonPress,point(.25,.6),Qt::LeftButton,Qt::LeftButton);
            mouse(window,QEvent::MouseMove,point(.75,.6),Qt::NoButton,Qt::LeftButton);mouse(window,QEvent::MouseButtonRelease,point(.75,.6),Qt::LeftButton,Qt::NoButton);
            const auto doc=c->gpuPlan(true).state.brushMasks;
            if(doc.layers.size()!=2){finish("Zoom pointer stroke did not create the selected new layer");return;}
            const auto &dabs=doc.layers.back().strokes.back().dabs;
            s->zoom=std::abs(dabs.front().center.x-(s->frame.x()+.25*s->frame.width()))<1e-6
                &&std::abs(dabs.back().center.x-(s->frame.x()+.75*s->frame.width()))<1e-6
                &&std::abs(dabs.front().center.y-(s->frame.y()+.6*s->frame.height()))<1e-6;
            s->phase=8;s->settle=0;
        }else if(s->phase==8) {
            if(!c->scopePlotCurrent()||c->scopesPixelCount()!=2659021||++s->settle<3)return;
            if(!window->grabWindow().save(reportPath+".zoom.png")){finish("Cannot save zoomed brush UI");return;}finish({});
        }
    });
    window->setProperty("workspaceIndex",1);window->resize(1180,800);window->requestActivate();
    QImage image(2203,1207,QImage::Format_RGBA64);image.setColorSpace(QColorSpace(QColorSpace::ProPhotoRgb).withTransferFunction(QColorSpace::TransferFunction::Linear));
    for(int y=0;y<image.height();++y)for(int x=0;x<image.width();++x)
        reinterpret_cast<QRgba64 *>(image.scanLine(y))[x]=QRgba64::fromRgba64(12000+x*4,14000+y*5,18000+(x+y)*2,65535);
    if(!dir->isValid()||!image.save(source)||!c->importFile(QUrl::fromLocalFile(source))){finish("Cannot create owned brush fixture");return;}
    s->sourceHash=fileHash(source);timer->start();
}
