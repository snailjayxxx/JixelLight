#include "XmpMergeReviewDialog.h"
#include <QComboBox>
#include <QDialogButtonBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QVBoxLayout>

namespace {
using XmpMerge::Field;
using XmpMerge::Decision;
QString uiText(bool en, const char *zh, const char *english) {
    return QString::fromUtf8(en ? english : zh);
}
QString label(Field f, bool en) {
    switch (f) {
    case Field::Develop: return uiText(en, "显影（整个快照）", "Develop (whole snapshot)");
    case Field::Rating: return uiText(en, "评分", "Rating");
    case Field::Flag: return uiText(en, "旗标", "Flag");
    case Field::Keywords: return uiText(en, "关键词", "Keywords");
    case Field::Label: return uiText(en, "颜色标签", "Color label");
    case Field::Albums: return uiText(en, "相册", "Albums");
    }
    return {};
}
QString token(const QString &text, bool en) {
    const QMap<QString, QPair<const char *, const char *>> names{
        {"none", {"无", "None"}}, {"pick", {"已选", "Pick"}}, {"reject", {"剔除", "Reject"}},
        {"red", {"红", "Red"}}, {"yellow", {"黄", "Yellow"}}, {"green", {"绿", "Green"}},
        {"blue", {"蓝", "Blue"}}, {"purple", {"紫", "Purple"}},
        {"off", {"关闭", "Off"}}, {"as-shot", {"拍摄时", "As shot"}},
        {"manual", {"手动", "Manual"}}, {"calibrated", {"已校准", "Calibrated"}}};
    const auto i = names.constFind(text);
    return i == names.cend() ? text : uiText(en, i->first, i->second);
}
QString valueText(Field f, const QJsonValue &value, bool en) {
    if (f == Field::Develop) {
        const auto object = value.toObject();
        return uiText(en, "曝光 %1 EV\n对比度 %2\nLook：%3", "Exposure %1 EV\nContrast %2\nLook: %3")
            .arg(QString::number(object.value("exposure").toDouble(), 'g', 5),
                 QString::number(object.value("contrast").toDouble(), 'g', 5),
                 token(object.value("look").toObject().value("mode").toString(), en));
    }
    if (f == Field::Rating) return QString::number(value.toInt()) + QStringLiteral(" / 5");
    if (f == Field::Keywords || f == Field::Albums) {
        QStringList values;
        for (const auto &v : value.toArray()) values.append(v.toString());
        return values.isEmpty() ? uiText(en, "（空）", "(empty)") : values.join(QStringLiteral(", "));
    }
    return token(value.toString(), en);
}
}

