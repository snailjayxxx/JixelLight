#pragma once
#include <QStringList>

namespace FileNames {
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
