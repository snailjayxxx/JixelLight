# F4 Stage M4.1 — strict, bounded versioned brush-layer document

Branch: `design/lightcraft-fusion-20261008`, PR #16 Draft. This stage is a **new exact-HEAD CI candidate**, not yet accepted.

## Implementation

`core/pipeline/BrushMaskDocument.h` is a standalone, opt-in Qt JSON adapter for the already accepted `BrushMaskGeometry` CPU oracle. No production loading/writing, hidden brush UI button, new adjustment command, RAW/GPU shader or XMP/project schema is enabled.

An explicit brush document has canonical JSON `{"schema":1,"layers":[...]}`; each layer has **exactly** `id`, `enabled`, `exposure` (EV -3..3), and `strokes`. A stroke contains exactly `radius`, `hardness`, `opacity` and `dabs`, with every dab a numeric triple `[x,y,pressure]` in the complete post-geometry/crop normalized frame. Layer array order is preserved for future compositing; IDs are ASCII letters/digits/hyphen/underscore, nonempty, at most 64 characters, case-sensitive and unique within the document.

Guards: <=16 layers, <=64 strokes per layer, <=4096 dabs per stroke, <=16,384 dabs across the document, with existing finite/radius/pressure bounds. A malformed, future, redundant-empty, unknown-field, duplicated-ID or over-limit object is **rejected in full**. Failed parsing leaves a previously valid document untouched; failed serialization never modifies the destination. Missing `brushMasks` represents the legacy default and serializes as an absent optional field; an explicitly saved null/{} is never treated as a valid empty brush.

The CPU-only `weightedExposureStops` oracle composes enabled layers as `sum(exposureEV * unionStrokeCoverage)`; disabled layers retain their entire stroke payload without pixel effect. This validates the intended scene-linear exposure semantics but is **not called by ImagePipeline or GpuEngine**. It does not claim actual output parity, persistence, brush painting or multiple fully editable masks.

## Required test/acceptance

`JixelLightCoreTests` covers order and byte-equivalent canonical JSON round trips, weighted local exposures, invalid camera-frame coordinates, disabled layers, legacy-absence nonmutation and atomic rejection of invalid schema/types/array ranks/IDs/strokes/parameters. Tests exercise maximum layer/stroke/aggregate-dab limits without accepting partially decoded content.

Any further activation needs separate migration/provenance and undo/redo tests, cache-revision dependencies, tiled CPU/GPU masked-pixel parity <= 40/65535, full 1024-bin Scopes, exact photo/export parity and source SHA protection. Keep existing 60,000ms full RAW GUI smoke, real Sony RAW/JPEG and Windows/macOS deployed package CI gates unchanged. Preserve Sony Creative Look and monitor ICC isolation.

## Next stages

M4.2: opt-in per-photo draft brush state and atomic undo history with strict real-pixel preview and no silent unsupported-format drop. M4.3: tile-bounded CPU reference and explicit QRhi supported/fallback path under forced cross-platform numeric acceptance. M4.4: actual canvas pointer painting, brush controls and secure project/preset/XMP history. M4.5: unify linear/oval/brush independently enabled mask layers with versioned migration.
