# F3 — FP32 scene-linear geometry and full-resolution scopes (stage 2)

Working branch: `design/lightcraft-fusion-20261008` / Draft PR #16.
Stage 1 (`33381a5`, [run 38201306237](https://github.com/snailjayxxx/JixelLight/actions/runs/38201306237))
established direct FP32 scene color processing and GPU numeric fallbacks.
Stage 1 follow-up (`ff94a51`, [run 38202436018](https://github.com/snailjayxxx/JixelLight/actions/runs/38202436018))
was accepted on **all three OSes** (133 core / 28 mandatory GPU / 25 performance checks
per platform, real Sony ARW/JPEG, GUI, packaged Windows/macOS apps).

## Problem: geometry and statistics still quantized early

Before this change, an optional `QImage::Format_RGBA32FPx4` source passed to
`GeometryState::apply` with nonzero straighten/perspective/radial correction/
lateral CA was converted to `QImage::Format_RGBA64` **before** resampling.
This lost scene RGB above 1.0 and negative out-of-gamut values. `QImage`'s
generic transform functions could also convert FP32 sources. Furthermore
`ScopesEngine::analyzeFull` converted every input to RGBA64 **before**
the color kernel, so histogram/scopes values could disagree with the correct
FP32 preview and export whenever exposure or highlight recovery were active.

## Implemented

1. New internal `core/pipeline/FloatGeometrySampling.h` provides
   floating-point **premultiplied-alpha bilinear** sampling, with double
   accumulation and unassociated `RGBA32FPx4` output. RGB is never
   gamut-clipped or quantized in geometry. Alpha is finite and [0,1].
   Hidden colors with zero alpha do not contaminate visible neighbors.
2. `GeometryState::apply` selects a separate FP32 straighten path,
   preserving the existing pixel-center-inscribed trim, crop and
   source color-space / device / text metadata. It validates the
   explicit FP32 source before any transform, including identity.
   The old integer code remains in its own `RGBA64` branch.
3. `GeometryCorrections.cpp` adds a corresponding FP32 path for manual
   horizontal/vertical perspective, radial distortion, red/blue lateral
   CA and simultaneous straighten. Each channel uses the same
   coordinate mapping and its own CA radial sample, while **green**
   defines the alpha pixel; every source-coordinate constraint and
   conservative inscribed crop remains identical.
4. For FP32 90°-step rotations and flips, `GeometryState` copies each
   16-byte IEEE-754 pixel **bit-for-bit** without delegating to an
   image-format-converting Qt transform. Zero-alpha RGB and signed zero
   survive these axis-only operations unchanged.
5. `ScopesEngine::analyzeFull` consumes FP32 scene input directly in the
   same `ImagePipeline::processRegion` as preview/PNG/TIFF export.
   Only the **result** is quantized to RGBA64 for 1024-bin histogram counting.
   The RGBA64 statistics input and historical behavior remain unchanged.
6. Stage dependency diagnostics now report the actual FP32 input and
   associated-alpha sampling, rather than incorrectly describing all
   geometry as 16-bit. This does not change parameter schema, Sony
   Creative Look, output profiles, GUI, or the default RAW source.

## New validation

- Analytical tests for FP32 straighten and the existing projective/radial/CA
  coordinate formula using affine color gradients above 1 and below 0.
- Transparency sample with intentionally bright, hidden blue and a quarter
  opacity output: invisible color must not leak through associated interpolation.
- All sixteen combinations of 0/90/180/270° rotation and horizontal/vertical
  flip: full FP32 pixels, including negative values and signed zero,
  retain their exact IEEE-754 storage bits.
- Invalid or non-finite FP32 inputs reject before identity/crop/straighten/
  corrections and must not create a valid full-scopes cache entry.
- Prepared preview source, color-geometry diagnostic manifest, full-resolution
  scope histograms/cache, 16-bit PNG and TIFF exports must agree exactly with
  the same geometry and color plan; early-clipped source must differ.
- Mandatory GPU tests on corrected FP32 frames, including native compute and
  D3D/OpenGL CPU numerical fallback, four output spaces, unchanged
  **40/65535 RGB** bound, exact alpha and 1024-bin histogram.
- Three-platform CI, original Sony real RAW/JPEG and unchanged GUI/export
  package tests remain mandatory. New stage not accepted until exact-HEAD
  Windows/macOS/Linux run passes.

## Deliberate limitations

This is **not** a native floating-point LibRaw sensor decoder. Real ARW, CR3,
NEF and other camera RAW continue through LibRaw's existing 16-bit developed
bitmap and Jixel Neutral v2. No clipped photosite information is recreated.

The GPU still only performs the color-compute stage; manual perspective,
lens/CA and straighten resampling run on CPU. FP32 intermediates require
**16 bytes per pixel**, twice the RGBA64 storage; this stage does not promise
faster resampling or reduced memory use. Source/disk caches still preserve
only existing real RAW RGBA64 sources. Dynamic preview resizing and automated
lens-profile lookup require separate auditing. PNG16/TIFF16 remain
output-quantized and ICC-tagged, not scene-linear float exports. No new
camera white-level calibration or fixed RAW exposure offsets are introduced.

Next F3 work: verify anti-aliased FP32 downscaled proxy/thumbnail generation;
review SourceCache persistence for float decoded sources; then investigate
camera-linear LibRaw processing with sensor black/white levels and **actual**
highlight headroom before any integer development, without changing the
validated alpha.11 RAW zero point.
