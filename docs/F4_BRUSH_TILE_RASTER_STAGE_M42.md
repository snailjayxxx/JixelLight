# F4 Stage M4.2 — bounded brush-exposure tile CPU oracle

Development: `design/lightcraft-fusion-20261008`, PR #16 Draft.

**Scope:** standalone CPU reference only. This is not yet wired into the editor, PhotoController, RAW decoder, Project.db, XMP, exported-photo pixel engine or QRhi shader. It must not create a user-visible brush tool or claim GPU parity.

## Pixel semantics

`BrushMaskTileRaster.h` consumes a validated M4.1 `BrushMaskDocument::Document`, the complete post-geometry/crop frame dimensions, a requested tile, and an **unassociated / tight-packed** linear RGBA32F input span. Each pixel center samples global `((x+0.5)/frameWidth,(y+0.5)/frameHeight)`, never tile-relative coordinates. Brush radius is measured along the shorter image edge in physical pixels, so non-square scenes preserve circles. Per-dab coverage uses the same cubic feather and pressure semantics as the accepted scalar oracle.

For each enabled layer: use **maximum** dab coverage within one stroke, source-over **union** across separate strokes, then add `layerExposureEv * layerAlpha` to the local EV sum. Apply `exp2(sumEV)` to unassociated scene-linear RGB. Preserve incoming alpha exactly, including alpha=0 hidden RGB, negative out-of-gamut channels and >1 HDR headroom. The default empty document is exact identity. Disabled layers preserve their payload without color effect.

Each invocation handles at most 128×128 pixels and at most 8,388,608 candidate dab-pixel visits; it allocates only the requested tile. Input RGB must be finite within ±65,536, alpha finite in [0,1], document/rectangle valid. Overflow, excessive CPU work, cancellation or invalid source **rejects the entire output atomically**: no clipping, no partial RGB, no source mutation, and no misleading success. A failed call preserves the caller's previous output.

## Test coverage and acceptance gate

Add deterministic CoreTests comparing **every** frame pixel to `BrushMaskDocument::weightedExposureStops` in multiple overlapping layers and source channels (linear >1, negative, half/zero alpha); compare four uneven tiles bit-for-bit to a whole-frame tile; check default identity and source immutability; reject invalid frames, incomplete source buffers, nonfinite RGB, bad alpha, overflow, pre-cancel and excessive dab workloads without modifying prior outputs. Core tests must stay fast enough for strict deployed smoke.

Continue mandatory exact-HEAD Windows x64, macOS ARM64 and Linux GPU builds and all existing Core/GPU, real Sony RAW/JPEG, Windows/macOS deployed binaries, 40/65535 CPU/GPU tolerance, full-image 1024-bin Scopes and Windows **60,000 ms** RAW CPU GUI limit. Passing these only validates this isolated CPU oracle and the absence of regressions, **not** a GPU brush renderer or Lightroom-like paintable brush.

Next: profile the tile kernel, connect validated brush documents to explicit, versioned photo edit intent with atomic undo/preset/XMP/project migration, then implement matching QRhi/fallback and live canvas input before enabling brush UI. No auto-AI selection is included.
