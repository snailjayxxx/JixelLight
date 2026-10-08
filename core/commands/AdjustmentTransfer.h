#pragma once
#include "core/pipeline/AdjustmentState.h"
#include <QStringList>
#include <QSet>

// Copy stored editing intent, including shared immutable Sony LUTs. Camera
// metadata, catalog annotations and history are never part of a transfer.
namespace AdjustmentTransfer {
inline QStringList allGroups() {
    return {"exposure","white_balance","tone","color","hsl","curves","sony_look","geometry"};
}
inline bool validGroups(const QStringList &groups) {
    if (groups.isEmpty()) return false;
    const auto allowed=allGroups(); QSet<QString> seen;
    for (const auto &group : groups) {
        if (!allowed.contains(group) || seen.contains(group)) return false;
        seen.insert(group);
    }
    return true;
}
inline bool apply(AdjustmentState &target,const AdjustmentState &source,const QStringList &groups) {
    if (!validGroups(groups)) return false;
    for (const auto &group : groups) {
        if (group=="exposure") target.exposure=source.exposure;
        else if (group=="white_balance") { target.temperature=source.temperature; target.tint=source.tint; }
        else if (group=="tone") {
            target.contrast=source.contrast; target.highlights=source.highlights; target.shadows=source.shadows;
            target.whites=source.whites; target.blacks=source.blacks; target.highlightRecovery=source.highlightRecovery;
        } else if (group=="color") { target.hue=source.hue; target.saturation=source.saturation; target.vibrance=source.vibrance; }
        else if (group=="hsl") { target.hslHue=source.hslHue; target.hslSaturation=source.hslSaturation; target.hslLuminance=source.hslLuminance; }
        else if (group=="curves") { target.masterCurve=source.masterCurve; target.redCurve=source.redCurve; target.greenCurve=source.greenCurve; target.blueCurve=source.blueCurve; }
        else if (group=="sony_look") target.look=source.look;
        else if (group=="geometry") target.geometry=source.geometry;
    }
    return true;
}
}
