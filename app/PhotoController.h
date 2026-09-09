#pragma once

#include <QObject>
#include <QImage>
#include <QVariantList>
#include <QVariantMap>
#include <QVector>
#include <QUrl>
#include <QTimer>
#include <QElapsedTimer>
#include <QSet>
#include <algorithm>
#include <cmath>
#include "core/preview/PreviewTasks.h"
#include "core/export/ExportQueue.h"
#include "core/look/CameraReference.h"
#include "core/look/LookCalibration.h"

#include "core/pipeline/AdjustmentState.h"
#include "core/project/ProjectDatabase.h"
#include "core/scopes/ScopesEngine.h"

class ProcessedImageProvider;

class PhotoController final : public QObject {
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
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY previewUrlChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY currentIndexChanged)
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
    Q_PROPERTY(bool exportBusy READ exportBusy NOTIFY exportChanged)
    Q_PROPERTY(double exportProgress READ exportProgress NOTIFY exportChanged)
    Q_PROPERTY(QSizeF previewDisplaySize READ previewDisplaySize NOTIFY previewGeometryChanged)
    Q_PROPERTY(qulonglong renderRevision READ renderRevision NOTIFY gpuFrameChanged)
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

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
    explicit PhotoController(ProcessedImageProvider *provider, QObject *parent = nullptr);
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
    bool exportBusy() const { return m_exportQueue && m_exportQueue->busy(); }
    double exportProgress() const { return m_exportProgress; }
    QSizeF previewDisplaySize() const { return m_displayPixels / m_devicePixelRatio; }
    qulonglong renderRevision() const { return m_requestedRevision; }
    QImage gpuSource() const { return m_preparing ? QImage{} : m_gpuSource; }
    float rawBaseExposureStops() const {
        if (!currentIsRaw()) return 0.0f;
        bool ok = false;
        const double value = m_currentMetadata.value(QStringLiteral("rawBaseExposureStops"), 0.0).toDouble(&ok);
        return ok && std::isfinite(value) ? float(std::clamp(value, -8.0, 8.0)) : 0.0f;
    }
    ProcessingPlan gpuPlan() const {
        return ProcessingPlan::compile(currentState(), ImagePipeline::InputEncoding::LinearProPhoto,
                                       ColorManagement::OutputSpace::SRgb, currentIsRaw(), rawBaseExposureStops());
    }
    void gpuPresented(quint64 revision, const QString &backend);
    void gpuScopes(quint64 revision, const QByteArray &counts, quint64 pixels);
    void gpuFailed(const QString &message);
    Q_INVOKABLE void setGpuEnabled(bool enabled);
    Q_INVOKABLE void setExactScopes(bool enabled);
    Q_INVOKABLE void setViewport(double width, double height, double dpr, double zoom, double centerX, double centerY);
    Q_INVOKABLE void finishInteraction();
    Q_INVOKABLE bool exportAll(const QUrl &folder, const QString &colorSpaceKey = QStringLiteral("srgb"), int quality = 92);
    Q_INVOKABLE void cancelExport();
    Q_INVOKABLE bool flushEdits();
    int currentIndex() const { return m_currentIndex; }
    QString previewUrl() const;
    bool hasImage() const { return m_currentIndex >= 0 && m_currentIndex < m_photos.size(); }
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
    QVariantList hslHue() const; QVariantList hslSaturation() const; QVariantList hslLuminance() const;
    QVariantList masterCurve() const; QVariantList redCurve() const; QVariantList greenCurve() const; QVariantList blueCurve() const;
    QString statusMessage() const { return m_statusMessage; }

    void setExposure(double v); void setTemperature(double v); void setTint(double v); void setContrast(double v);
    void setHighlights(double v); void setShadows(double v); void setWhites(double v); void setBlacks(double v);
    void setHighlightRecovery(double v); void setHue(double v); void setSaturation(double v); void setVibrance(double v);
    Q_INVOKABLE void setLanguage(const QString &language);

    Q_INVOKABLE void openImportDialog();
    Q_INVOKABLE bool importFile(const QUrl &url);
    Q_INVOKABLE void importFiles(const QVariantList &urls);
    Q_INVOKABLE void selectPhoto(int index);
    Q_INVOKABLE bool createProject(const QUrl &folder, const QString &name);
    Q_INVOKABLE void resetAdjustments();
    Q_INVOKABLE void copyAdjustments();
    Q_INVOKABLE void pasteAdjustments();
    Q_INVOKABLE void syncAdjustmentsToAll();
    Q_INVOKABLE void setColorMix(int band, int component, double value);
    Q_INVOKABLE void setCurvePoint(int channel, int point, double value);
    Q_INVOKABLE void resetCurve(int channel);
    Q_INVOKABLE bool exportCurrent(const QUrl &destination, const QString &colorSpaceKey = QStringLiteral("srgb"), int quality = 92);
    Q_INVOKABLE QString reportBug();
    Q_INVOKABLE void reportBugWithDialog();