XmpMergeReviewDialog::XmpMergeReviewDialog(const XmpMerge::Plan &plan, bool english,
        const QStringList &warnings, QWidget *parent) : QDialog(parent), m_english(english) {
    setObjectName(QStringLiteral("xmp-merge-review"));
    setWindowTitle(uiText(english, "逐项检查 XMP 导入", "Review XMP import fields"));
    setMinimumSize(620, 360); resize(820, 540);
    auto *layout = new QVBoxLayout(this);
    auto *explanation = new QLabel(uiText(english,
        "选择要保留的值。双方都改过的字段必须逐项选择；文件中缺失的字段保持目录值。",
        "Choose which values to keep. Fields changed on both sides require a choice; absent fields keep catalog values."), this);
    explanation->setWordWrap(true); explanation->setTextFormat(Qt::PlainText); layout->addWidget(explanation);
    auto *table = new QTableWidget(this);
    table->setObjectName(QStringLiteral("xmp-merge-fields"));
    table->setColumnCount(4);
    table->setHorizontalHeaderLabels({uiText(english, "字段", "Field"), uiText(english, "当前目录", "Current catalog"),
                                     uiText(english, "侧车文件", "Sidecar"), uiText(english, "选择", "Choice")});
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionMode(QAbstractItemView::NoSelection);
    table->setWordWrap(true); table->verticalHeader()->hide();
    table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    table->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Fixed);
    table->setColumnWidth(3, 170);
    for (const auto &delta : plan.fields()) {
        if (delta.decision == Decision::Absent || delta.decision == Decision::Equal) continue;
        const int row = table->rowCount(); table->insertRow(row);
        table->setItem(row, 0, new QTableWidgetItem(label(delta.field, english)));
        table->setItem(row, 1, new QTableWidgetItem(valueText(delta.field, delta.catalog, english)));
        table->setItem(row, 2, new QTableWidgetItem(valueText(delta.field, delta.sidecar, english)));
        for (const int column : {0, 1, 2}) table->item(row, column)->setToolTip(table->item(row, column)->text());
        if (delta.decision == Decision::Conflict) {
            m_hasIncomingChange = true;
            auto *choice = new QComboBox(table);
            choice->setObjectName(QStringLiteral("xmp-choice-%1").arg(int(delta.field)));
            choice->setAccessibleName(label(delta.field, english) + uiText(english, "：选择来源", ": choose source"));
            choice->addItem(uiText(english, "请选择…", "Choose…"), -1);
            choice->addItem(uiText(english, "保留当前目录", "Keep catalog"), int(XmpMerge::Choice::KeepCatalog));
            choice->addItem(uiText(english, "使用侧车值", "Use sidecar"), int(XmpMerge::Choice::UseSidecar));
            table->setCellWidget(row, 3, choice); m_choices.insert(int(delta.field), choice);
            connect(choice, &QComboBox::currentIndexChanged, this, [this] { updateReady(); });
        } else {
            const bool take = delta.decision == Decision::SidecarOnly;
            m_hasIncomingChange |= take;
            table->setItem(row, 3, new QTableWidgetItem(take ? uiText(english, "使用侧车修改", "Use sidecar edit")
                                                          : uiText(english, "保留目录修改", "Keep catalog edit")));
        }
        table->setRowHeight(row, delta.field == Field::Develop ? 86 : 56);
    }
    layout->addWidget(table, 1);
    auto *notice = new QLabel(uiText(english,
        "显影参数按整个快照选择（上方为摘要）。原有显影撤销保持可用；目录标注暂不可撤销。侧车文件不会被改写。",
        "Develop selects the complete snapshot (summary shown above). Develop undo remains available; catalog annotations are not undoable. The sidecar file is not rewritten."), this);
    notice->setObjectName(QStringLiteral("xmp-merge-notice")); notice->setTextFormat(Qt::PlainText);
    notice->setWordWrap(true); layout->addWidget(notice);
    if (!warnings.isEmpty()) {
        auto *external = new QLabel(warnings.join(QLatin1Char('\n')), this);
        external->setObjectName(QStringLiteral("xmp-merge-external-warnings"));
        external->setTextFormat(Qt::PlainText); external->setWordWrap(true); layout->addWidget(external);
    }
    m_status = new QLabel(this); m_status->setObjectName(QStringLiteral("xmp-merge-status"));
    m_status->setWordWrap(true); m_status->setTextFormat(Qt::PlainText); layout->addWidget(m_status);
    m_buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Apply)->setText(uiText(english, "应用所选字段", "Apply selected fields"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(uiText(english, "取消", "Cancel"));
    connect(m_buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, this,
            [this] { if (resolutions().has_value() && m_hasIncomingChange) accept(); });
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(m_buttons); updateReady();
}

std::optional<QVector<XmpMerge::Resolution>> XmpMergeReviewDialog::resolutions() const {
    if (!m_hasIncomingChange) return std::nullopt;
    QVector<XmpMerge::Resolution> result;
    for (auto i = m_choices.cbegin(); i != m_choices.cend(); ++i) {
        if (i.value()->currentIndex() <= 0) return std::nullopt;
        const auto value = i.value()->currentData().toInt();
        if (value != int(XmpMerge::Choice::KeepCatalog) && value != int(XmpMerge::Choice::UseSidecar)) return std::nullopt;
        result.append({XmpMerge::Field(i.key()), XmpMerge::Choice(value)});
    }
    return result;
}

void XmpMergeReviewDialog::updateReady() {
    int missing = 0;
    for (const auto *choice : m_choices) if (choice->currentIndex() <= 0) ++missing;
    m_buttons->button(QDialogButtonBox::Apply)->setEnabled(m_hasIncomingChange && missing == 0);
    m_status->setText(!m_hasIncomingChange ? uiText(m_english, "没有需要导入的变化。", "No incoming changes to apply.")
        : missing ? uiText(m_english, "还需选择 %1 个冲突字段。", "%1 conflicting fields still need a choice.").arg(missing)
                  : uiText(m_english, "选择已完整；应用前还会检查当前版本是否发生变化。", "Choices are complete; the current version must still be checked before applying."));
}
