# F3 — scene-linear FP32 preview scaling and antialiasing (stage 3)

Development branch: `design/lightcraft-fusion-20261008`, Draft PR #16.
This stage does **not** claim native floating-point LibRaw sensor decoding.

## Accepted baseline

F3 stage 2 at `9969f5e510cb4886dd7e1632e28e3195337bdc07`,
[CI run 38322823120](https://github.com/snailjayxxx/JixelLight/actions/runs/38322823120),
completed **Success** on Linux GPU / Windows x64 / macOS arm64 with 137 core,
29 mandatory GPU and 25 performance correctness tests per platform, actual
Sony RAW/JPEG, GUI integration and Windows/macOS deployed packages.
The new corrected-scene GPU samples had maximum CPU/GPU channel error
1 Linux, 1 Windows and 2 macOS /65535 (original gate is 40/65535).
The initial C++ compilation failure was repaired by explicitly including
`QColorSpace` in the geometry helper; no intermediate failed commit was merged.

## Scope: three preview resizing points

The existing `preparePreview` path performs geometry correction, then
downscales to a fit-size image, optionally caps the longest image edge
at 4096 for QRhi staging, and creates a fast 1024-edge slider draft.
The old path calls `QImage::scaled(..., SmoothTransformation)` at each stage.
That remains **unchanged for existing RGBA64 RAW/JPEG sources**.

For explicit `QImage::Format_RGBA32FPx4` scene-linear sources, the generic
Qt scaling behavior is not an acceptable assumption for data outside [0,1]:
over-white highlights, signed out-of-gamut channels, alpha and linear-light
sampling must remain consistent between normal/fast/GPU inputs. The new
`FloatSceneResampler::scale` path:

1. Validates source RGB finite within ±65536 and alpha finite in [0,1],
   respecting cancellation, without altering any source pixel.
2. For **downsampling**, integrates source pixel footprints over each
   destination pixel by normalized exact fractional **area/box weights**
   on both axes. This removes checkerboard aliasing instead of choosing
   a few point samples and preserves mean linear light on integer reductions.
3. For **upsampling**, uses pixel-center-aligned bilinear interpolation,
   clamping samples to image edges. A 1:1 resize returns the same immutable
   QImage without changing hidden RGB bits or allocating a copy.
4. Applies the separable filter in associated-alpha double arithmetic,
   then writes **unassociated RGBA32FPx4** with alpha clamped only for
   floating rounding. Invisible RGB cannot bleed into visible edges.
   RGB is **never clipped to [0,1]**, tone-mapped, or converted to RGBA64.
5. Reuses the bounded ParallelRows CPU worker budget and only allocates
   one source-width associated-color scratch row per active worker.
   No additional full-resolution float intermediate frame is allocated.
6. Copies ICC/color-space identity, DPI, device pixel ratio and image text.
   Cancellation rejects partial frames, leaving the 128 MiB prepared
   preview LRU untouched. Cache keys continue to include original image
   cache identity, format, geometry, zoom and viewport. No disk cache
   format, saved project schema or source-decoder behavior is changed.
7. Writes `preview_fp32_scale` performance spans and a descriptive
   `preview_resize` entry into the on-demand StageGraph diagnostic
   manifest. Normal display monitor ICC remains presentation-only.

## Test evidence required for acceptance

- Core tests for 8×8 antialiased checkerboard down to 2×2:
  exact mean 2.0 from alternating 0 and 4, with negative-green preservation.
- Alpha-edge test: one opaque colored sample + three transparent hidden
  blue samples down to one pixel must retain the opaque RGB and alpha 0.25;
  fully transparent output must have zero color, not hidden blue.
- Bilinear 2×2 → 4×4 gradient with known pixel-center values, and same-size
  QImage identity.
- Fit → fast → GPU prepared-preview stages from synthetic full-size float
  scene, real cache hit identity and source-bit fingerprint invariance.
  One-to-one viewport ROI remains a byte-identical crop, and the separate
  longest-edge 4096 ceiling uses the same float filter.
- Invalid float values, invalid dimensions and cancelled jobs must not
  publish output or populate the LRU.
- Performance correctness checks compare serial vs worker-pool output
  pixel-exactly and verify RGBA32FPx4 memory size without imposing an
  unreliable CI-host throughput benchmark.
- Mandatory QRhi native compute/CPU fallback checks on downsampled
  FP32 input across four output color spaces, exact alpha and 1024-bin
  GPU scopes, retaining the original maximum **40/65535** RGB delta.
- Exact-HEAD CI must pass on Windows x64, macOS arm64 and Linux GPU,
  including existing real Sony ARW/JPEG, 60-second GUI smoke, actual
  deployed Windows/macOS packages and protected offline CLI export.

## Known boundaries and follow-up

True RAW source decoding still produces **16-bit RGBA64** via LibRaw:
converting those decoded pixels to FP32 is not a sensor highlight recovery.
The new resize kernel is a CPU preparer, **not** a GPU resize stage; it is
not an exact recreation of Qt's RGBA64 SmoothTransformation algorithm,
nor does it alter the default RAW/JPEG path.

Area/box filtering is a robust first antialiasing method, not a fully
configurable Lanczos/edge-adaptive reconstruction. The fast preview
intentionally downsamples the already prepared fit image to reuse a
bounded image, so it is not a raw sensor-level high-frequency oracle.
SourceCache and on-disk proxies still store RGBA64 real RAW data.
A later F3 milestone must design a genuine scene-linear camera RAW
decoder/demosaic stage with sensor black/white metadata, verified
highlight reconstruction and a versioned FP32 source cache, benchmark
memory and throughput, and preserve Jixel Neutral v2 camera baselines.
