#pragma once
#include <QString>
class PhotoController;
class QQuickWindow;
void startSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath, const QString &screenshotPath);
void startDiagnosticSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath);
void startMaskCanvasSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath);
void startRelinkSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath);
