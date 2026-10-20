#include "ExportNaming.h"
#include "core/files/FileNames.h"
#include "core/library/PhotoTimeline.h"
#include <QFileInfo>
#include <QSet>

namespace {
enum class Kind { Literal, Name, Version, Sequence, CaptureDate, CaptureTime };
struct Part { QString literal; Kind kind=Kind::Literal; int width=0; };
}

bool exportTemplateNeedsCaptureTime(const QString &pattern) {
    return pattern.contains("{capture_date}") || pattern.contains("{capture_time}");
}

ExportNames planExportNames(const QVector<ExportNameSource> &sources, const ExportNaming &naming,
                            const QString &format, const QStringList &occupied) {
    ExportNames result;
    auto fail=[&](const QString &en,const QString &zh) {
        result.names.clear(); result.error=en; result.errorZh=zh; return result;
    };
    if (sources.isEmpty()) return fail("Choose photos to export", "请选择要导出的照片");
    if (!naming.pattern.isEmpty() && (naming.sequenceStart<1
        || qint64(naming.sequenceStart)+sources.size()-1>999999999))
        return fail("Choose photos and a sequence range within 1–999999999", "请选择照片，序号范围须为 1–999999999");
    const auto normalized=format.toLower();
    if (!QStringList{"jpeg","jpg","png","tif","tiff","webp"}.contains(normalized))
        return fail("Choose JPEG, PNG, TIFF or WebP", "请选择 JPEG、PNG、TIFF 或 WebP");
    const auto extension=normalized=="jpeg" ? QString("jpg") : normalized=="tiff" ? QString("tif") : normalized;
    if (naming.pattern.size()>160)
        return fail("Filename template is longer than 160 characters", "文件名模板不能超过 160 个字符");
    QVector<Part> parts;
    for (qsizetype pos=0;pos<naming.pattern.size();) {
        const auto c=naming.pattern[pos];
        if (c=='}') return fail("Unmatched closing brace in filename template", "文件名模板中有未匹配的右花括号");
        if (c!='{') {
            const auto begin=pos;
            while (pos<naming.pattern.size() && naming.pattern[pos]!='{' && naming.pattern[pos]!='}') ++pos;
            parts.push_back({naming.pattern.mid(begin,pos-begin)}); continue;
        }
        const auto end=naming.pattern.indexOf('}',pos+1);
        if (end<0) return fail("Unclosed token in filename template", "文件名模板中的占位符未闭合");
        const auto token=naming.pattern.mid(pos+1,end-pos-1);
        if (token=="name") parts.push_back({{},Kind::Name});
        else if (token=="version") parts.push_back({{},Kind::Version});
        else if (token=="capture_date") parts.push_back({{},Kind::CaptureDate});
        else if (token=="capture_time") parts.push_back({{},Kind::CaptureTime});
        else if (token=="seq") parts.push_back({{},Kind::Sequence});
        else if (token.size()==5 && token.startsWith("seq:") && token[4]>='1' && token[4]<='9')
            parts.push_back({{},Kind::Sequence,token[4].digitValue()});
        else return fail("Unknown filename token: {"+token+"}", "未知的文件名占位符：{"+token+"}");
        pos=end+1;
    }
    QSet<QString> seen;
    for (const auto &name : occupied) seen.insert(FileNames::folded(name));
    const bool dated=exportTemplateNeedsCaptureTime(naming.pattern);
    for (qsizetype i=0;i<sources.size();++i) {
        const auto &source=sources[i]; const QFileInfo file(source.path);
        if (dated) {
            auto raw=source.captureTime;
            if (raw.size()==19) { raw[4]=':'; raw[7]=':'; }
            if (raw.size()!=19 || PhotoTimeline::cameraTime(raw)!=source.captureTime)
                return fail("Missing or invalid capture time: "+file.fileName(), "缺少有效拍摄时间："+file.fileName());
        }
        QString stem;
        if (naming.pattern.isEmpty()) stem=file.completeBaseName()+"_JixelLight";
        else for (const auto &part : parts) {
            switch (part.kind) {
            case Kind::Literal: stem+=part.literal; break;
            case Kind::Name: stem+=file.completeBaseName(); break;
            case Kind::Version: stem+=source.versionName.isEmpty()?QString("Original"):source.versionName; break;
            case Kind::Sequence: stem+=QString::number(qint64(naming.sequenceStart)+i).rightJustified(part.width,'0'); break;
            case Kind::CaptureDate: stem+=source.captureTime.left(10).remove('-'); break;
            case Kind::CaptureTime: stem+=source.captureTime.mid(11).remove(':'); break;
            }
        }
        if (!FileNames::portable(stem))
            return fail("Unsafe or too long filename stem: "+stem, "文件名主体包含非法字符或过长："+stem);
        auto name=(stem+"."+extension).normalized(QString::NormalizationForm_C);
        if (naming.pattern.isEmpty()) {
            int suffix=1;
            while (seen.contains(FileNames::folded(name))) name=(stem+QString("_%1.").arg(suffix++)+extension).normalized(QString::NormalizationForm_C);
        }
        if (!FileNames::portable(name))
            return fail("Unsafe or too long destination filename: "+name, "目标文件名包含非法字符或过长："+name);
        if (seen.contains(FileNames::folded(name)))
            return fail("Destination filename already used: "+name+". Include {seq} to distinguish versions.",
                        "目标文件名已被使用："+name+"。可添加 {seq} 区分不同版本。");
        seen.insert(FileNames::folded(name)); result.names.push_back(name);
    }
    return result;
}
