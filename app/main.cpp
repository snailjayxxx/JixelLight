#include <QApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>

#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/CrashReporter.h"
#include "diagnostics/LoggingEngine.h"
#include "diagnostics/ActionTrace.h"

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

    LoggingEngine::install();
    CrashReporter::install();
    ActionTrace::instance().record("app_start", {{"version", JIXELLIGHT_VERSION}, {"git_commit", JIXELLIGHT_GIT_COMMIT}});
    qInfo() << "JixelLight" << JIXELLIGHT_VERSION << "commit" << JIXELLIGHT_GIT_COMMIT;

    QQmlApplicationEngine engine;
    auto *provider = new ProcessedImageProvider;
    engine.addImageProvider("processed", provider);
    PhotoController controller(provider);
    engine.rootContext()->setContextProperty("photoController", &controller);
    engine.rootContext()->setContextProperty("appVersion", QString(JIXELLIGHT_VERSION));

    QObject::connect(&engine, &QQmlApplicationEngine::objectCreationFailed, &app, [] { QCoreApplication::exit(-1); }, Qt::QueuedConnection);
    engine.loadFromModule("JixelLight", "Main");
    return app.exec();
}
