#pragma once
#include <QObject>
#include <QVariantMap>

class RecipeClipboardReader final : public QObject {
    Q_OBJECT
public:
    explicit RecipeClipboardReader(QObject *parent=nullptr):QObject(parent) {}
    Q_INVOKABLE QVariantMap snapshot() const;
};
