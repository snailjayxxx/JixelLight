#pragma once

// In the exact-60-second GUI smoke, only steps which *read pixels/scopes*
// may wait for async image processing. The batch export naming dialog's
// presentation, localized labels, validation, cancel and history checks
// and selected-version snapshot checks (phases 290..299) depend only on QML
// state and are safe while CPU or GPU
// is still rendering the previous photo. Phase 50 MUST wait for the source
// RAW and full-resolution scopes to be ready after selecting it again.
namespace SmokePhasePolicy {
inline constexpr int DeadlineMs=60000;
constexpr bool isRenderIndependentExportUi(int phase) noexcept {
    return phase>=290 && phase<=299;
}
constexpr bool canAdvance(int phase,bool imageReady) noexcept {
    return imageReady||isRenderIndependentExportUi(phase);
}
constexpr bool finalPresentationReady(bool imageAndScopesReady,bool gpuRequired,bool gpuActive) noexcept {
    return imageAndScopesReady&&(!gpuRequired||gpuActive);
}
}
