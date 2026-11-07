# F4 M4.4B.1 — mask-only EV plane without dummy RGBA tile buffers

Branch `design/lightcraft-fusion-20261008`, PR #16 **Draft**. This stage is only an optimization / regression candidate. Do **not** mark accepted before the new commit's exact-HEAD Windows x64, macOS ARM64 and Linux GPU jobs (including deployed desktop packages) succeed.

## Motivation

M4.4A established correct in-memory, scene-linear brush exposure after camera WB/global exposure and the existing linear/oval masks, before vignette/tone. It reused the M4.2 RGBA tile reference renderer by allocating a zero-filled RGBA32F input and returning an RGBA32F image that was discarded: the live CPU pixel kernel consumed only the EV plane. This unnecessarily initialized, validated and scaled dummy RGB and allocated two tile-sized color arrays.

## Change

Refactor the existing `core/pipeline/BrushMaskTileRaster.h`, rather than introducing a second brush algorithm:

- The private `detail::calculateCoverage` computes exactly the original double-precision dab pressure/feather, max-within-stroke, source-over-union-across-strokes, and ordered layer EV accumulation. Both public interfaces use the **same** coverage function, so their mask positions and numeric behavior cannot diverge.
- The new `exposureOnly(..., ExposureOutput*, cancel, NormalizedFrame)` returns a single `weightedExposureEv` float array and candidate-dab-pixel visit count without requiring *any* RGBA input or output. It validates document, full-source dimensions, 128×128 tile and normalized ROI, checks cancellation/budget, and publishes its result **atomically**.
- The existing `render(..., Output*)` still takes and validates unassociated row-tight RGBA32F and uses the **double** coverage stops for `exp2`, preserving historical HDR/negative/Alpha math and error semantics. Its output remains atomic on invalid input, finite/overflow failure, cancellation, or work-budget exhaustion.
- The M4.4A `ImagePipeline::processColorFrame` active-brush research branch now calls `exposureOnly`, removes the previously persistent 128×128 zero-filled RGBA32F source and per-tile unused RGBA32F result, and hands the same EV values to the identical WB/masks/brush/vignette/tone kernel. At maximum tile size this removes two 262,144-byte color scratch arrays; measured throughput improvement is **not claimed** without separate profiling.
- Existing safety limits (at most 2,097,152 active research pixels, 8,388,608 candidate visits per tile, 100,663,296 visits per frame, enabled-brush direct project/Controller/XMP refusal) remain intact. No change to default/no-brush rendering, QRhi shader, Sony Look, RAW decoder, ICC presentation or persisted schema.

## Acceptance

The `brushExposureOnlyMatchesRgbaOracleWithoutScratchColor` CoreTest verifies exact exposure-plane equality with the original RGBA oracle and exact visit counts, correct whole-frame versus four nonuniform subtiles, independent weighted-EV scalar oracle using a half-image viewport, signed HDR/negative scene channels and bit-exact alpha in the legacy RGBA reference. It also checks inactive identity, invalid geometry/ROI, duplicate layer IDs, pre-cancel and over-budget refusal without changing the caller's previous result, and immutable source input.

The existing M4.4A Core and mandatory GPU test must still exercise real processed pixels, cropped ROI, source alpha, negative HDR RGB, explicit `cpu-reference-upload` fingerprint, six full/viewport×sRGB/P3/ProPhoto parity comparisons with **max 40/65535** unchanged, 1024-bin exact Scopes, and zero false native-brush claims. Real checksum-verified Sony ARW/JPEG, Stage 6C, Windows/macOS deployed app checks and the original **60,000ms Windows CPU RAW GUI limit** stay mandatory. Do not skip failed tests.

## Next step

M4.4B.2 should profile representative brush workloads and improve frame-level work planning/cancellation without weakening deterministic, bounded CPU/GPU equality. A full-size camera RAW brush editor, enabled persisted masks, multi-mask GUI and export parity remain future gates—not delivered here.
