#include "app/RecipeClipboardReader.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>
#include <QThread>
#include "core/commands/EditRecipe.h"

QVariantMap RecipeClipboardReader::copy(const QString &text) const {
    if(text.size()>EditRecipe::MaximumTextLength)return {{"status","too-long"}};
    if(!qGuiApp || QThread::currentThread()!=qGuiApp->thread())return {{"status","unavailable"}};
    auto *clipboard=QGuiApplication::clipboard();
    if(!clipboard)return {{"status","unavailable"}};
    clipboard->setText(text,QClipboard::Clipboard);
    return {{"status","ok"}};
}

QVariantMap RecipeClipboardReader::snapshot() const {
    if (!qGuiApp || QThread::currentThread()!=qGuiApp->thread()) return {{"status","unavailable"}};
    auto *clipboard=QGuiApplication::clipboard();
    const QMimeData *mime=clipboard?clipboard->mimeData(QClipboard::Clipboard):nullptr;
    if (!mime || !mime->hasText()) return {{"status","unavailable"}};
    const QString text=mime->text();
    if (text.size()>EditRecipe::MaximumTextLength) return {{"status","too-long"}};
    return {{"status","ok"},{"text",text}};
}
