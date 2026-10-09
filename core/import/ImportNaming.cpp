#include "ImportNaming.h"
#include <QFileInfo>
#include <QSet>
#include <QVector>

namespace {
bool portableName(const QString &name) {
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
struct Part { QString literal; int width=-1; bool name=false; };
}

ImportNames planImportNames(const QStringList &sources,const ImportNaming &naming) {
    ImportNames result;
    auto fail=[&](const QString &error) { result.names.clear(); result.error=error; return result; };
    if (sources.isEmpty() || sources.size()>1000 || naming.sequenceStart<1
        || qint64(naming.sequenceStart)+sources.size()-1>999999999)
        return fail("Choose 1–1000 files and a sequence range within 1–999999999");
    if (naming.pattern.size()>160) return fail("Filename template is longer than 160 characters");
    QVector<Part> parts;
    for (qsizetype pos=0;pos<naming.pattern.size();) {
        const auto c=naming.pattern[pos];
        if (c=='}') return fail("Unmatched closing brace in filename template");
        if (c!='{') {
            const auto begin=pos;
            while (pos<naming.pattern.size() && naming.pattern[pos]!='{' && naming.pattern[pos]!='}') ++pos;
            parts.push_back({naming.pattern.mid(begin,pos-begin)}); continue;
        }
        const auto end=naming.pattern.indexOf('}',pos+1);
        if (end<0) return fail("Unclosed token in filename template");
        const auto token=naming.pattern.mid(pos+1,end-pos-1);
        if (token=="name") parts.push_back({{},-1,true});
        else if (token=="seq") parts.push_back({{},0,false});
        else if (token.size()==5 && token.startsWith("seq:") && token[4]>='1' && token[4]<='9')
            parts.push_back({{},token[4].digitValue(),false});
        else return fail("Unknown filename token: {"+token+"}");
        pos=end+1;
    }
    QSet<QString> seen;
    for (int i=0;i<sources.size();++i) {
        const QFileInfo source(sources[i]); QString name=source.fileName();
        if (!naming.pattern.isEmpty()) {
            QString stem;
            for (const auto &part : parts) {
                if (part.name) stem+=source.completeBaseName();
                else if (part.width>=0) stem+=QString::number(naming.sequenceStart+i).rightJustified(part.width,'0');
                else stem+=part.literal;
            }
            // Reject unsafe stems even if an appended extension could hide a
            // trailing dot/space or a directory traversal marker.
            if (!portableName(stem)) return fail("Unsafe or too long filename stem: "+stem);
            name=(stem+(source.suffix().isEmpty() ? QString() : "."+source.suffix())).normalized(QString::NormalizationForm_C);
        }
        if (!portableName(name)) return fail("Unsafe or too long destination filename: "+name);
        const auto folded=name.normalized(QString::NormalizationForm_C).toCaseFolded();
        if (seen.contains(folded)) return fail("Duplicate destination filename: "+name);
        seen.insert(folded); result.names.push_back(name);
    }
    return result;
}
