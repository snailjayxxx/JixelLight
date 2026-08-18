#pragma once

#include <QString>
#include "core/pipeline/AdjustmentState.h"

class ProjectDatabase {
public:
    ProjectDatabase();
    ~ProjectDatabase();
    bool create(const QString &projectDirectory, const QString &projectName);
    bool addOrUpdatePhoto(const QString &path, const AdjustmentState &state);
    bool updateAdjustment(const QString &path, const AdjustmentState &state);
    [[nodiscard]] bool isOpen() const;
    [[nodiscard]] QString projectPath() const { return m_projectPath; }
    [[nodiscard]] QString projectName() const { return m_projectName; }

private:
    QString m_connectionName;
    QString m_projectPath;
    QString m_projectName;
};
