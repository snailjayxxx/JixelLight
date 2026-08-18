#pragma once

#include <QObject>
#include <QImage>
#include <QVariantList>
#include <QVector>
#include <QUrl>

#include "core/pipeline/AdjustmentState.h"
#include "core/project/ProjectDatabase.h"
#include "core/scopes/ScopesEngine.h"

class ProcessedImageProvider;

class PhotoController final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QVariantList library READ library NOTIFY libraryChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentIndexChanged)
    Q_PROPERTY(QString previewUrl READ previewUrl NOTIFY previewUrlChanged)
    Q_PROPERTY(bool hasImage READ hasImage NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentFile READ currentFile NOTIFY currentIndexChanged)
    Q_PROPERTY(QString currentFormat READ currentFormat NOTIFY currentIndexChanged)
    Q_PROPERTY(bool currentIsRaw READ currentIsRaw NOTIFY currentIndexChanged)
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
    Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY statusMessageChanged)

public:
    explicit PhotoController(ProcessedImageProvider *provider, QObject *parent = nullptr);
    QVariantList library() const;
    int currentIndex() const { return m_currentIndex; }
    QString previewUrl() const;
    bool hasImage() const { return m_currentIndex >= 0 && m_currentIndex < m_photos.size(); }
    QString currentFile() const;
    QString currentFormat() const;
    bool currentIsRaw() const;
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
    QString statusMessage() const { return m_statusMessage; }

    void setExposure(double v); void setTemperature(double v); void setTint(double v); void setContrast(double v);
    void setHighlights(double v); void setShadows(double v); void setWhites(double v); void setBlacks(double v);
    Q_INVOKABLE void setLanguage(const QString &language);

    Q_INVOKABLE void importFiles(const QVariantList &urls);
    Q_INVOKABLE void selectPhoto(int index);
    Q_INVOKABLE bool createProject(const QUrl &folder, const QString &name);
    Q_INVOKABLE void resetAdjustments();
    Q_INVOKABLE void copyAdjustments();
    Q_INVOKABLE void pasteAdjustments();
    Q_INVOKABLE void syncAdjustmentsToAll();
    Q_INVOKABLE bool exportCurrent(const QUrl &destination);
    Q_INVOKABLE QString reportBug();

signals:
    void libraryChanged(); void currentIndexChanged(); void previewUrlChanged(); void scopesChanged();
    void adjustmentsChanged(); void projectChanged(); void statusMessageChanged(); void languageChanged();

private:
    struct PhotoEntry { QString path; QString name; AdjustmentState state; bool raw = false; };
    QVector<PhotoEntry> m_photos;
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

    AdjustmentState currentState() const;
    AdjustmentState *mutableCurrentState();
    void applyCurrent();
    void loadCurrent();
    void setStatus(const QString &message);
    QString uiText(const QString &zh, const QString &en) const;
    void setAdjustment(const char *name, double v, double AdjustmentState::*member);
};
