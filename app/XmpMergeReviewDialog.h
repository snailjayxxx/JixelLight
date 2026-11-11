#pragma once
#include "core/metadata/XmpMerge.h"
#include <QDialog>
#include <QMap>

class QComboBox;
class QDialogButtonBox;
class QLabel;

// Selection UI only. After exec(), the caller must resolve against the actual
// current target and catalog before its existing explicit import transaction.
class XmpMergeReviewDialog final : public QDialog {
public:
    explicit XmpMergeReviewDialog(const XmpMerge::Plan &, bool english,
                                 const QStringList &externalWarnings = {}, QWidget *parent = nullptr);
    std::optional<QVector<XmpMerge::Resolution>> resolutions() const;
private:
    void updateReady();
    bool m_english = false;
    bool m_hasIncomingChange = false;
    QMap<int, QComboBox *> m_choices;
    QDialogButtonBox *m_buttons = nullptr;
    QLabel *m_status = nullptr;
};
