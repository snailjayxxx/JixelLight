#include "app/RecipeClipboardReader.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QThread>

QVariantMap RecipeClipboardReader::snapshot() const {
    if (!qGuiApp || QThread::currentThread()!=qGuiApp->thread()) return {{"status","unavailable"}};
    auto *clipboard=QGuiApplication::clipboard();
    const QMimeData *mime=clipboard?clipboard->mimeData(QClipboard::Clipboard):nullptr;
    if (!mime || !mime->hasText()) return {{"status","unavailable"}};
    const QString text=mime->text();
    if (text.size()>1048576) return {{"status","too-long"}};
    return {{"status","ok"},{"text",text}};
}
