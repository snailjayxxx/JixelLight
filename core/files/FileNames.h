#pragma once
#include <QStringList>
#include <QFileInfo>
#include <QDateTime>

namespace FileNames {
// Cheap metadata-cache identity: filesystem facts only, no content reads.
// Actual date-named copies must reread and verify their capture-time snapshot.
inline QString sourceStamp(const QString &path) {
    const QFileInfo info(path);
    if (!info.isFile() || !info.isReadable()) return {};
    const auto canonical=info.canonicalFilePath();
    if (canonical.isEmpty()) return {};
    return canonical+QChar(0)+QString::number(info.size())+QChar(0)
        +QString::number(info.lastModified().toMSecsSinceEpoch());
}
inline QString folded(const QString &name) {
    return name.normalized(QString::NormalizationForm_C).toCaseFolded();
}
inline bool portable(const QString &name) {
    if (name.isEmpty() || name=="." || name==".." || name.endsWith('.') || name.endsWith(' ')
        || name.size()>240 || name.toUtf8().size()>240) return false;
    for (const auto c : name)
        if (c.unicode()<32 || c.unicode()==127 || QStringLiteral("<>:\"/\\|?*").contains(c)) return false;
    const auto device=name.section('.',0,0).trimmed().toUpper();
    if (QStringList{"CON","PRN","AUX","NUL","CONIN$","CONOUT$"}.contains(device)) return false;
    if (device.size()==4 && (device.startsWith("COM") || device.startsWith("LPT"))) {
        const auto digit=device[3];
        if ((digit>='1' && digit<='9') || QString::fromUtf8("¹²³").contains(digit)) return false;
    }
    return true;
}
}
