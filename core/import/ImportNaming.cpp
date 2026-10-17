#include "ImportNaming.h"
#include "core/files/FileNames.h"
#include "core/library/PhotoTimeline.h"
#include <QFileInfo>
#include <QSet>
#include <QVector>

namespace {
enum class Kind { Literal, Name, Sequence, CaptureDate, CaptureTime };
struct Part { QString literal; Kind kind=Kind::Literal; int width=0; };
}

bool importTemplateNeedsCaptureTime(const QString &pattern) {
    return pattern.contains("{capture_date}") || pattern.contains("{capture_time}");
}

ImportNames planImportNames(const QStringList &sources,const ImportNaming &naming,const QStringList &captureTimes) {
    ImportNames result;
    auto fail=[&](const QString &error,const QString &zh) { result.names.clear(); result.error=error; result.errorZh=zh; return result; };
    if (sources.isEmpty() || sources.size()>1000)
        return fail("Choose 1–1000 files", "请选择 1–1000 个文件");
    if (!naming.pattern.isEmpty() && (naming.sequenceStart<1
        || qint64(naming.sequenceStart)+sources.size()-1>999999999))
        return fail("Choose a sequence range within 1–999999999", "序号范围须为 1–999999999");
    if (naming.pattern.size()>160) return fail("Filename template is longer than 160 characters", "文件名模板不能超过 160 个字符");
    QVector<Part> parts;
    for (qsizetype pos=0;pos<naming.pattern.size();) {
        const auto c=naming.pattern[pos];
        if (c=='}') return fail("Unmatched closing brace in filename template", "文件名模板中有未匹配的右花括号");
        if (c!='{') {
            const auto begin=pos;
            while (pos<naming.pattern.size() && naming.pattern[pos]!='{' && naming.pattern[pos]!='}') {
                const auto literal=naming.pattern[pos++];
                if (literal.unicode()<32 || literal.unicode()==127 || QStringLiteral("<>:\"/\\|?*").contains(literal))
                    return fail("Unsafe character in filename template", "文件名模板包含非法字符");
            }
            parts.push_back({naming.pattern.mid(begin,pos-begin)}); continue;
        }
        const auto end=naming.pattern.indexOf('}',pos+1);
        if (end<0) return fail("Unclosed token in filename template", "文件名模板中的占位符未闭合");
        const auto token=naming.pattern.mid(pos+1,end-pos-1);
        if (token=="name") parts.push_back({{},Kind::Name});
        else if (token=="capture_date") parts.push_back({{},Kind::CaptureDate});
        else if (token=="capture_time") parts.push_back({{},Kind::CaptureTime});
        else if (token=="seq") parts.push_back({{},Kind::Sequence});
        else if (token.size()==5 && token.startsWith("seq:") && token[4]>='1' && token[4]<='9')
            parts.push_back({{},Kind::Sequence,token[4].digitValue()});
        else return fail("Unknown filename token: {"+token+"}", "未知的文件名占位符：{"+token+"}");
        pos=end+1;
    }
    QSet<QString> seen;
    const bool dated=importTemplateNeedsCaptureTime(naming.pattern);
    if (dated && captureTimes.size()!=sources.size()) {
        result.needsCaptureTimes=true;
        return fail("Capture times are not available for this selection", "当前选择尚无可用拍摄时间");
    }
    for (int i=0;i<sources.size();++i) {
        const QFileInfo source(sources[i]); QString name=source.fileName();
        if (dated) {
            auto raw=captureTimes[i];
            if (raw.size()==19) { raw[4]=':'; raw[7]=':'; }
            if (raw.size()!=19 || PhotoTimeline::cameraTime(raw)!=captureTimes[i])
                return fail("Missing or invalid capture time: "+source.fileName(), "缺少有效拍摄时间："+source.fileName());
        }
        if (!naming.pattern.isEmpty()) {
            QString stem;
            for (const auto &part : parts) {
                switch (part.kind) {
                case Kind::Literal: stem+=part.literal; break;
                case Kind::Name: stem+=source.completeBaseName(); break;
                case Kind::Sequence: stem+=QString::number(naming.sequenceStart+i).rightJustified(part.width,'0'); break;
                case Kind::CaptureDate: stem+=captureTimes[i].left(10).remove('-'); break;
                case Kind::CaptureTime: stem+=captureTimes[i].mid(11).remove(':'); break;
                }
            }
            // Reject unsafe stems even if an appended extension could hide a
            // trailing dot/space or a directory traversal marker.
            if (!FileNames::portable(stem)) return fail("Unsafe or too long filename stem: "+stem, "文件名主体包含非法字符或过长："+stem);
            name=(stem+(source.suffix().isEmpty() ? QString() : "."+source.suffix())).normalized(QString::NormalizationForm_C);
        }
        if (!FileNames::portable(name)) return fail("Unsafe or too long destination filename: "+name, "目标文件名包含非法字符或过长："+name);
        const auto folded=FileNames::folded(name);
        if (seen.contains(folded)) return fail("Duplicate destination filename: "+name, "目标文件名重复："+name);
        seen.insert(folded); result.names.push_back(name);
    }
    return result;
}
