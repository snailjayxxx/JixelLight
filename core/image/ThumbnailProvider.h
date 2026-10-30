#pragma once

#include <QQuickAsyncImageProvider>
#include <memory>

struct ThumbnailStore;

// Image-provider requests are performed off the GUI thread. Photo lists and
// filmstrips never decode a full RAW just to show a tile.
class ThumbnailProvider final : public QQuickAsyncImageProvider {
public:
    ThumbnailProvider();
    QQuickImageResponse *requestImageResponse(const QString &id, const QSize &requestedSize) override;

private:
    std::shared_ptr<ThumbnailStore> m_store;
};
