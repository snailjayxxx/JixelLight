# F3 — FP32 scene-input preservation (stage 1)

Branch: `design/lightcraft-fusion-20261008`, PR #16 (Draft).
This is an incremental precision fix, **not** a claim that LibRaw has been upgraded to a native float RAW decoder.

## What was wrong

The QRhi color shader already accepts `RGBA32F`, but CPU rendering always passed
input images through `ImagePipeline::rgba64Source()` first. That helper is a
valid **output/display quantizer**: it clamps channel values into [0,1] and
stores 16-bit RGBA. It is not valid as a scene-linear input adapter. If a
future float camera stage supplies red=2.25, converting it through RGBA64 first
silently changes it to 1.0 *before* exposure compensation and highlight
reconstruction. Windows D3D11 and Linux OpenGL compute also take a CPU numeric
fallback for complex HSL. That fallback had the same early clipping problem.
A frozen GPU diagnostic CPU oracle likewise compared against a clipped input.

## Implemented

1. `ImagePipeline::processWithPlan`, `processRegion` and
   `diagnoseWithPlan` accept explicit `QImage::Format_RGBA32FPx4` input
   directly. A shared color-pixel kernel preserves out-of-range scene values
   and negative out-of-gamut channels until exposure, highlight recovery, tone,
   look and output encoding. The regular `RGBA64` path remains unchanged.
2. `GpuEngine::process` sends **the exact FP32 upload source** to the CPU
   numeric fallback; it no longer round-trips through a clipping RGBA64 proxy.
3. Bug-report frozen GPU inputs use that same FP32 CPU oracle. The diagnostic
   records its input storage and distinguishes source FP32 precision from
   16-bit output quantization.
4. An explicit validation gate rejects NaN/Inf and scene RGB values outside
   [-65536,65536], or alpha outside [0,1]. It runs for FP32 CPU inputs and
   on GPU **new-source uploads only**, not on every slider revision.
   Rejected frames do not enter the render/cache pipeline.
5. Existing source cache/render keys already include QImage storage format
   and source cache identity. The optional float path does not change
   default RAW/JPEG rendering, the compiled engine profile ID or existing
   saved project JSON.
6. Tests cover over-white exposures, signed channels, unassociated alpha,
   transparency/hidden RGB, float CPU image/ROI/diagnostics, invalid input
   rejection, packed 16-bit pixel identity across existing effects/output
   profiles, mandatory native-GPU plus CPU-fallback FP32 parity and the
   absence of GPU uploads after rejected input.

## Data format and boundary contract

| Boundary | Current format | Implication |
|---|---|---|
| Real LibRaw unpack/demosaic | LibRaw 16-bit bitmap -> QImage RGBA64 | Camera highlight information already truncated inside LibRaw cannot be recovered |
| Optional explicit scene input | RGBA32FPx4, linear ProPhoto or explicitly tagged encoding | Values above display white survive CPU pipeline and GPU numeric fallback |
| Existing source cache & disk proxy | RGBA64 | Not yet a lossless persistence format for FP32 scene input |
| Optical corrections/straighten | RGBA64 CPU resampling | Not yet safe for optional out-of-range FP32 scene values |
| QRhi upload + color compute | RGBA32F | Full FP32 values, output texture in display-referred space |
| CPU color/export result | RGBA64 | Final output intentionally quantized; RGB and alpha stored at 16-bit |
| Output JPEG/PNG/TIFF/WebP | Existing codecs and ICC pipeline | No change to output-bit-depth promises |

The float input is **not** automatically designated RAW: callers must pass
`InputEncoding::LinearProPhoto`, the intended output space, an explicit
`rawSource` flag, and an independently verified camera exposure baseline.
The older input-encoding overload is retained for historical test compatibility.

A synthetic scene-linear FP32 source now renders its red=2.25 using the actual
2.25 value, so exposure −3 EV applies to 2.25/8, **not** 1.0/8.
This does not reconstruct a camera photosite clipped by the sensor or a
16-bit value already clamped in the old LibRaw processing flow.

## Acceptance

- Same exact-commit three-platform GitHub Actions matrix as F0–F6, including
  real Sony ARW/JPEG, mandatory QRhi compute, Windows/macOS deployed package
  checks and the original 60-second RAW GUI smoke.
- Retain max CPU/GPU RGB difference **40/65535**, exact alpha, 1024-bin
  scopes, Sony Creative Look and zero-test-skip requirements.
- CPU reference tests require bit-identical output for existing 16-bit sources
  staged as float; synthetic FP32 values above one must visibly differ from
  an intentionally clipped proxy. FP32 stage diagnostics must describe the
  actual input >1 before output shoulder.
- **Stage 1 accepted:** commit `33381a56234c7a9e255fa6733c83b668e780abc4`,
  [CI run 38201306237](https://github.com/snailjayxxx/JixelLight/actions/runs/38201306237)
  succeeded across **Windows x64 / macOS arm64 / Linux GPU**, including 132 core,
  28 mandatory GPU and 25 performance checks on every platform, real Sony
  RAW/JPEG fixtures, GUI, Windows/macOS package validation and no GPU skip.
  The 12 new FP32 color-matrix fixtures have maximum channel delta **1 Linux,
  4 macOS, 1 Windows /65535** (existing max 40/65535).
- **Stage 1 follow-up (new exact-head CI pending):** exercise the prepared
  FP32 preview source and crop-only `GeometryState::apply` path, plus an
  independently decoded PNG16 export. The optional FP32 data must survive
  these stages above 1.0 until explicit display conversion. This follow-up
  tests crop-only geometry, **not** rotate/straighten/lens corrections.

## Next F3 work

1. Characterize LibRaw's 16-bit `dcraw_process` saturation/quantization and
   image calibration against real ARW/DNG/CR3/NEF inputs and sensor-white
   metadata, without changing the Jixel Neutral v2 zero point.
2. Design a camera-linear FP32 decode/demosaic pipeline that preserves
   unsaturated channel headroom **before** integer LibRaw output processing,
   respecting camera WB/matrix, sensor black/white levels and orientation.
   Do not simply convert the current 16-bit bitmap to float and call it HDR.
3. Extend optical geometry/crop resampling, source and disk-cache formats,
   CPU/GPU prepared previews, all export paths and scopes to handle true
   high-dynamic-range FP32 working sources without implicit RGBA64 fallbacks.
4. Benchmark live RAW memory/throughput and validate on independently held-out
   camera/JPEG fixtures; preserve a safe, explicit 16-bit compatibility path.

The stage-1 changes make future float sources meaningful in the shared color
graph. They intentionally do **not** claim completion of the F3 full RAW
pipeline, floating geometry, lossless FP32 source persistence, sensor highlight
reconstruction, or full-output floating-point image export.
