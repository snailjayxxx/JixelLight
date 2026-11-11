#include "app/ShortcutLabels.h"
#include "app/RecipeClipboardReader.h"
#include <QApplication>
#include "app/BrushTabletInput.h"
#include <QFileInfo>
#include <QDir>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QQuickGraphicsConfiguration>
#include <QScreen>
#include <QTimer>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <memory>
#include "app/SmokeRun.h"

#include "app/PhotoController.h"
#include "core/color/MonitorColorTransform.h"
#include "core/gpu/GpuPreviewItem.h"
#include "core/image/ProcessedImageProvider.h"
#include "core/image/ThumbnailProvider.h"
#include "diagnostics/CrashReporter.h"
#include "diagnostics/LoggingEngine.h"
#include "diagnostics/ActionTrace.h"
#include "diagnostics/PerformanceRecorder.h"

#ifndef JIXELLIGHT_VERSION
#define JIXELLIGHT_VERSION "dev"
#endif
#ifndef JIXELLIGHT_GIT_COMMIT
#define JIXELLIGHT_GIT_COMMIT "unknown"
#endif

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    app.setApplicationName("JixelLight");
    app.setOrganizationName("JixelLight");
    app.setApplicationVersion(JIXELLIGHT_VERSION);
    QQuickStyle::setStyle("Fusion");
    QCommandLineParser arguments;arguments.setApplicationDescription("JixelLight RAW editor");
    arguments.addHelpOption();arguments.addVersionOption();
    arguments.addOption({"texture-smoke-report", "Validate actual texture controls, scopes, durable state and export on owned pixels", "file"});
    arguments.addOption({"texture-language", "Language for owned texture validation", "language", "en_US"});
    arguments.addOption({"clarity-smoke-report", "Validate actual clarity controls, scopes, durable state and export on owned pixels", "file"});
    arguments.addOption({"clarity-language", "Language for owned clarity validation", "language", "en_US"});
    arguments.addOption({"denoise-smoke-report", "Validate actual denoise controls, scopes, durable state and export on owned pixels", "file"});
    arguments.addOption({"denoise-language", "Language for owned denoise validation", "language", "en_US"});
    arguments.addOption({"tangential-geometry-smoke-report", "Validate manual tangential geometry through the actual application", "file"});
    arguments.addOption({"tangential-geometry-language", "Language for the owned tangential validation", "language", "en_US"});
    arguments.addOption({"brush-raw-qpa-smoke-report", "Validate a finite unfiltered pressure trace through the normal photo canvas", "file"});
    arguments.addOption({"brush-raw-qpa-language", "Language for the finite raw pressure probe", "language", "en_US"});
    arguments.addOption({"project-menu-layout-smoke-report", "Validate localized project-menu layout and anchored pointer presentation without importing a photo", "file"});
    arguments.addPositionalArgument("photos", "Photographs to import", "[photos...]");
    arguments.addOption({"xmp-merge-smoke-report", "Validate normal XMP review and durable merge in the actual application using owned fixtures", "file"});
    arguments.addOption({"xmp-merge-smoke-language", "Language for the owned XMP application validation", "language", "en_US"});
    arguments.addOption({"native-brush-delivery-smoke-report", "Validate actual queued native delivery, cache reuse and complete CPU fallback on owned pixels", "file"});
    arguments.addOption({"native-brush-delivery-focus", "Run one new delivery boundary: all/cache/revision/photo/project/hard/detail/lut/pending/retired-render", "case", "all"});
    arguments.addOption({"native-brush-monitor-smoke-report", "Validate actual native QtQuick identity/DisplayP3 monitor presentation on owned pixels", "file"});
    arguments.addOption({"native-brush-sony-smoke-report", "Validate genuine Sony native committed preview with exact full CPU scopes and export", "file"});
    arguments.addOption({"native-brush-route-smoke-report", "Validate native brush controller/QtQuick routing with owned pixels", "file"});
    arguments.addOption({"smoke-report", "Run deterministic GUI validation and write a JSON report", "file"});
    arguments.addOption({"diagnostic-smoke-report", "Validate a frozen diagnostic snapshot using an owned small image", "file"});
    arguments.addOption({"mask-canvas-smoke-report", "Validate local mask handles with an owned synthetic image", "file"});
    arguments.addOption({"relink-smoke-report", "Validate missing-source recovery with owned synthetic images", "file"});
    arguments.addOption({"brush-canvas-smoke-report", "Validate actual brush pointer gestures with owned images", "file"});
    arguments.addOption({"sony-brush-canvas-smoke-report", "Validate real Sony large-image brush input; private report/captures, requires one pinned ARW", "file"});
    arguments.addOption({"brush-erase-canvas-smoke-report", "Validate native Paint/Erase mode with an owned image and controls-only captures", "file"});
    arguments.addOption({"transparent-brush-erase-canvas-smoke-report", "Validate transparent native Paint/Erase composition with an owned image", "file"});
    arguments.addOption({"sony-brush-erase-canvas-smoke-report", "Validate native Erase using exactly one pinned real Sony ARW; controls-only captures", "file"});
    arguments.addOption({"brush-pressure-edges-smoke-report", "Validate zero pressure and stationary positive pressure on an owned photo", "file"});
    arguments.addOption({"transparent-brush-pressure-edges-smoke-report", "Validate zero pressure and stationary positive pressure on transparent owned pixels", "file"});
    arguments.addOption({"brush-recovery-smoke-report", "Validate explicit prefix recovery, discard and Escape on an owned photo", "file"});
    arguments.addOption({"transparent-brush-recovery-smoke-report", "Validate explicit prefix recovery on transparent owned pixels", "file"});
    arguments.addOption({"brush-recovery-erase", "Exercise Erase in the explicit prefix recovery validation"});
    arguments.addOption({"brush-stabilization-smoke-report", "Validate explicitly selected pen movement stabilization with owned pixels", "file"});
    arguments.addOption({"transparent-brush-stabilization-smoke-report", "Validate selected pen stabilization with transparent owned pixels", "file"});
    arguments.addOption({"brush-stabilization-erase", "Exercise Erase in the selected pen stabilization validation"});
    arguments.addOption({"brush-pressure-canvas-smoke-report", "Validate controlled pressure and input ownership on an owned photo", "file"});
    arguments.addOption({"transparent-brush-pressure-canvas-smoke-report", "Validate controlled pressure on transparent owned pixels", "file"});
    arguments.addOption({"sony-brush-stabilized-pressure-canvas-smoke-report", "Validate explicitly selected pen stabilization on pinned Sony RAW and exact export", "file"});
    arguments.addOption({"sony-brush-pressure-canvas-smoke-report", "Validate pinned real Sony pressure and exact export; controls-only captures", "file"});
    arguments.addOption({"shortcut-help-smoke-report", "Validate the actual shortcut guide and modal input ownership with owned pixels", "file"});
    arguments.addOption({"edit-recipe-smoke-report", "Validate the edit recipe dialog with an in-memory clipboard and owned pixels", "file"});
    arguments.addOption({"edit-recipe-clipboard-smoke-report", "Validate recipe clipboard using offscreen or explicitly isolated CI clipboard", "file"});
    arguments.addOption({"screenshot", "Screenshot path for GUI validation", "file"});
    arguments.addOption({"one-pixel-brush-stabilization-smoke-report", "Validate explicitly selected one image pixel pen input with owned pixels", "file"});
    arguments.addOption({"transparent-one-pixel-brush-stabilization-smoke-report", "Validate explicit one pixel input on transparent owned pixels", "file"});
    arguments.addOption({"sony-one-pixel-brush-pressure-canvas-smoke-report", "Validate explicit one pixel pen input on pinned Sony RAW and exact export", "file"});
    arguments.process(app);
    const int smokeModes=int(arguments.isSet("texture-smoke-report"))+int(arguments.isSet("clarity-smoke-report"))+int(arguments.isSet("denoise-smoke-report"))+int(arguments.isSet("tangential-geometry-smoke-report"))+int(arguments.isSet("brush-raw-qpa-smoke-report"))+int(arguments.isSet("project-menu-layout-smoke-report"))+int(arguments.isSet("xmp-merge-smoke-report"))+int(arguments.isSet("native-brush-monitor-smoke-report"))+int(arguments.isSet("native-brush-sony-smoke-report"))+int(arguments.isSet("native-brush-delivery-smoke-report"))+int(arguments.isSet("native-brush-route-smoke-report"))+int(arguments.isSet("one-pixel-brush-stabilization-smoke-report"))+int(arguments.isSet("transparent-one-pixel-brush-stabilization-smoke-report"))+int(arguments.isSet("sony-one-pixel-brush-pressure-canvas-smoke-report"))+int(arguments.isSet("sony-brush-stabilized-pressure-canvas-smoke-report"))+int(arguments.isSet("brush-pressure-edges-smoke-report"))+int(arguments.isSet("transparent-brush-pressure-edges-smoke-report"))+int(arguments.isSet("edit-recipe-clipboard-smoke-report"))+int(arguments.isSet("edit-recipe-smoke-report"))+int(arguments.isSet("shortcut-help-smoke-report"))+int(arguments.isSet("brush-pressure-canvas-smoke-report"))+int(arguments.isSet("transparent-brush-pressure-canvas-smoke-report"))+int(arguments.isSet("sony-brush-pressure-canvas-smoke-report"))+int(arguments.isSet("smoke-report"))+int(arguments.isSet("diagnostic-smoke-report"))
        +int(arguments.isSet("mask-canvas-smoke-report"))+int(arguments.isSet("relink-smoke-report"))+int(arguments.isSet("brush-canvas-smoke-report"))
        +int(arguments.isSet("sony-brush-canvas-smoke-report"))+int(arguments.isSet("brush-erase-canvas-smoke-report"))
        +int(arguments.isSet("sony-brush-erase-canvas-smoke-report"))+int(arguments.isSet("transparent-brush-erase-canvas-smoke-report"))
        +int(arguments.isSet("brush-recovery-smoke-report"))+int(arguments.isSet("transparent-brush-recovery-smoke-report"))
        +int(arguments.isSet("brush-stabilization-smoke-report"))+int(arguments.isSet("transparent-brush-stabilization-smoke-report"));
    if(smokeModes>1)return 2;
    if(arguments.isSet("texture-language")&&!arguments.isSet("texture-smoke-report"))return 2;
    if(arguments.isSet("texture-smoke-report")) {
        for(const auto &option:arguments.optionNames())if(option!="texture-smoke-report"&&option!="texture-language")return 2;
        const QFileInfo output(arguments.value("texture-smoke-report"));
        if(!arguments.positionalArguments().isEmpty()||!QStringList{"en_US","zh_CN"}.contains(arguments.value("texture-language"))
            ||output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()||!QFileInfo(output.absolutePath()).isDir()
            ||!QDir(output.absolutePath()).entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty())return 2;
    }
    if(arguments.isSet("clarity-language")&&!arguments.isSet("clarity-smoke-report"))return 2;
    if(arguments.isSet("clarity-smoke-report")) {
        for(const auto &option:arguments.optionNames())if(option!="clarity-smoke-report"&&option!="clarity-language")return 2;
        const QFileInfo output(arguments.value("clarity-smoke-report"));
        if(!arguments.positionalArguments().isEmpty()||!QStringList{"en_US","zh_CN"}.contains(arguments.value("clarity-language"))
            ||output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()||!QFileInfo(output.absolutePath()).isDir()
            ||!QDir(output.absolutePath()).entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty())return 2;
    }
    if(arguments.isSet("denoise-language")&&!arguments.isSet("denoise-smoke-report"))return 2;
    if(arguments.isSet("denoise-smoke-report")) {
        for(const auto &option:arguments.optionNames())if(option!="denoise-smoke-report"&&option!="denoise-language")return 2;
        const QFileInfo output(arguments.value("denoise-smoke-report"));
        if(!arguments.positionalArguments().isEmpty()||!QStringList{"en_US","zh_CN"}.contains(arguments.value("denoise-language"))
            ||output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()||!QFileInfo(output.absolutePath()).isDir()
            ||!QDir(output.absolutePath()).entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty())return 2;
    }
    if(arguments.isSet("tangential-geometry-language")&&!arguments.isSet("tangential-geometry-smoke-report"))return 2;
    if(arguments.isSet("tangential-geometry-smoke-report")) {
        for(const auto &option:arguments.optionNames())if(option!="tangential-geometry-smoke-report"&&option!="tangential-geometry-language")return 2;
        const QFileInfo output(arguments.value("tangential-geometry-smoke-report"));
        if(!arguments.positionalArguments().isEmpty()||!QStringList{"en_US","zh_CN"}.contains(arguments.value("tangential-geometry-language"))
           ||output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()||!QFileInfo(output.absolutePath()).isDir()
           ||!QDir(output.absolutePath()).entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty())return 2;
    }
    if(arguments.isSet("brush-raw-qpa-language")&&!arguments.isSet("brush-raw-qpa-smoke-report"))return 2;
    if(arguments.isSet("brush-raw-qpa-smoke-report")) {
        for(const auto &option:arguments.optionNames())if(option!="brush-raw-qpa-smoke-report"&&option!="brush-raw-qpa-language")return 2;
        const QFileInfo output(arguments.value("brush-raw-qpa-smoke-report"));
        const QFileInfo parent(output.absolutePath());
        if(!arguments.positionalArguments().isEmpty()||!QStringList{"en_US","zh_CN"}.contains(arguments.value("brush-raw-qpa-language"))
           ||output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()||!parent.isDir()||parent.isSymLink()
           ||!QDir(output.absolutePath()).entryList({output.fileName()+".*"},QDir::Files|QDir::Hidden|QDir::System).isEmpty())return 2;
    }
    if(arguments.isSet("project-menu-layout-smoke-report")) {
        for(const QString &option:arguments.optionNames())if(option!="project-menu-layout-smoke-report")return 2;
        if(!arguments.positionalArguments().isEmpty()||arguments.isSet("screenshot"))return 2;
        const QFileInfo output(arguments.value("project-menu-layout-smoke-report"));
        if(output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()||!QFileInfo(output.absolutePath()).isDir())return 2;
        for(int i=0;i<4;++i){const QFileInfo image(output.absoluteFilePath()+".case-"+QString::number(i)+".png");if(image.exists()||image.isSymLink())return 2;}
    }

    if(arguments.isSet("xmp-merge-smoke-language")&&!arguments.isSet("xmp-merge-smoke-report"))return 2;
    if(arguments.isSet("xmp-merge-smoke-report")) {
        if(!arguments.positionalArguments().isEmpty()||arguments.isSet("screenshot")
           ||!QStringList{"en_US","zh_CN"}.contains(arguments.value("xmp-merge-smoke-language")))return 2;
        const QFileInfo output(arguments.value("xmp-merge-smoke-report"));
        if(output.suffix().compare("json",Qt::CaseInsensitive)!=0||output.exists()||output.isSymLink()
           ||!QFileInfo(output.absolutePath()).isDir())return 2;
        for(const auto &suffix:QStringList{".review.png",".applied.png",".undo.png"}) {
            const QFileInfo capture(output.absoluteFilePath()+suffix);
            if(capture.exists()||capture.isSymLink())return 2;
        }
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    if(arguments.isSet("native-brush-delivery-focus")&&!arguments.isSet("native-brush-delivery-smoke-report"))return 2;
    if(!QStringList{"all","cache","revision","photo","project","hard","detail","lut","pending","retired-render"}.contains(arguments.value("native-brush-delivery-focus")))return 2;
    if(arguments.isSet("brush-recovery-erase")&&!arguments.isSet("brush-recovery-smoke-report")
        &&!arguments.isSet("transparent-brush-recovery-smoke-report"))return 2;
    if(arguments.isSet("brush-stabilization-erase")&&!arguments.isSet("brush-stabilization-smoke-report")
        &&!arguments.isSet("transparent-brush-stabilization-smoke-report")
        &&!arguments.isSet("one-pixel-brush-stabilization-smoke-report")&&!arguments.isSet("transparent-one-pixel-brush-stabilization-smoke-report"))return 2;
    std::unique_ptr<QTemporaryDir> smokeSettings;
    if(smokeModes) {
        // HOME/APPDATA overrides alone do not isolate native CFPreferences or
        // the Windows registry. Keep QA settings in a process-owned INI store.
        smokeSettings=std::make_unique<QTemporaryDir>(QDir::tempPath()+"/JixelLight-smoke-settings-XXXXXX");
        if(!smokeSettings->isValid())return 1;
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat,QSettings::UserScope,smokeSettings->path());
        QSettings::setPath(QSettings::IniFormat,QSettings::SystemScope,smokeSettings->path());
        QStandardPaths::setTestModeEnabled(true);
        app.setProperty("jixellightSmokeSettingsIsolated",true);
    }

    LoggingEngine::install();
    CrashReporter::install();
    ActionTrace::instance().record("app_start", {{"version", JIXELLIGHT_VERSION}, {"git_commit", JIXELLIGHT_GIT_COMMIT}});
    qInfo() << "JixelLight" << JIXELLIGHT_VERSION << "commit" << JIXELLIGHT_GIT_COMMIT;

    qmlRegisterType<GpuPreviewItem>("JixelLight.Native", 1, 0, "GpuPreview");
    qmlRegisterType<BrushTabletInput>("JixelLight.Native",1,0,"BrushTabletInput");
    auto *provider = new ProcessedImageProvider;
    PhotoController controller(provider);
    RecipeClipboardReader recipeClipboard;
    QQmlApplicationEngine engine;
    QQmlEngine::setObjectOwnership(&recipeClipboard,QQmlEngine::CppOwnership);
    engine.addImageProvider("processed", provider);
    engine.addImageProvider("thumbnails", new ThumbnailProvider);
    engine.rootContext()->setContextProperty("photoController", &controller);
    engine.rootContext()->setContextProperty("recipeClipboard", &recipeClipboard);
    engine.rootContext()->setContextProperty("appVersion", QString(JIXELLIGHT_VERSION));
    engine.rootContext()->setContextProperty("standardShortcutLabels", nativeShortcutLabels());

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("JixelLight", "Main");
    if (engine.rootObjects().isEmpty()) return 1;
    auto *window=qobject_cast<QQuickWindow *>(engine.rootObjects().first());
    if (window && qEnvironmentVariableIsSet("JIXELLIGHT_GPU_TIMESTAMPS")) {
        auto config=window->graphicsConfiguration();config.setTimestamps(true);window->setGraphicsConfiguration(config);
    }

    // Display color management must not depend on the GPU preview item being
    // instantiated. In explicit CPU/software mode the RHI item is deliberately
    // absent, but placeholders, references and CPU previews still need the same
    // monitor ICC transform. The GPU item independently consumes the same
    // profile/LUT for its final display shader when GPU rendering is enabled.
    if (window) {
        const auto refreshDisplayColor=[window,&controller] {
            constexpr int LutSize=33;
            const MonitorColorProfile profile=MonitorColorTransform::profileForScreen(window->screen());
            QString error;
            QImage lut=profile.valid ? MonitorColorTransform::srgbToMonitorLut(profile.icc,LutSize,&error)
                                     : MonitorColorTransform::identityLut(LutSize);
            QString key=profile.valid ? QStringLiteral("icc:")+profile.key : QStringLiteral("identity-srgb");
            if(lut.isNull()) {
                lut=MonitorColorTransform::identityLut(LutSize);
                key=QStringLiteral("identity-after-icc-error:")+profile.key;
                PerformanceRecorder::value("display_monitor_icc_error",error);
            }
            if(lut.isNull()) return;
            controller.setDisplayColorLut(lut,key);
            PerformanceRecorder::value("display_monitor_profile",
                profile.valid ? profile.description : QStringLiteral("sRGB identity fallback"));
            if(profile.valid)PerformanceRecorder::value("display_monitor_profile_path",profile.sourcePath);
        };
        QObject::connect(window,&QWindow::screenChanged,&app,[refreshDisplayColor](QScreen *) {refreshDisplayColor();});
        QObject::connect(window,&QWindow::activeChanged,&app,[window,refreshDisplayColor] {
            if(window->isActive())refreshDisplayColor();
        });
        QTimer::singleShot(0,&app,refreshDisplayColor);
    }

    if(arguments.isSet("texture-smoke-report")){if(!window)return 2;startTextureSmokeRun(&controller,window,arguments.value("texture-smoke-report"),arguments.value("texture-language"));return app.exec();}
    if(arguments.isSet("clarity-smoke-report")){if(!window)return 2;startClaritySmokeRun(&controller,window,arguments.value("clarity-smoke-report"),arguments.value("clarity-language"));return app.exec();}
    if(arguments.isSet("denoise-smoke-report")){if(!window)return 2;startDenoiseSmokeRun(&controller,window,arguments.value("denoise-smoke-report"),arguments.value("denoise-language"));return app.exec();}
    if(arguments.isSet("tangential-geometry-smoke-report")){if(!window)return 2;startTangentialGeometrySmokeRun(&controller,window,arguments.value("tangential-geometry-smoke-report"),arguments.value("tangential-geometry-language"));return app.exec();}
    if(arguments.isSet("brush-raw-qpa-smoke-report")){if(!window)return 2;startRawPressureEventSmokeRun(&controller,window,arguments.value("brush-raw-qpa-smoke-report"),arguments.value("brush-raw-qpa-language"));return app.exec();}
    QVariantList photos;
    for (const QString &path:arguments.positionalArguments()) photos.push_back(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    if(arguments.isSet("project-menu-layout-smoke-report")){if(!window||!photos.isEmpty())return 2;startProjectMenuLayoutSmokeRun(&controller,window,arguments.value("project-menu-layout-smoke-report"));return app.exec();}
    if(arguments.isSet("xmp-merge-smoke-report")){if(!window||!photos.isEmpty())return 2;startXmpMergeSmokeRun(&controller,window,arguments.value("xmp-merge-smoke-report"),arguments.value("xmp-merge-smoke-language"));return app.exec();}
    if(arguments.isSet("native-brush-monitor-smoke-report")){if(!window||!photos.isEmpty())return 2;startNativeBrushMonitorSmokeRun(&controller,window,arguments.value("native-brush-monitor-smoke-report"));return app.exec();}
    if(arguments.isSet("native-brush-sony-smoke-report")){if(!window||photos.size()!=1)return 2;startNativeBrushSonySmokeRun(&controller,window,photos.front().toUrl().toLocalFile(),arguments.value("native-brush-sony-smoke-report"));return app.exec();}
    if(arguments.isSet("native-brush-delivery-smoke-report")){if(!window||!photos.isEmpty())return 2;startNativeBrushDeliverySmokeRun(&controller,window,arguments.value("native-brush-delivery-smoke-report"),arguments.value("native-brush-delivery-focus"));return app.exec();}
    if(arguments.isSet("native-brush-route-smoke-report")){if(!window||!photos.isEmpty())return 2;startNativeBrushRouteSmokeRun(&controller,window,arguments.value("native-brush-route-smoke-report"));return app.exec();}
    if(arguments.isSet("edit-recipe-clipboard-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startEditRecipeClipboardSmokeRun(&controller,window,arguments.value("edit-recipe-clipboard-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("edit-recipe-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startEditRecipeSmokeRun(&controller,window,arguments.value("edit-recipe-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("shortcut-help-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startShortcutHelpSmokeRun(&controller,window,arguments.value("shortcut-help-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("sony-one-pixel-brush-pressure-canvas-smoke-report")) {
        if(!window||photos.size()!=1)return 2;
        app.setProperty("jixellightSmokeSonyPenStabilization",true);
        startSonyOnePixelBrushPressureCanvasSmokeRun(&controller,window,photos.front().toUrl().toLocalFile(),arguments.value("sony-one-pixel-brush-pressure-canvas-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("one-pixel-brush-stabilization-smoke-report")||arguments.isSet("transparent-one-pixel-brush-stabilization-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        const bool transparent=arguments.isSet("transparent-one-pixel-brush-stabilization-smoke-report");
        startOnePixelBrushStabilizationSmokeRun(&controller,window,arguments.value(transparent?"transparent-one-pixel-brush-stabilization-smoke-report":"one-pixel-brush-stabilization-smoke-report"),transparent,arguments.isSet("brush-stabilization-erase"));
        return app.exec();
    }
    if(arguments.isSet("sony-brush-stabilized-pressure-canvas-smoke-report")) {
        if(!window||photos.size()!=1)return 2;
        app.setProperty("jixellightSmokeSonyPenStabilization",true);
        startSonyBrushPressureCanvasSmokeRun(&controller,window,photos.front().toUrl().toLocalFile(),arguments.value("sony-brush-stabilized-pressure-canvas-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("sony-brush-pressure-canvas-smoke-report")) {
        if(!window||photos.size()!=1)return 2;
        startSonyBrushPressureCanvasSmokeRun(&controller,window,photos.front().toUrl().toLocalFile(),arguments.value("sony-brush-pressure-canvas-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("sony-brush-erase-canvas-smoke-report")) {
        if(!window||photos.size()!=1)return 2;
        startSonyBrushEraseCanvasSmokeRun(&controller,window,photos.front().toUrl().toLocalFile(),arguments.value("sony-brush-erase-canvas-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("sony-brush-canvas-smoke-report")) {
        if(!window||photos.size()!=1)return 2;
        startSonyBrushCanvasSmokeRun(&controller,window,photos.front().toUrl().toLocalFile(),arguments.value("sony-brush-canvas-smoke-report"));
        return app.exec();
    }
    if(arguments.isSet("brush-stabilization-smoke-report")||arguments.isSet("transparent-brush-stabilization-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        const bool transparent=arguments.isSet("transparent-brush-stabilization-smoke-report");
        startBrushStabilizationSmokeRun(&controller,window,arguments.value(transparent?"transparent-brush-stabilization-smoke-report":"brush-stabilization-smoke-report"),transparent,arguments.isSet("brush-stabilization-erase"));
        return app.exec();
    }
    if(arguments.isSet("brush-recovery-smoke-report")||arguments.isSet("transparent-brush-recovery-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        const bool transparent=arguments.isSet("transparent-brush-recovery-smoke-report");
        startBrushRecoverySmokeRun(&controller,window,arguments.value(transparent?"transparent-brush-recovery-smoke-report":"brush-recovery-smoke-report"),transparent,arguments.isSet("brush-recovery-erase"));
        return app.exec();
    }
    if(arguments.isSet("brush-pressure-edges-smoke-report")||arguments.isSet("transparent-brush-pressure-edges-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        const bool transparent=arguments.isSet("transparent-brush-pressure-edges-smoke-report");
        startBrushPressureEdgesSmokeRun(&controller,window,arguments.value(transparent?"transparent-brush-pressure-edges-smoke-report":"brush-pressure-edges-smoke-report"),transparent);
        return app.exec();
    }
    if(arguments.isSet("brush-pressure-canvas-smoke-report")||arguments.isSet("transparent-brush-pressure-canvas-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        const bool transparent=arguments.isSet("transparent-brush-pressure-canvas-smoke-report");
        startBrushPressureCanvasSmokeRun(&controller,window,arguments.value(transparent?"transparent-brush-pressure-canvas-smoke-report":"brush-pressure-canvas-smoke-report"),transparent);
        return app.exec();
    }
    if (!photos.isEmpty()) controller.importFiles(photos);
    if (arguments.isSet("smoke-report")) {
        if (!window || photos.isEmpty()) return 2;
        startSmokeRun(&controller,window,arguments.value("smoke-report"),arguments.value("screenshot"));
    }
    if (arguments.isSet("diagnostic-smoke-report")) {
        if (!window || !photos.isEmpty()) return 2;
        startDiagnosticSmokeRun(&controller,window,arguments.value("diagnostic-smoke-report"));
    }
    if (arguments.isSet("mask-canvas-smoke-report")) {
        if (!window || !photos.isEmpty()) return 2;
        startMaskCanvasSmokeRun(&controller,window,arguments.value("mask-canvas-smoke-report"));
    }
    if (arguments.isSet("relink-smoke-report")) {
        if (!window || !photos.isEmpty()) return 2;
        startRelinkSmokeRun(&controller,window,arguments.value("relink-smoke-report"));
    }
    if(arguments.isSet("transparent-brush-erase-canvas-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startTransparentBrushEraseCanvasSmokeRun(&controller,window,arguments.value("transparent-brush-erase-canvas-smoke-report"));
    }
    if(arguments.isSet("brush-erase-canvas-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startBrushEraseCanvasSmokeRun(&controller,window,arguments.value("brush-erase-canvas-smoke-report"));
    }
    if (arguments.isSet("brush-canvas-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startBrushCanvasSmokeRun(&controller,window,arguments.value("brush-canvas-smoke-report"));
    }
    return app.exec();
}
