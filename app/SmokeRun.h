#pragma once
#include <QString>
class PhotoController;
class QQuickWindow;
void startSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath, const QString &screenshotPath);
void startDiagnosticSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath);
void startMaskCanvasSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath);
void startRelinkSmokeRun(PhotoController *controller, QQuickWindow *window, const QString &reportPath);
void startBrushCanvasSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &reportPath);
void startSonyBrushCanvasSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &sourcePath,const QString &reportPath);
void startBrushEraseCanvasSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &reportPath);
void startTransparentBrushEraseCanvasSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &reportPath);
void startSonyBrushEraseCanvasSmokeRun(PhotoController *controller,QQuickWindow *window,const QString &sourcePath,const QString &reportPath);

void startBrushPressureEdgesSmokeRun(PhotoController *,QQuickWindow *,const QString &,bool transparent=false);
void startBrushRecoverySmokeRun(PhotoController *,QQuickWindow *,const QString &,bool transparent,bool erase);
void startBrushStabilizationSmokeRun(PhotoController *,QQuickWindow *,const QString &,bool transparent,bool erase);
void startBrushPressureCanvasSmokeRun(PhotoController *,QQuickWindow *,const QString &,bool transparent=false);
void startSonyBrushPressureCanvasSmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);

void startShortcutHelpSmokeRun(PhotoController *,QQuickWindow *,const QString &);

void startEditRecipeSmokeRun(PhotoController *,QQuickWindow *,const QString &);

void startEditRecipeClipboardSmokeRun(PhotoController *,QQuickWindow *,const QString &);

void startOnePixelBrushStabilizationSmokeRun(PhotoController *,QQuickWindow *,const QString &,bool transparent,bool erase);
void startSonyOnePixelBrushPressureCanvasSmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);

void startNativeBrushRouteSmokeRun(PhotoController *,QQuickWindow *,const QString &);

void startNativeBrushDeliverySmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &focus);

void startNativeBrushSonySmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);

void startNativeBrushMonitorSmokeRun(PhotoController *,QQuickWindow *,const QString &);

void startXmpMergeSmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);

void startProjectMenuLayoutSmokeRun(PhotoController *,QQuickWindow *,const QString &);

void startRawPressureEventSmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);

void startTangentialGeometrySmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);
