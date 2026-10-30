#pragma once

#include <QObject>
class QQuickWindow;
#include <QImage>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <QCache>
#include <algorithm>
#include <cmath>
#include "core/preview/PreviewTasks.h"
#include "core/async/LatestFrameJob.h"
#include "core/export/ExportQueue.h"
#include "core/import/CopyImportQueue.h"
#include "core/look/CameraReference.h"
#include "core/look/LookCalibration.h"

#include "core/pipeline/AdjustmentState.h"
#include "core/commands/EditHistory.h"
#include "core/project/ProjectDatabase.h"
#include "core/scopes/ScopesEngine.h"
#include "core/scopes/ScopePlot.h"

class ProcessedImageProvider;
class RenderedPreviewCache;
class FullScopesCache;
class AnalysisFrameCache;
class ScopePlotCache;
namespace XmpSidecar { struct Document; }

class PhotoController final : public QObject {
    friend void startSonyBrushPressureCanvasSmokeRun(PhotoController *,QQuickWindow *,const QString &,const QString &);
    Q_OBJECT
    Q_PROPERTY(QVariantMap sonyLook READ sonyLook NOTIFY lookChanged)
    Q_PROPERTY(QVariantMap lookState READ lookState NOTIFY lookChanged)
    Q_PROPERTY(QVariantList lookCatalog READ lookCatalog CONSTANT)
    Q_PROPERTY(bool showCameraReference READ showCameraReference WRITE setShowCameraReference NOTIFY referenceChanged)
    Q_PROPERTY(QString cameraReferenceUrl READ cameraReferenceUrl NOTIFY referenceChanged)
    Q_PROPERTY(QVariantMap cameraReferenceInfo READ cameraReferenceInfo NOTIFY referenceChanged)
    Q_PROPERTY(QVariantMap referenceHistogram READ referenceHistogram NOTIFY referenceChanged)
    Q_PROPERTY(bool referenceBusy READ referenceBusy NOTIFY referenceChanged)
    Q_PROPERTY(bool calibrationBusy READ calibrationBusy NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantMap calibrationReport READ calibrationReport NOTIFY calibrationChanged)
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(QVariantList selectedIndices READ selectedIndices NOTIFY libraryChanged)
    Q_PROPERTY(QStringList albumNames READ albumNames NOTIFY libraryChanged)
    Q_PROPERTY(QStringList currentKeywords READ currentKeywords NOTIFY libraryChanged)
    Q_PROPERTY(QStringList currentAlbums READ currentAlbums NOTIFY libraryChanged)
    Q_PROPERTY(QString currentColorLabel READ currentColorLabel NOTIFY libraryChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(int currentRating READ currentRating NOTIFY curationChanged)
    Q_PROPERTY(QString currentFlag READ currentFlag NOTIFY curationChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY previewUrlChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY currentIndexChanged)
    Q_PROPERTY(bool currentSourceAvailable READ currentSourceAvailable NOTIFY currentIndexChanged)
    Q_PROPERTY(qulonglong thumbnailRevision READ thumbnailRevision NOTIFY libraryChanged)
    Q_PROPERTY(bool relinkBusy READ relinkBusy NOTIFY relinkChanged)
    Q_PROPERTY(QVariantMap relinkPreview READ relinkPreview NOTIFY relinkChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentFormat READ currentFormat NOTIFY currentIndexChanged)
    Q_PROPERTY(bool currentIsRaw READ currentIsRaw NOTIFY currentIndexChanged)
    Q_PROPERTY(QString pipelineDescription READ pipelineDescription NOTIFY currentIndexChanged)
    Q_PROPERTY(QVariantMap currentMetadata READ currentMetadata NOTIFY currentMetadataChanged)
    Q_PROPERTY(QString projectName READ projectName NOTIFY projectChanged)
    Q_PROPERTY(QString projectPath READ projectPath NOTIFY projectChanged)
    Q_PROPERTY(QString language READ language WRITE setLanguage NOTIFY languageChanged)
    Q_PROPERTY(QVariantList redHistogram READ redHistogram NOTIFY scopesChanged)
    Q_PROPERTY(QVariantList greenHistogram READ greenHistogram NOTIFY scopesChanged)
    Q_PROPERTY(QVariantList blueHistogram READ blueHistogram NOTIFY scopesChanged)
    Q_PROPERTY(QVariantList lumaHistogram READ lumaHistogram NOTIFY scopesChanged)
    Q_PROPERTY(double shadowClipPercent READ shadowClipPercent NOTIFY scopesChanged)
    Q_PROPERTY(double highlightClipPercent READ highlightClipPercent NOTIFY scopesChanged)

    Q_PROPERTY(QVariantMap geometry READ geometry NOTIFY adjustmentsChanged)
    Q_PROPERTY(bool cropEditing READ cropEditing NOTIFY cropEditingChanged)
    Q_PROPERTY(QRectF cropOverlay READ cropOverlay NOTIFY cropEditingChanged)
    Q_PROPERTY(QStringList presetNames READ presetNames NOTIFY presetsChanged)
    Q_PROPERTY(bool canUndo READ canUndo NOTIFY historyChanged)
    Q_PROPERTY(bool canRedo READ canRedo NOTIFY historyChanged)
    Q_PROPERTY(QVariantList editHistory READ editHistory NOTIFY historyChanged)
    Q_PROPERTY(bool hasAdjustmentClipboard READ hasAdjustmentClipboard NOTIFY adjustmentClipboardChanged)
    Q_PROPERTY(QString adjustmentClipboardName READ adjustmentClipboardName NOTIFY adjustmentClipboardChanged)

    Q_PROPERTY(double exposure READ exposure WRITE setExposure NOTIFY adjustmentsChanged)
    Q_PROPERTY(double temperature READ temperature WRITE setTemperature NOTIFY adjustmentsChanged)
    Q_PROPERTY(double tint READ tint WRITE setTint NOTIFY adjustmentsChanged)
    Q_PROPERTY(double contrast READ contrast WRITE setContrast NOTIFY adjustmentsChanged)
    Q_PROPERTY(double highlights READ highlights WRITE setHighlights NOTIFY adjustmentsChanged)
    Q_PROPERTY(double shadows READ shadows WRITE setShadows NOTIFY adjustmentsChanged)
    Q_PROPERTY(double whites READ whites WRITE setWhites NOTIFY adjustmentsChanged)
    Q_PROPERTY(double blacks READ blacks WRITE setBlacks NOTIFY adjustmentsChanged)
    Q_PROPERTY(double highlightRecovery READ highlightRecovery WRITE setHighlightRecovery NOTIFY adjustmentsChanged)
    Q_PROPERTY(double hue READ hue WRITE setHue NOTIFY adjustmentsChanged)
    Q_PROPERTY(double saturation READ saturation WRITE setSaturation NOTIFY adjustmentsChanged)
    Q_PROPERTY(double vibrance READ vibrance WRITE setVibrance NOTIFY adjustmentsChanged)
    Q_PROPERTY(double vignetteAmount READ vignetteAmount WRITE setVignetteAmount NOTIFY adjustmentsChanged)
    Q_PROPERTY(double vignetteMidpoint READ vignetteMidpoint WRITE setVignetteMidpoint NOTIFY adjustmentsChanged)
    Q_PROPERTY(double vignetteFeather READ vignetteFeather WRITE setVignetteFeather NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantMap linearMask READ linearMask NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantMap radialMask READ radialMask NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList brushLayers READ brushLayers NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantMap brushCapacity READ brushCapacity NOTIFY adjustmentsChanged)
    Q_PROPERTY(bool brushDrawing READ brushDrawing NOTIFY brushDrawingChanged)
    Q_PROPERTY(bool brushCapacityWarning READ brushCapacityWarning NOTIFY brushCapacityWarningChanged)
    Q_PROPERTY(QString brushPreviewUrl READ brushPreviewUrl NOTIFY brushPreviewChanged)
    Q_PROPERTY(bool brushReady READ brushReady NOTIFY activityChanged)
    Q_PROPERTY(QString brushStatus READ brushStatus NOTIFY activityChanged)
    Q_PROPERTY(QSize brushFrameSize READ brushFrameSize NOTIFY previewGeometryChanged)
    Q_PROPERTY(bool blackWhite READ blackWhite WRITE setBlackWhite NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList blackWhiteMix READ blackWhiteMix NOTIFY adjustmentsChanged)
    Q_PROPERTY(bool gradingEnabled READ gradingEnabled WRITE setGradingEnabled NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList gradingHue READ gradingHue NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList gradingSaturation READ gradingSaturation NOTIFY adjustmentsChanged)
    Q_PROPERTY(double gradingBalance READ gradingBalance WRITE setGradingBalance NOTIFY adjustmentsChanged)

    Q_PROPERTY(QVariantList hslHue READ hslHue NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList hslSaturation READ hslSaturation NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList hslLuminance READ hslLuminance NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList masterCurve READ masterCurve NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList redCurve READ redCurve NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList greenCurve READ greenCurve NOTIFY adjustmentsChanged)
    Q_PROPERTY(QVariantList blueCurve READ blueCurve NOTIFY adjustmentsChanged)

    Q_PROPERTY(bool loading READ loading NOTIFY activityChanged)
    Q_PROPERTY(bool previewReady READ previewReady NOTIFY activityChanged)
    Q_PROPERTY(bool rendering READ rendering NOTIFY activityChanged)
    Q_PROPERTY(bool gpuEnabled READ gpuEnabled WRITE setGpuEnabled NOTIFY backendChanged)
    Q_PROPERTY(bool gpuActive READ gpuActive NOTIFY backendChanged)
    Q_PROPERTY(QString processingBackend READ processingBackend NOTIFY backendChanged)
    Q_PROPERTY(QString scopesStatus READ scopesStatus NOTIFY scopesChanged)
    Q_PROPERTY(bool exactScopes READ exactScopes WRITE setExactScopes NOTIFY scopesChanged)
    Q_PROPERTY(qulonglong scopesPixelCount READ scopesPixelCount NOTIFY scopesChanged)
    Q_PROPERTY(QString scopeMode READ scopeMode WRITE setScopeMode NOTIFY scopePlotChanged)
    Q_PROPERTY(QString scopePlotUrl READ scopePlotUrl NOTIFY scopePlotChanged)
    Q_PROPERTY(QString scopePlotStatus READ scopePlotStatus NOTIFY scopePlotChanged)
    Q_PROPERTY(bool scopePlotCurrent READ scopePlotCurrent NOTIFY scopePlotChanged)
    Q_PROPERTY(qulonglong scopePlotPixels READ scopePlotPixels NOTIFY scopePlotChanged)
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(bool copyImportBusy READ copyImportBusy NOTIFY copyImportChanged)
    Q_PROPERTY(quint64 importNameRevision READ importNameRevision NOTIFY importNamePreviewChanged)
    Q_PROPERTY(double copyImportProgress READ copyImportProgress NOTIFY copyImportChanged)
    Q_PROPERTY(QString copyImportStatus READ copyImportStatus NOTIFY copyImportChanged)
    Q_PROPERTY(QSizeF previewDisplaySize READ previewDisplaySize NOTIFY previewGeometryChanged)
    Q_PROPERTY(QRectF previewFrameRect READ previewFrameRect NOTIFY previewGeometryChanged)
    Q_PROPERTY(qulonglong renderRevision READ renderRevision NOTIFY gpuFrameChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)
    Q_PROPERTY(bool diagnosticBusy READ diagnosticBusy NOTIFY diagnosticChanged)

public:
    QVariantMap sonyLook() const { return m_currentMetadata.value("sonyLook").toMap(); }
    QVariantMap lookState() const;
    QVariantList lookCatalog() const;
    bool showCameraReference() const { return m_showReference; }
    QString cameraReferenceUrl() const;
    QVariantMap cameraReferenceInfo() const { return m_referenceInfo; }
    QVariantMap referenceHistogram() const;
    bool referenceBusy() const { return m_referenceBusy; }
    bool calibrationBusy() const { return m_calibrationBusy; }
    QVariantMap calibrationReport() const { return m_calibrationReport; }
    Q_INVOKABLE void setShowCameraReference(bool show);
    Q_INVOKABLE void setLookMode(const QString &mode);
    Q_INVOKABLE void setLookCode(const QString &code);
    Q_INVOKABLE void setLookStrength(double strength);
    Q_INVOKABLE void setLookParameter(const QString &name, double value);
    Q_INVOKABLE void openReferenceDialog();
    Q_INVOKABLE bool loadReference(const QUrl &url);
    Q_INVOKABLE bool calibrateFromReference();
    Q_INVOKABLE void cancelCalibration();
    Q_INVOKABLE void openLookProfileDialog();
    Q_INVOKABLE void saveLookProfileDialog();
    Q_INVOKABLE bool loadLookProfile(const QUrl &url);
    Q_INVOKABLE bool saveLookProfile(const QUrl &url);
    explicit PhotoController(ProcessedImageProvider *provider,QObject *parent=nullptr,
                             std::shared_ptr<BrushRenderBudget> brushBudget={});
    std::shared_ptr<BrushRenderBudget> brushRenderBudget() const {return m_brushBudget;}
    ~PhotoController() override;
    QVariantList library() const;
    bool loading() const { return m_loading; }
    bool previewReady() const { return !m_fullSource.isNull() && !m_previewSource.isNull(); }
    bool rendering() const { return m_rendering; }
    bool gpuEnabled() const { return m_gpuEnabled; }
    bool gpuActive() const { return m_gpuActive; }
    QString processingBackend() const;
    QString scopesStatus() const;
    bool exactScopes() const { return m_exactScopes; }
    qulonglong scopesPixelCount() const { return m_scopes.pixelCount; }
    QString scopeMode() const { return m_scopeMode; }
    Q_INVOKABLE void setScopeMode(const QString &mode);
    QString scopePlotUrl() const;
    QString scopePlotStatus() const;
    bool scopePlotCurrent() const { return !m_scopePlot.image.isNull() && m_scopePlot.error.isEmpty() && m_plotRevision==m_requestedRevision && !m_preparing && m_plotFull==m_exactScopes; }
    qulonglong scopePlotPixels() const { return m_scopePlot.pixels; }
    bool exportBusy() const { return m_exportQueue && m_exportQueue->busy(); }
    double exportProgress() const { return m_exportProgress; }
    QSizeF previewDisplaySize() const { return m_displayPixels / m_devicePixelRatio; }
    QRectF previewFrameRect() const { return m_previewFrameRect; }
    // Returns normalized full-image coordinates for a pointer on the current
    // preview plane (including a zoom/pan ROI). NaN on invalid/unloaded input.
    Q_INVOKABLE QPointF maskCanvasPoint(double x,double y,double width,double height) const;
    qulonglong renderRevision() const { return m_requestedRevision; }
    QImage gpuSource() const { return m_preparing ? QImage{} : m_gpuSource; }
    float rawBaseExposureStops() const {
        if (!currentIsRaw()) return 0.0f;
        bool ok = false;
        const double value = m_currentMetadata.value(QStringLiteral("rawBaseExposureStops"), 0.0).toDouble(&ok);
        return ok && std::isfinite(value) ? float(std::clamp(value, -8.0, 8.0)) : 0.0f;
    }
    ProcessingPlan gpuPlan(bool fullFrame=false) const {
        auto plan=ProcessingPlan::compile(currentState(), ImagePipeline::InputEncoding::LinearProPhoto,
                                       ColorManagement::OutputSpace::SRgb, currentIsRaw(), rawBaseExposureStops());
        if (!fullFrame) plan.setFrameRect(m_previewFrameRect);
        return plan;
    }
    void gpuPresented(quint64 revision, const QString &backend);
    void gpuScopes(quint64 revision, const QByteArray &counts, quint64 pixels);
    void gpuFailed(const QString &message);
    void setDisplayColorLut(const QImage &atlas, const QString &key);
    bool diagnosticBusy() const { return m_diagnosticBusy; }
    quint64 diagnosticRequestId() const;
    quint64 diagnosticRevision() const;
    qint64 diagnosticSourceKey() const;
    void gpuDiagnosticReady(quint64 request, QJsonObject output);
    Q_INVOKABLE void setGpuEnabled(bool enabled);
    Q_INVOKABLE void setExactScopes(bool enabled);
    Q_INVOKABLE void setViewport(double width, double height, double dpr, double zoom, double centerX, double centerY);
    Q_INVOKABLE void finishInteraction();
    Q_INVOKABLE QVariantMap exportNamePreview(const QString &pattern, int sequenceStart=1, const QString &format=QStringLiteral("jpeg")) const;
    Q_INVOKABLE QStringList selectedVersionKeys() const;
    Q_INVOKABLE QVariantMap exportVersionNamePreview(const QStringList &keys, const QString &pattern, int sequenceStart=1, const QString &format=QStringLiteral("jpeg")) const;
    Q_INVOKABLE bool exportVersions(const QStringList &keys, const QUrl &folder, const QString &colorSpaceKey = QStringLiteral("srgb"), int quality = 92, const QString &format = QStringLiteral("jpeg"), const QString &pattern={}, int sequenceStart=1);
    Q_INVOKABLE bool exportAll(const QUrl &folder, const QString &colorSpaceKey = QStringLiteral("srgb"), int quality = 92, const QString &format = QStringLiteral("jpeg"), const QString &pattern={}, int sequenceStart=1);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE bool flushEdits();
    int currentIndex() const { return m_currentIndex; }
    int currentRating() const { return hasImage() ? m_photos[m_currentIndex].rating : 0; }
    QString currentFlag() const { return hasImage() ? m_photos[m_currentIndex].flag : QStringLiteral("none"); }
    QString previewUrl() const;
    bool hasImage() const { return m_currentIndex >= 0 && m_currentIndex < m_photos.size(); }
    bool currentSourceAvailable() const { return hasImage() && m_photos[m_currentIndex].available; }
    qulonglong thumbnailRevision() const { return hasImage()?m_photos[m_currentIndex].thumbnailRevision:0; }
    bool relinkBusy() const { return m_relinkBusy; }
    QVariantMap relinkPreview() const { return m_relinkPreview; }
    Q_INVOKABLE QString relinkCatalogToken() const { return QString::number(m_catalogEpoch); }
    Q_INVOKABLE bool beginRelink(const QString &versionKey, const QUrl &candidate, const QString &catalogToken={});
    Q_INVOKABLE bool confirmRelink(const QString &requestId, bool acknowledgeUnknownIdentity=false);
    Q_INVOKABLE void cancelRelink();
    QString currentFile() const;
    QString currentFormat() const;
    bool currentIsRaw() const;
    QString pipelineDescription() const;
    QVariantMap currentMetadata() const { return m_currentMetadata; }
    QString projectName() const { return m_project.projectName(); }
    QString projectPath() const { return m_project.projectPath(); }
    QString language() const { return m_language; }
    QVariantList redHistogram() const { return m_scopes.red; }
    QVariantList greenHistogram() const { return m_scopes.green; }
    QVariantList blueHistogram() const { return m_scopes.blue; }
    QVariantList lumaHistogram() const { return m_scopes.luma; }
    double shadowClipPercent() const { return m_scopes.shadowClipPercent; }
    double highlightClipPercent() const { return m_scopes.highlightClipPercent; }

    double exposure() const; double temperature() const; double tint() const; double contrast() const;
    double highlights() const; double shadows() const; double whites() const; double blacks() const;
    double highlightRecovery() const; double hue() const; double saturation() const; double vibrance() const;
    double vignetteAmount() const; double vignetteMidpoint() const; double vignetteFeather() const;
    QVariantMap linearMask() const {
        const auto state=currentState();
        return {{"enabled",state.linearMaskEnabled},{"exposure",state.linearMaskExposure},
                {"center",state.linearMaskCenter},{"feather",state.linearMaskFeather},
                {"angle",state.linearMaskAngle}};
    }
    QVariantMap radialMask() const {
        const auto state=currentState();
        return {{"enabled",state.radialMaskEnabled},{"inverted",state.radialMaskInverted},
                {"exposure",state.radialMaskExposure},
                {"centerX",state.radialMaskCenterX},{"centerY",state.radialMaskCenterY},
                {"radiusX",state.radialMaskRadiusX},{"radiusY",state.radialMaskRadiusY},
                {"feather",state.radialMaskFeather},{"angle",state.radialMaskAngle}};
    }
    bool blackWhite() const { return currentState().blackWhite; }
    QVariantList blackWhiteMix() const;
    bool gradingEnabled() const { return currentState().gradingEnabled; }
    QVariantList gradingHue() const; QVariantList gradingSaturation() const;
    double gradingBalance() const { return currentState().gradingBalance; }
    QVariantList hslHue() const; QVariantList hslSaturation() const; QVariantList hslLuminance() const;
    QVariantList masterCurve() const; QVariantList redCurve() const; QVariantList greenCurve() const; QVariantList blueCurve() const;
    QString statusMessage() const { return m_statusMessage; }

    void setExposure(double v); void setTemperature(double v); void setTint(double v); void setContrast(double v);
    void setHighlights(double v); void setShadows(double v); void setWhites(double v); void setBlacks(double v);
    void setHighlightRecovery(double v); void setHue(double v); void setSaturation(double v); void setVibrance(double v);
    void setVignetteAmount(double v); void setVignetteMidpoint(double v); void setVignetteFeather(double v);
    Q_INVOKABLE bool setLinearMaskEnabled(bool enabled);
    Q_INVOKABLE bool setLinearMaskParameter(const QString &parameter,double value);
    Q_INVOKABLE void resetLinearMask();
    Q_INVOKABLE bool setRadialMaskEnabled(bool enabled);
    Q_INVOKABLE bool setRadialMaskInverted(bool inverted);
    Q_INVOKABLE bool setRadialMaskParameter(const QString &parameter,double value);
    // Atomic 2D center change: one undo entry per completed canvas drag.
    Q_INVOKABLE bool setRadialMaskCenter(double x,double y);
    Q_INVOKABLE void resetRadialMask();
    // Legacy C++ draft entrypoint stays disabled-only. Active intent uses
    // the command registry or the guarded pointer-gesture API below.
    bool replaceBrushMaskDraft(const QJsonObject &document);
    QVariantList brushLayers() const;
    QVariantMap brushCapacity() const;
    bool brushDrawing() const { return bool(m_brushGesture); }
    bool brushCapacityWarning() const;
    QString brushPreviewUrl() const {return m_brushPreviewId.isEmpty()?QString{}:QStringLiteral("image://processed/")+m_brushPreviewId;}
    bool brushReady() const;
    QString brushStatus() const;
    QSize brushFrameSize() const;
    Q_INVOKABLE bool beginBrushStroke(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure);
    Q_INVOKABLE bool beginBrushStrokeMode(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode);
    Q_INVOKABLE bool beginBrushStrokeModePressure(const QString &layer,double x,double y,double radius,double hardness,double opacity,double exposure,const QString &mode,double pressure);
    Q_INVOKABLE bool appendBrushStroke(double x,double y,double pressure=1);
    Q_INVOKABLE QString endBrushStroke();
    Q_INVOKABLE void cancelBrushStroke();
    Q_INVOKABLE bool setBrushLayerExposure(const QString &id,double exposure);
    Q_INVOKABLE bool setBrushLayerEnabled(const QString &id,bool enabled);
    Q_INVOKABLE bool removeBrushLayer(const QString &id);
    Q_INVOKABLE bool removeLastBrushStroke(const QString &id);
    Q_INVOKABLE QVariantMap brushStrokeSelection(const QString &id,int index) const;
    Q_INVOKABLE bool removeBrushStroke(const QString &id,int index,const QString &selectionToken);
    void setBlackWhite(bool enabled);
    Q_INVOKABLE void setBlackWhiteMix(int band,double value);
    Q_INVOKABLE void resetBlackWhite();
    void setGradingEnabled(bool enabled);
    void setGradingBalance(double value);
    Q_INVOKABLE void setGradingControl(int region,int component,double value);
    Q_INVOKABLE void setGradingWheel(int region,double hue,double saturation);
    Q_INVOKABLE void resetColorGrading();
    Q_INVOKABLE void setLanguage(const QString &language);

    Q_INVOKABLE void openImportDialog();
    Q_INVOKABLE void openCopyImportDialog();
    Q_INVOKABLE bool copyImport(const QVariantList &urls,const QUrl &directory,
                                const QString &pattern=QString(),int sequenceStart=1);
    Q_INVOKABLE QVariantMap previewImportNames(const QVariantList &urls,const QString &pattern,int sequenceStart);
    Q_INVOKABLE void cancelImportNamePreview();
    quint64 importNameRevision() const { return m_importNameRevision; }
    Q_INVOKABLE void cancelCopyImport();
    Q_INVOKABLE bool prepareToClose();
    bool copyImportBusy() const { return m_copyImportQueue && m_copyImportQueue->busy(); }
    double copyImportProgress() const { return m_copyImportProgress; }
    QString copyImportStatus() const { return m_copyImportStatus; }
    Q_INVOKABLE bool importFile(const QUrl &url);
    Q_INVOKABLE void importFiles(const QVariantList &urls);
    Q_INVOKABLE void selectPhoto(int index);
    Q_INVOKABLE bool createProject(const QUrl &folder, const QString &name);
    Q_INVOKABLE bool openProject(const QUrl &folder);
    Q_INVOKABLE void setRating(int rating);
    Q_INVOKABLE void setFlag(const QString &flag);
    QVariantMap geometry() const { return currentState().geometry.toJson().toVariantMap(); }
    Q_INVOKABLE void rotatePhoto(int quarterTurns);
    Q_INVOKABLE void flipPhoto(bool horizontal);
    Q_INVOKABLE void setCrop(double x, double y, double width, double height);
    Q_INVOKABLE void setStraighten(double degrees);
    Q_INVOKABLE bool setGeometryAdjustment(const QString &parameter, double value);
    Q_INVOKABLE void resetGeometryCorrections();
    Q_INVOKABLE void setCropAspect(double aspect);
    Q_INVOKABLE void resetGeometry();
    bool cropEditing() const { return m_cropEditing; }
    QRectF cropOverlay() const { const auto g = currentState().geometry; return g.orientedRect(g.crop); }
    Q_INVOKABLE bool beginCrop();
    Q_INVOKABLE void cancelCrop();
    Q_INVOKABLE bool applyCrop(double x, double y, double width, double height);
    bool canUndo() const;
    bool canRedo() const;
    QVariantList editHistory() const;
    QVariantList selectedIndices() const;
    QStringList albumNames() const;
    QStringList currentKeywords() const;
    QStringList currentAlbums() const;
    QString currentColorLabel() const;
    Q_INVOKABLE bool executeEditCommand(const QVariantMap &command);
    Q_INVOKABLE bool executeEditCommands(const QVariantList &commands);
    Q_INVOKABLE bool createVirtualCopy(const QString &name = {});
    Q_INVOKABLE bool renameCurrentVirtualCopy(const QString &name);
    Q_INVOKABLE bool removeCurrentVirtualCopy();
    Q_INVOKABLE bool setPhotoSelection(const QVariantList &indices);
    Q_INVOKABLE bool setSelectionRating(int rating);
    Q_INVOKABLE bool setSelectionFlag(const QString &flag);
    Q_INVOKABLE bool setSelectionKeywords(const QString &text);
    Q_INVOKABLE bool setSelectionLabel(const QString &label);
    Q_INVOKABLE bool addSelectionToAlbum(const QString &name);
    Q_INVOKABLE bool removeSelectionFromAlbum(const QString &name);
    QStringList presetNames() const;
    Q_INVOKABLE bool saveNamedPreset(const QString &name);
    Q_INVOKABLE bool applyNamedPreset(const QString &name);
    Q_INVOKABLE bool removeNamedPreset(const QString &name);
    Q_INVOKABLE bool renameNamedPreset(const QString &name, const QString &replacement);
    Q_INVOKABLE bool replaceNamedPreset(const QString &name);
    Q_INVOKABLE bool exportNamedPreset(const QString &name, const QUrl &destination);
    Q_INVOKABLE bool importNamedPreset(const QUrl &source, const QString &replacementName = {});
    Q_INVOKABLE void openPresetExportDialog(const QString &name);
    Q_INVOKABLE void openPresetImportDialog(const QString &replacementName = {});
    Q_INVOKABLE bool exportXmp(const QUrl &destination);
    Q_INVOKABLE bool importXmp(const QUrl &source);
    Q_INVOKABLE void openXmpExportDialog();
    Q_INVOKABLE void openXmpImportDialog();
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE void resetAdjustments();
    Q_INVOKABLE void resetVignette();
    Q_INVOKABLE void copyAdjustments();
    Q_INVOKABLE void pasteAdjustments();
    Q_INVOKABLE void syncAdjustmentsToAll();
    bool hasAdjustmentClipboard() const { return m_hasClipboard; }
    QString adjustmentClipboardName() const { return m_clipboardName; }
    Q_INVOKABLE bool pasteAdjustmentGroups(const QStringList &groups);
    // Returns changed version count, or -1 for invalid input. Source is excluded.
    Q_INVOKABLE int syncAdjustmentGroups(const QStringList &groups, bool selectedOnly);
    Q_INVOKABLE void setColorMix(int band, int component, double value);
    Q_INVOKABLE void setCurvePoint(int channel, int point, double value);
    Q_INVOKABLE void resetCurve(int channel);
    Q_INVOKABLE bool exportCurrent(const QUrl &destination, const QString &colorSpaceKey = QStringLiteral("srgb"), int quality = 92);
    Q_INVOKABLE QString reportBug();
    Q_INVOKABLE qulonglong requestBugReport();
    Q_INVOKABLE void reportBugWithDialog();

signals:
    void brushDrawingChanged();
    void brushCapacityWarningChanged();
    void brushPreviewChanged();
    void relinkChanged();
    void diagnosticChanged();
    void diagnosticFinished(const QString &path);
    void adjustmentClipboardChanged();
    void copyImportChanged();
    void importNamePreviewChanged();
    void copyImportRequested(const QVariantList &urls);
    void scopePlotChanged();
    void cropEditingChanged();
    void lookChanged(); void referenceChanged(); void calibrationChanged();
    void libraryChanged(); void currentIndexChanged(); void previewUrlChanged(); void scopesChanged();
    void curationChanged();
    void presetsChanged();
    void historyChanged();
    void adjustmentsChanged(); void projectChanged(); void statusMessageChanged(); void languageChanged();
    void currentMetadataChanged();
    void activityChanged(); void backendChanged(); void exportChanged();
    void previewGeometryChanged(); void gpuFrameChanged();
    void exportFinished(int succeeded, int failed, bool cancelled);

private:
    struct BugSnapshot {
        quint64 request = 0, revision = 0, photoEpoch = 0;
        bool requestGpu = false;
        PrepareRequest prepare;
        ProcessingPlan plan, visiblePlan;
        QImage gpuSource;
        QRectF gpuFrameRect;
        AdjustmentState state;
        QString file, project, pipeline;
        double shadowClip = 0, highlightClip = 0;
        QJsonObject scopeContext, performanceValues;
    };
    BugSnapshot freezeBugSnapshot() const;
    QString createBugReport(const BugSnapshot &snapshot, QJsonObject gpuOutput);
    quint64 startBugReport(bool showDialog);
    void finishBugReport(QJsonObject gpuOutput);
    void showBugReport(const QString &path);
    std::unique_ptr<BugSnapshot> m_pendingBug;
    quint64 m_nextDiagnosticRequest = 0;
    bool m_diagnosticBusy = false, m_diagnosticDialog = false;
    QTimer m_diagnosticTimer;
    struct PhotoEntry {
        QString path; QString name; AdjustmentState state; bool raw = false;
        int rating = 0; QString flag = QStringLiteral("none");
        EditHistory history; CatalogTags tags;
        QString copyKey, versionName, originalKey;
        PhotoTimeline timeline;
        QString sourceKey;
        bool available=true;
        quint64 thumbnailRevision=0;
        ProjectDatabase::OriginalIdentity identity;
        QString storageKey() const { return copyKey.isEmpty() ? (originalKey.isEmpty() ? path : originalKey) : copyKey; }
    };
    QVector<PhotoEntry> resolveExportVersions(const QStringList &keys, QString *error) const;
    QVector<PhotoEntry> m_photos;
    struct RelinkRequest {
        quint64 epoch=0, id=0;
        QString sourceKey, sourcePath, candidatePath;
        QStringList versionKeys;
        ProjectDatabase::OriginalIdentity expected, candidateIdentity;
        bool confirming=false, acknowledgeUnknown=false;
    };
    struct RelinkResult { ProjectDatabase::OriginalIdentity identity; SourceData source; QString error, verifiedKey; };
    struct IdentityRequest { quint64 epoch=0; QStringList paths; };
    std::unique_ptr<LatestJob<RelinkRequest,RelinkResult>> m_relinkJob;
    std::unique_ptr<LatestJob<IdentityRequest,QHash<QString,ProjectDatabase::OriginalIdentity>>> m_identityJob;
    std::unique_ptr<RelinkRequest> m_relinkRequest;
    QVariantMap m_relinkPreview;
    bool m_relinkBusy=false;
    quint64 m_nextRelinkId=0, m_thumbnailRevision=0;
    QSet<QString> m_identityPending;
    QSet<QString> m_identityEstablished;
    void initializeRelinkJobs();
    void queueSourceIdentity(const QString &path);
    void startSourceIdentities();
    QString sourceFamily(const PhotoEntry &photo) const;
    QStringList familyVersionKeys(const QString &sourcePath) const;
    bool sourceReadable(const PhotoEntry &photo) const;
    QSet<QString> m_importedPaths;
    QSet<int> m_selectedPhotos;
    int m_currentIndex = -1;
    quint64 m_previewRevision = 0;
    QImage m_fullSource, m_previewSource, m_processedPreview;
    ProcessedImageProvider *m_provider = nullptr;
    ScopesResult m_scopes;
    ProjectDatabase m_project;
    AdjustmentState m_clipboard;
    QString m_clipboardName;
    bool m_hasClipboard = false;
    QString m_language = QStringLiteral("zh_CN");
    QString m_statusMessage;
    QVariantMap m_currentMetadata;
    std::shared_ptr<SourceCache> m_sourceCache;
    std::shared_ptr<BrushRenderBudget> m_brushBudget;
    std::shared_ptr<RenderedPreviewCache> m_renderCache;
    std::shared_ptr<FullScopesCache> m_fullScopesCache;
    std::shared_ptr<AnalysisFrameCache> m_analysisFrameCache;
    std::shared_ptr<ScopePlotCache> m_scopePlotCache;
    std::unique_ptr<ExportQueue> m_exportQueue;
    std::unique_ptr<CopyImportQueue> m_copyImportQueue;
    struct ImportNameDateRequest { QHash<QString,QString> sources; };
    QCache<QString,QString> m_importNameTimes{1000};
    QHash<QString,QString> m_importNamePending;
    quint64 m_importNameRevision=0;
    std::unique_ptr<LatestJob<ImportNameDateRequest,QHash<QString,QString>>> m_importNameDatesJob;
    double m_copyImportProgress=0;
    QString m_copyImportStatus;
    QImage m_fastSource, m_gpuSource, m_loadedPreview;
    QString m_loadedKey, m_backendName, m_scopesLabel;
    QString m_displayColorLutKey = QStringLiteral("identity-srgb");
    bool m_loading = false, m_rendering = false, m_gpuEnabled = true, m_gpuActive = false;
    bool m_exactScopes = false, m_scopesUpdating = true, m_viewportOnly = false;
    QJsonObject m_preparedGeometry;
    bool m_preparing = false;
    bool m_cropEditing = false;
    bool m_interacting = false, m_sourceIsFull = false, m_closing = false;
    quint64 m_photoEpoch = 0, m_prepareGeneration = 0, m_requestedRevision = 0, m_scopesRevision = 0;
    int m_scopesRank = -1;
    QSize m_viewport{1600, 1000};
    QSizeF m_displayPixels;
    QRectF m_previewFrameRect{0,0,1,1};
    double m_devicePixelRatio = 1, m_zoom = 0, m_centerX = .5, m_centerY = .5, m_exportProgress = 0;
    QTimer m_saveTimer, m_saveMaxTimer, m_refineTimer, m_exactTimer, m_prefetchTimer, m_catalogDateTimer;
    QHash<QString, AdjustmentState> m_dirtyEdits;
    QHash<QString, EditHistory> m_dirtyHistories;
    QHash<QString, ProjectDatabase::PhotoCuration> m_dirtyCuration;
    QHash<QString, CatalogTags> m_dirtyTags;
    QHash<QString, PhotoTimeline> m_dirtyDates;
    struct CatalogDateRequest { quint64 epoch; QStringList paths; };
    quint64 m_catalogEpoch = 0;
    std::unique_ptr<LatestJob<CatalogDateRequest,QHash<QString,QString>>> m_catalogDatesJob;
    void updateCaptureTime(const QString &path, const QString &time);
    void markPhotoDirty(const PhotoEntry &photo);
    bool updateSelectedTags(const QString &operation, const QString &value);
    QElapsedTimer m_renderClock;
    std::unique_ptr<LatestJob<LoadRequest, SourceData>> m_loader, m_prefetch;
    std::unique_ptr<LatestJob<PrepareRequest, PreparedPreview>> m_prepare;
    std::unique_ptr<LatestJob<RenderRequest, RenderedFrame>> m_render;
    std::unique_ptr<LatestJob<ScopeRequest, ScopesResult>> m_scopeJob, m_fullScopeJob;
    std::unique_ptr<LatestJob<ScopePlotRequest, ScopePlotResult>> m_plotJob;
    ScopePlotResult m_scopePlot;
    QString m_scopeMode = "histogram";
    QTimer m_plotTimer;
    quint64 m_plotRevision = 0, m_plotImageId = 0, m_plotSubmittedRevision = 0;
    bool m_plotFull = false;
    QString m_scopesError;
    quint64 m_scopesErrorRevision=0;
    int m_scopesErrorRank=-1;
    struct DeferredBudgetJob {quint64 revision=0;int attempts=0;bool pending=false;};
    std::array<DeferredBudgetJob,3> m_deferredBudgetJobs{};
    QTimer m_budgetRetryTimer;
    void deferBudgetJob(int job,quint64 revision);
    void retryBudgetJobs(bool afterExport=false);
    friend class BrushScopeRoutingTests;
    friend class BrushLivePreviewTests;
    struct BrushGesture {
        QString photoKey,layer,newLayer;
        quint64 catalog=0,epoch=0,generation=0,revision=0,gesture=0,sample=0;
        qint64 sourceKey=0;
        QSize frame;
        QJsonObject geometry;
        double exposure=1;
        std::size_t capacityWarningAt=1;bool capacityWarned=false,capacityHadPositivePressure=false;
        BrushMaskGeometry::StrokeMode mode=BrushMaskGeometry::StrokeMode::Paint;
        AdjustmentState initial;
        std::unique_ptr<BrushMaskGeometry::StrokeBuilder> builder;
    };
    std::unique_ptr<BrushGesture> m_brushGesture;
    bool brushGestureCurrent() const;
    bool brushGestureCurrent(const BrushGesture &gesture) const;
    struct BrushPreviewRequest {QImage source;ProcessingPlan plan;quint64 gesture=0,sample=0;};
    std::unique_ptr<LatestFrameJob<BrushPreviewRequest,RenderedFrame>> m_brushPreviewJob;
    quint64 m_brushGestureSerial=0,m_brushPreviewSerial=0,m_brushPreviewSample=0;
    QString m_brushPreviewId;
    QTimer m_brushPreviewRetryTimer;
    int m_brushPreviewRetries=0;
    void initializeBrushPreview();
    void requestBrushPreview();
    void updateBrushCapacityWarning();
    void clearBrushPreview();
    bool brushCandidate(const BrushGesture &gesture,BrushMaskGeometry::Stroke stroke,AdjustmentState *candidate) const;
    void requestScopePlot();

    bool m_showReference=false, m_referenceBusy=false, m_calibrationBusy=false;
    QImage m_referenceImage;
    QVariantMap m_referenceInfo, m_calibrationReport;
    ScopesResult m_referenceScopes;
    QString m_manualReferencePath;
    QSet<QString> m_referenceFiles;
    quint64 m_referenceRevision=0;
    std::unique_ptr<LatestJob<CameraReferenceRequest,CameraReferenceResult>> m_referenceJob;
    std::unique_ptr<LatestJob<LookCalibrationRequest,LookCalibrationResult>> m_calibrationJob;
    void initializeLookJobs();
    bool applyLookEdit(const QJsonObject &command, const QString &action, const QVariantMap &details = {});
    void resetReference();
    void requestReference();
    bool isProtectedPhoto(const QString &path) const;
    void initializeJobs();
    void acceptSource(quint64 photo, SourceData data);
    void prepareCurrent();
    void scheduleRender(bool fast);
    void markDirty();
    void enqueueEdits();
    void requestFullScopes();
    void acceptScopes(quint64 revision, const ScopesResult &scopes, int rank, const QString &label);
    void completeFrame(quint64 revision);
    void setBusy(bool busy);
    void prefetchNeighbor();

    AdjustmentState currentState() const;
    GeometryState previewGeometry() const;
    AdjustmentState *mutableCurrentState();
    bool importPath(const QString &path, bool notifyImmediately);
    void finishImportBatch(int added, int rawAdded);
    void applyCurrent();
    void loadCurrent();
    // A dialog previews the exact parsed XMP snapshot which is later applied.
    // Never re-read an external file after the user has confirmed its impact.
    bool applyParsedXmp(const XmpSidecar::Document &document);
    void persistAndApply(const QString &action, const QVariantMap &details = {});
    void setStatus(const QString &message);
    QString uiText(const QString &zh, const QString &en) const;
    void setAdjustment(const char *name, double v, double AdjustmentState::*member);

    static QVariantList toVariantList(const AdjustmentState::ColorBandArray &values);
    static QVariantList toVariantList(const AdjustmentState::CurveArray &values);
    static QVariantList toVariantList(const AdjustmentState::GradingArray &values);
};
