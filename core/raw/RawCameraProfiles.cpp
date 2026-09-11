#include "core/raw/RawCameraProfiles.h"

namespace RawCameraProfiles {
std::optional<RawCameraProfile> find(const QString &make, const QString &model) {
    const QString normalizedMake = make.trimmed();
    const QString normalizedModel = model.trimmed();

    if (normalizedMake.contains(QStringLiteral("SONY"), Qt::CaseInsensitive)
        && normalizedModel.compare(QStringLiteral("ILCE-7RM6"), Qt::CaseInsensitive) == 0) {
        RawCameraProfile profile;
        profile.make = QStringLiteral("SONY");
        profile.model = QStringLiteral("ILCE-7RM6");
        profile.blackLevel = 512;
        profile.whiteLevel = 16383;
        profile.cameraToXyz = {
             1.1765, -0.5595, -0.1192,
            -0.3689,  1.1507,  0.2485,
             0.0051,  0.0681,  0.5731,
             0.0,     0.0,     0.0
        };
        // Public camera characterization merged by RawSpeed after validation
        // against public ILCE-7RM6 lossless samples and Adobe DNG Converter
        // 18.4.1. Keep the provenance with the data instead of presenting
        // these numbers as Sony-proprietary coefficients.
        profile.provenance = QStringLiteral(
            "RawSpeed PR #979 / public ILCE-7RM6 lossless samples / ADC 18.4.1");
        return profile;
    }

    return std::nullopt;
}
}
