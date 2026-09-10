#include <QApplication>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QCommandLineParser>
#include <QQuickWindow>
#include <QQuickGraphicsConfiguration>
#include <QScreen>
#include <QTimer>
#include "app/SmokeRun.h"

#include "app/PhotoController.h"
#include "core/color/MonitorColorTransform.h"
#include "core/gpu/GpuPreviewItem.h"
#include "core/image/ProcessedImageProvider.h"
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
    arguments.addOption({"screenshot", "Screenshot path for GUI validation", "file"});
    arguments.process(app);

    LoggingEngine::install();
    CrashReporter::install();
    ActionTrace::instance().record("app_start", {{"version", JIXELLIGHT_VERSION}, {"git_commit", JIXELLIGHT_GIT_COMMIT}});
    qInfo() << "JixelLight" << JIXELLIGHT_VERSION << "commit" << JIXELLIGHT_GIT_COMMIT;

    qmlRegisterType<GpuPreviewItem>("JixelLight.Native", 1, 0, "GpuPreview");
    auto *provider = new ProcessedImageProvider;
    PhotoController controller(provider);
    QQmlApplicationEngine engine;
    engine.addImageProvider("processed", provider);
    engine.rootContext()->setContextProperty("photoController", &controller);
    engine.rootContext()->setContextProperty("appVersion", QString(JIXELLIGHT_VERSION));

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
    if (!photos.isEmpty()) controller.importFiles(photos);
    if (arguments.isSet("smoke-report")) {
        if (!window || photos.isEmpty()) return 2;
        startSmokeRun(&controller,window,arguments.value("smoke-report"),arguments.value("screenshot"));
    }
    return app.exec();
}
