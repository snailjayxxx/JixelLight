#include "app/ShortcutLabels.h"
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
    arguments.addPositionalArgument("photos", "Photographs to import", "[photos...]");
    arguments.addOption({"smoke-report", "Run deterministic GUI validation and write a JSON report", "file"});
    arguments.addOption({"diagnostic-smoke-report", "Validate a frozen diagnostic snapshot using an owned small image", "file"});
    arguments.addOption({"mask-canvas-smoke-report", "Validate local mask handles with an owned synthetic image", "file"});
    arguments.addOption({"relink-smoke-report", "Validate missing-source recovery with owned synthetic images", "file"});
    arguments.addOption({"brush-canvas-smoke-report", "Validate actual brush pointer gestures with owned images", "file"});
    arguments.addOption({"sony-brush-canvas-smoke-report", "Validate real Sony large-image brush input; private report/captures, requires one pinned ARW", "file"});
    arguments.addOption({"brush-erase-canvas-smoke-report", "Validate native Paint/Erase mode with an owned image and controls-only captures", "file"});
    arguments.addOption({"transparent-brush-erase-canvas-smoke-report", "Validate transparent native Paint/Erase composition with an owned image", "file"});
    arguments.addOption({"sony-brush-erase-canvas-smoke-report", "Validate native Erase using exactly one pinned real Sony ARW; controls-only captures", "file"});
    arguments.addOption({"brush-pressure-canvas-smoke-report", "Validate controlled pressure and input ownership on an owned photo", "file"});
    arguments.addOption({"transparent-brush-pressure-canvas-smoke-report", "Validate controlled pressure on transparent owned pixels", "file"});
    arguments.addOption({"sony-brush-pressure-canvas-smoke-report", "Validate pinned real Sony pressure and exact export; controls-only captures", "file"});
    arguments.addOption({"shortcut-help-smoke-report", "Validate the actual shortcut guide and modal input ownership with owned pixels", "file"});
    arguments.addOption({"screenshot", "Screenshot path for GUI validation", "file"});
    arguments.process(app);
    const int smokeModes=int(arguments.isSet("shortcut-help-smoke-report"))+int(arguments.isSet("brush-pressure-canvas-smoke-report"))+int(arguments.isSet("transparent-brush-pressure-canvas-smoke-report"))+int(arguments.isSet("sony-brush-pressure-canvas-smoke-report"))+int(arguments.isSet("smoke-report"))+int(arguments.isSet("diagnostic-smoke-report"))
        +int(arguments.isSet("mask-canvas-smoke-report"))+int(arguments.isSet("relink-smoke-report"))+int(arguments.isSet("brush-canvas-smoke-report"))
        +int(arguments.isSet("sony-brush-canvas-smoke-report"))+int(arguments.isSet("brush-erase-canvas-smoke-report"))
        +int(arguments.isSet("sony-brush-erase-canvas-smoke-report"))+int(arguments.isSet("transparent-brush-erase-canvas-smoke-report"));
    if(smokeModes>1)return 2;
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
    QQmlApplicationEngine engine;
    engine.addImageProvider("processed", provider);
    engine.addImageProvider("thumbnails", new ThumbnailProvider);
    engine.rootContext()->setContextProperty("photoController", &controller);
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

    QVariantList photos;
    for (const QString &path:arguments.positionalArguments()) photos.push_back(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    if(arguments.isSet("shortcut-help-smoke-report")) {
        if(!window||!photos.isEmpty())return 2;
        startShortcutHelpSmokeRun(&controller,window,arguments.value("shortcut-help-smoke-report"));
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
