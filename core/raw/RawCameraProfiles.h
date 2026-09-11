#pragma once

#include <QString>
#include <array>
#include <optional>

struct RawCameraProfile {
    QString make;
    QString model;
    int blackLevel = -1;
    int whiteLevel = -1;
    // DNG/Adobe ColorMatrix semantics: camera-channel rows, XYZ columns.
    // Values are normalized (the published integer matrix divided by 10000).
    std::array<double, 12> cameraToXyz{};
    QString provenance;
};

namespace RawCameraProfiles {
std::optional<RawCameraProfile> find(const QString &make, const QString &model);
}