signals:
    void lookChanged(); void referenceChanged(); void calibrationChanged();
    void libraryChanged(); void currentIndexChanged(); void previewUrlChanged(); void scopesChanged();
    void adjustmentsChanged(); void projectChanged(); void statusMessageChanged(); void languageChanged();
    void currentMetadataChanged();
    void activityChanged(); void backendChanged(); void exportChanged();
    void previewGeometryChanged(); void gpuFrameChanged();
    void exportFinished(int succeeded, int failed, bool cancelled);

private:
    struct PhotoEntry { QString path; QString name; AdjustmentState state; bool raw = false; };
    QVector<PhotoEntry> m_photos;
    QSet<QString> m_importedPaths;
    int m_currentIndex = -1;
    quint64 m_previewRevision = 0;
    QImage m_fullSource, m_previewSource, m_processedPreview;
    ProcessedImageProvider *m_provider = nullptr;
    ScopesResult m_scopes;
    ProjectDatabase m_project;
    AdjustmentState m_clipboard;
    bool m_hasClipboard = false;
    QString m_language = QStringLiteral("zh_CN");
    QString m_statusMessage;
    QVariantMap m_currentMetadata;
    std::shared_ptr<SourceCache> m_sourceCache;
    std::unique_ptr<ExportQueue> m_exportQueue;
    QImage m_fastSource, m_gpuSource, m_loadedPreview;
    QString m_loadedKey, m_backendName, m_scopesLabel;
    bool m_loading = false, m_rendering = false, m_gpuEnabled = true, m_gpuActive = false;
    bool m_exactScopes = false, m_scopesUpdating = true, m_viewportOnly = false;
    bool m_preparing = false;
    bool m_interacting = false, m_sourceIsFull = false, m_closing = false;
    quint64 m_photoEpoch = 0, m_prepareGeneration = 0, m_requestedRevision = 0, m_scopesRevision = 0;
    int m_scopesRank = -1;
    QSize m_viewport{1600, 1000};
    QSizeF m_displayPixels;
    double m_devicePixelRatio = 1, m_zoom = 0, m_centerX = .5, m_centerY = .5, m_exportProgress = 0;
    QTimer m_saveTimer, m_saveMaxTimer, m_refineTimer, m_exactTimer, m_prefetchTimer;
    QHash<QString, AdjustmentState> m_dirtyEdits;
    QElapsedTimer m_renderClock;
    std::unique_ptr<LatestJob<LoadRequest, SourceData>> m_loader, m_prefetch;
    std::unique_ptr<LatestJob<PrepareRequest, PreparedPreview>> m_prepare;
    std::unique_ptr<LatestJob<RenderRequest, QImage>> m_render;
    std::unique_ptr<LatestJob<ScopeRequest, ScopesResult>> m_scopeJob, m_fullScopeJob;

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
    AdjustmentState *mutableCurrentState();
    bool importPath(const QString &path, bool notifyImmediately);
    void finishImportBatch(int added, int rawAdded);
    void applyCurrent();
    void loadCurrent();
    void persistAndApply(const QString &action, const QVariantMap &details = {});
    void setStatus(const QString &message);
    QString uiText(const QString &zh, const QString &en) const;
    void setAdjustment(const char *name, double v, double AdjustmentState::*member);

    static QVariantList toVariantList(const AdjustmentState::ColorBandArray &values);
    static QVariantList toVariantList(const AdjustmentState::CurveArray &values);
};
