# F4 M3.4 / M4.0 — canvas guide repaint and manual brush geometry pilot

Branch `design/lightcraft-fusion-20261008`; PR #16 remains **Draft**. An exact-head three-platform CI is required. This stage adds **no user-visible brush tool** and is not a finished mask editing subsystem.

## M3.4: fix canvas guide invalidation

The QML Canvas caches its drawn ink and does not automatically invalidate because `root.mode` changed. Switching between the previously implemented linear / oval tools could retain the old geometry even when the new Repeater handles had already appeared. Explicitly request a new paint on every mode change, retaining the existing gesture-end policy.

The independent native synthetic-image GUI smoke now verifies the last painted guide mode and monotonic paint serial, in both linear -> oval and oval -> linear directions after earlier event-triggered paints settle. All old handle/Shift/Undo/ROI/source-SHA checks remain mandatory. The independent deadline remains 30s (45s outer script); the original full RAW GUI CPU deadline stays at **60,000ms**.

## M4.0: geometry and raster coverage oracle for future non-AI brushes

`core/pipeline/BrushMaskGeometry.h` is a C++20, Qt-independent geometry primitive; no saved state, image I/O, model weight or background network request. It is referenced by synthetic `JixelLightCoreTests` but is **not connected** to the live image pipeline.

- All path/dab centers are normalized to the complete post-geometry / committed-crop image. The brush radius is measured as a fraction of the shorter image side so a round brush stays circular in physical pixels even on non-square images. Current supported radius is [0.002,0.5], hardness and opacity [0,1], pressure [0,1].
- `StrokeBuilder` samples each piecewise-linear pointer path at fixed spacing `radius/4` in that short-edge metric, interpolating pressure. For an identical geometric path and linear pressure function, the output is independent of pointer event frequency. The last tip is included within the sampling spacing. One invalid point, size or too-long stroke invalidates the builder; no partial result is allowed for later serialization.
- A `Stroke` has up to **4096 dabs**; a collection up to **64 strokes**. All numeric values must be finite and bounded, frames must have positive dimensions <= 131072. `bounds` provides a conservative normalized region for future tile culling.
- CPU reference coverage is smoothstep feathered, scaled by pressure. Within one stroke overlapping dabs use max alpha to avoid densification artifacts; separately committed strokes use source-over alpha union. Invalid stroke or frame refuses computation rather than claiming zero-alpha success. **This is only a reference oracle**; per-pixel full-frame O(dabs) iteration is not yet a production GPU/CPU tile implementation.

## Acceptance and non-goals

Synthetic CoreTests must cover landscape/portrait aspect, exactly predicted normalized radii and feather, identical stroke dabs at different pointer event densities, pressure interpolation, repeated dabs and separately committed strokes, invalid NaNs/bounds and overlong-stroke rejection. Native QML pointer smoke must confirm repaint after both tool swaps.

No RAW base exposure, Sony Creative Look, XMP/project serialization, GPU shader, 1024-bin Scopes, default CPU rendering, existing linear/oval mask parameter or source file is modified. Preserve all previously enforced exact-head CI, real Sony RAW/JPEG, Windows/macOS deployed-package, 40/65535 RGB and Windows 60,000ms CPU smoke gates.

**Next implementation stages:** (1) versioned, explicit brush stroke schema and atomic history/virtual-copy/preset transfer with backward-compatible legacy absence; (2) safe crop-relative pointer capture and editable visual brush overlay; (3) tile-bounded CPU reference plus QRhi-compute parity / deliberate, labeled fallback and quantitative tests; (4) multiple independently enabled mask layers with strict ordering and one-gesture Undo. None is claimed complete in M4.0.
