# F3 Stage 5 — pre-development RAW sensor mosaic and calibration audit

Branch: `design/lightcraft-fusion-20261008` / PR #16 (Draft).
Baseline: stage-4 versioned FP32 source cache `4c8e978`, three-platform
[CI 38534048071](https://github.com/snailjayxxx/JixelLight/actions/runs/38534048071)
**Success** (142 core / 30 mandatory GPU / 27 performance checks per platform;
real Sony ARW/JPEG, GUI, Windows/macOS deployed packages).

## Why this boundary matters

JixelLight presently uses LibRaw `unpack()` followed by `dcraw_process()` into a
**16-bit linear ProPhoto RGBA64 bitmap**. A later conversion into FP32 is not an
unclipped camera-linear RAW decoder. Correct native FP32 processing requires
verifying the camera's sensor sample codes, active array and black/white
calibration **before** LibRaw modifies them through black subtraction,
demosaic, white balance, automatic-maximum handling or color matrices.

LibRaw's documented `rawdata.raw_image` is a single-component U16 Bayer
buffer after unpack. It may already have undergone camera-specific unpack
linearization, but it has not been converted into RGB by `dcraw_process()`.
LibRaw's `color.black`, `color.cblack`, `color.maximum` and
`color.linear_max` have different semantics; `maximum` can subsequently
change when auto-maximum or black correction runs.

Official references:
- https://www.libraw.org/docs/API-CXX.html (unpack and black subtraction)
- https://github.com/LibRaw/LibRaw/blob/master/doc/API-datastruct.html
- https://www.libraw.org/comment/4509 (raw_image and non-Bayer caveats)

## Implementation (opt-in; no default rendering change)

- New `core/raw/SensorMosaicAudit.h/.cpp` inspects a **tightly packed,
  single-plane U16** sensor buffer, using the known LibRaw raw dimensions
  and `raw_pitch == raw_width*2`. Unsupported three/four-plane, unusual
  row pitches, empty or out-of-range active rectangles do not guess a
  reinterpretation; they return `available:false` with an explicit reason.
- Scans exactly `[left_margin,top_margin,width,height]` before
  `dcraw_process()`, with a hard 200-million-pixel limit and a
  single 65536-bin heap histogram. Cancelling mid-scan returns no partial
  acceptance. No whole-frame RAW copy is created.
- Returns finite per-frame min/max, mean, exact U16 p50/p99/p999,
  code-65535 count, sample counts relative to **unadjusted reported**
  nominal black/white thresholds, and parity (even/odd row/col) summaries.
  Parity cells are deliberately **not** labeled as red, green or blue;
  true CFA color requires full Bayer/X-Trans phase information.
- Provides a canonical SHA-256 raw-sample fingerprint over a documented
  stage label and active row-major little-endian U16 bytes, excluding
  row padding and platform byte order.
- When the caller explicitly sets `RawMetadata::captureSensorAudit`,
  `RawDecoder::decode` captures `RawMetadata::sensorAudit` before normal
  dcraw processing. It also reports raw pitch, CFA filters **without
  guessing phase**, camera orientation, `black`, four `cblack` offsets,
  the optional vendor black-pattern entries, the four metadata
  `linear_max` values and the pre-develop WB multipliers.
- The default `RawDecoder::decode` call does **not** request or scan the
  sensor; all existing Jixel Neutral v2, Sony Look, LUT, GPU and export
  behavior remains unchanged. The audit is **not** a new source cache,
  pixel renderer, sidecar field or project migration.
- The real Sony ARW/JPEG `JixelLightRawRenderMatrix` validation opt-ins
  the audit during its existing single RAW decode and includes the result
  as `scene.sensorUnpackedAudit` in the regression JSON, without
  changing color or baseline results. An unavailable sensor layout is
  shown transparently and does not reject an otherwise valid raw format.

## Regression gates

- Synthetic active mosaic 4×4 with known code distribution: exact sample
  count, p50/p99/p999, mean, nominal white/black counts, 2×2 parity and
  reproducible canonical SHA.
- A partial active rectangle must not hash or count unexposed sensor borders.
  Changing nominal metadata thresholds must not change the pixel SHA.
- Reject null image pointer, odd/padded pitch, out-of-bounds dimensions,
  empty crop, >200-million sample allocation and cancelled scans.
- The 512-KiB-stack real RAW worker requests an audit while performing the
  existing successful Sony decode. It verifies 1:1 code parity sums and
  valid SHA when LibRaw provides a compatible raw_image; otherwise the
  unavailability reason must be nonempty.
- The real RAW/JPEG matrix validates the sensor report if available while
  retaining the paired JPEG/RAW CPU/GPU measurements and original pixel
  baselines.
- Exact-head CI requires 3-OS Release build, mandatory GPU 40/65535
  parity with zero skips, real Sony files, GUI, deployed Win/macOS packages,
  and unchanged Jixel Neutral v2 color semantics. The stage is not
  accepted unless the complete run succeeds.

## Limits / next work

This stage is **measurement**, not scene-linear FP32 demosaic or highlight
reconstruction. A sensor code above a nominal white threshold is not
automatically recoverable RGB highlight information; camera metadata may
encode already corrected values or unusual per-pixel black patterns.
16-bit unpack sample values are not guaranteed to be linear or equivalent
between manufacturers. Floating-point native camera formats, unsupported
raw pitches, multi-plane formats, actual X-Trans/CFA phase determination
and gain/nonlinearity calibration need separate verified implementation.

Next, use actual Sony/DNG/CR3/NEF audit reports to design a safe opt-in
camera-linear float mosaic interpreter, verify black-level subtraction,
per-channel phase and white normalization against held-out real camera
references, then design demosaic/WB/color space stages that genuinely
preserve sensor headroom. Bind every decoder setting and correction
matrix to the already accepted `.jlfs` versioned FP32 source cache signature
before turning the pipeline on for normal RAW files.
