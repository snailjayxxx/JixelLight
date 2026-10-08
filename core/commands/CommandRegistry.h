#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QJsonArray>
#include <algorithm>
#include <cmath>

// UI and offline CLI share parameter names, finite checks and existing ranges.
struct CommandRegistry {
    struct Parameter { const char *name; double AdjustmentState::*member; double minimum, maximum; };
    static const auto &parameters() {
        static const std::array<Parameter,12> descriptors{{
            {"exposure",&AdjustmentState::exposure,-5,5},
            {"temperature",&AdjustmentState::temperature,-180,180},
            {"tint",&AdjustmentState::tint,-180,180},
            {"contrast",&AdjustmentState::contrast,-180,180},
            {"highlights",&AdjustmentState::highlights,-180,180},
            {"shadows",&AdjustmentState::shadows,-180,180},
            {"whites",&AdjustmentState::whites,-180,180},
            {"blacks",&AdjustmentState::blacks,-180,180},
            {"highlightRecovery",&AdjustmentState::highlightRecovery,0,100},
            {"hue",&AdjustmentState::hue,-180,180},
            {"saturation",&AdjustmentState::saturation,-100,100},
            {"vibrance",&AdjustmentState::vibrance,-100,100}
        }};
        return descriptors;
    }
    static bool set(AdjustmentState &state, const QString &name, double value, QString *error = nullptr) {
        if (error) error->clear();
        if (!std::isfinite(value)) { if(error)*error="Value must be finite"; return false; }
        for (const auto &parameter : parameters()) if (name == QLatin1String(parameter.name)) {
            state.*parameter.member = std::clamp(value,parameter.minimum,parameter.maximum);
            return true;
        }
        if(error)*error="Unknown develop parameter: " + name;
        return false;
    }
    static bool execute(AdjustmentState &state, const QJsonObject &command, QString *error = nullptr) {
        if (command["command"].toString() != "develop.set" || !command["parameter"].isString() || !command["value"].isDouble()) {
            if(error)*error="Expected develop.set with parameter and numeric value";
            return false;
        }
        return set(state,command["parameter"].toString(),command["value"].toDouble(),error);
    }
    static QJsonObject schema() {
        QJsonArray descriptors;
        for (const auto &p : parameters()) descriptors.append(QJsonObject{{"name",p.name},{"minimum",p.minimum},{"maximum",p.maximum}});
        return {{"schema",1},{"command","develop.set"},{"parameters",descriptors},{"range_policy","clamp"}};
    }
};
