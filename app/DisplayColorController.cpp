#include "app/PhotoController.h"
#include "core/image/ProcessedImageProvider.h"
#include "diagnostics/PerformanceRecorder.h"

void PhotoController::setDisplayColorLut(const QImage &atlas,const QString &key) {
    const QString effective=key.isEmpty()?QStringLiteral("identity-srgb"):key;
    if(m_displayColorLutKey==effective)return;
    m_displayColorLutKey=effective;
    if(m_provider)m_provider->setDisplayColorLut(atlas,effective);
    if(!m_brushPreviewId.isEmpty()&&m_provider) {
        const auto id=QStringLiteral("brush-live/%1").arg(++m_brushPreviewSerial);
        if(m_provider->renameBrushPreview(id)){m_brushPreviewId=id;emit brushPreviewChanged();}
    }

    // Only provider URLs are invalidated. Processing pixels, revisions used by
    // scopes, export state and RAW caches remain untouched by monitor changes.
    if(!m_processedPreview.isNull()) {
        ++m_previewRevision;
        emit previewUrlChanged();
    }
    if(!m_referenceImage.isNull()) {
        ++m_referenceRevision;
        emit referenceChanged();
    }
    PerformanceRecorder::value("display_provider_lut_key",effective);
}
