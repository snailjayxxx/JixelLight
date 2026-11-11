#pragma once
#include "core/gpu/brush/NativeBrushUpload.h"
#include "core/pipeline/BrushMaskTileRaster.h"
#include <array>

namespace NativeBrush {
enum class FrameFailure { None, WrongThread, Stale, InvalidSource, CPUPlan, CPUPolicy,
    CPUIndex, CPUWork, CPURepresentation, Budget, Cancelled, Allocation, Consumed };
namespace detail {
struct alignas(16) DabRecord {
    std::array<float,4> centerRadiusPeak, shape, residual, edge;
    std::array<int,4> bounds, group;
};
struct alignas(16) TileSpan { std::array<int,4> values; };
struct alignas(16) CoverageUniform { std::array<int,4> tile, counts; };
static_assert(sizeof(DabRecord) == 96 && sizeof(TileSpan) == 16 && sizeof(CoverageUniform) == 32);
}

// CPU worker admission owns the original validated plan and its bounded,
// immutable packet. No GUI/render-thread frame planning or packing is needed.
class FrameAdmission final {
public:
    using Ptr = std::shared_ptr<FrameAdmission>;
    static Ptr prepare(CanonicalSource::Ptr, const SourceCapabilities &, const ProcessingPlan &,
        DeliveryTarget, quint64 revision, FrameFailure * = nullptr, const CancelToken & = {},
        std::size_t indexLimit = BrushMaskTileRaster::MaxCandidateIndexReferences);
    bool discard();
    bool ready() const { return m_state.load(std::memory_order_acquire) == 0; }
    const FrameStamp &stamp() const { return m_stamp; }
    Qt::HANDLE preparationThread() const { return m_preparedOn; }
    qint64 outputBytes() const { return m_outputBytes; }
    qint64 workBytes() const { return m_workBytes; }
    std::size_t originalVisits() const { return m_candidateCount; }
    std::size_t shaderVisits() const { return m_shaderCount; }
private:
    friend class RenderDomain;
    FrameAdmission() = default;
    CanonicalSource::Ptr m_source;
    SourceCapabilities m_caps;
    FrameStamp m_stamp;
    ProcessingPlan m_plan;
    CancelToken m_cancel;
    std::shared_ptr<const void> m_outputCharge, m_workCharge;
    std::vector<detail::DabRecord> m_packed;
    std::vector<detail::TileSpan> m_tiles;
    std::atomic_int m_state{0};
    qint64 m_outputBytes = 0, m_workBytes = 0;
    int m_uniformBytes = 0, m_colourUniformBytes = 0;
    std::size_t m_candidateCount = 0, m_shaderCount = 0;
    Qt::HANDLE m_preparedOn = nullptr;
};
} // namespace NativeBrush
