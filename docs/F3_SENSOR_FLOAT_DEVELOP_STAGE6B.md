# F3 Stage 6B — experimental Bayer demosaic → scene-linear FP32

Branch: design/lightcraft-fusion-20261008; PR #16 remains Draft.
Status: accepted at commit 940829f3557557152670f9cd977b64e425bbc6ed, [CI run 38705643249](https://github.com/snailjayxxx/JixelLight/actions/runs/38705643249) Success on Windows x64 / macOS arm64 / Linux GPU (151 core / 30 mandatory GPU / 27 performance / 10 CTest suites per platform). Acceptance is isolated synthetic correctness and unchanged real-camera legacy paths, not proven real-camera FP32 calibration.

## Purpose and isolation

SensorFloatDevelop consumes unclamped, calibrated Bayer-site floats from Stage 6A, not pixels already converted by LibRaw dcraw_process to RGBA64. It is a separate laboratory API. The regular RAW path, Sony Creative Look, GPU renderer, Library database, XMP, presets, existing source cache and export remain unchanged.

- Inputs are independently sourced original RAW SHA-256, Stage 6A sensor calibration SHA-256, verified 2×2 Bayer CFA and black/white levels, and a separate WB/color-profile SHA-256. The four white-balance gains can differ by green-site phase; there are no implicit camera WB/exposure or guessed color transforms.
- Demosaicing is an intentionally simple bilinear Bayer reference, not an edge-aware production method. For each primary R/G/B channel use the own sensor site, otherwise average the nearest axial neighbors (squared distance 1) or nearest diagonal neighbors (squared distance 2), applying per-site WB before interpolation.
- Convert camera RGB using an explicitly supplied 3×3 camera-to-D50-linear-ProPhoto matrix, with double-precision arithmetic before casting to RGBA32FPx4. Alpha is exactly 1. The result retains negative, out-of-gamut and >1 values, with no 16-bit RGB staging or pre-tone clamp.
- Each output ROI requires the original U16 mosaic's physical active bounds plus a one-pixel input halo wherever actual active sensor sites exist. Missing halo refuses all output, avoiding seams between independently processed tiles. Physical sensor image borders use only available neighbors.
- Maximum input is 1,048,576 float Bayer sites (4 MiB) and output is 1,048,576 FP32 RGBA pixels (16 MiB) per request. Reject mismatched source/calibration signatures, malformed geometry or tile size, bad finite range, degenerate/NaN matrix, invalid WB, cancellation or failure. Do not return a partially valid frame.
- A versioned SHA-256 decoder identity hashes the Stage 6A signature, external profile SHA, and all four WB gains plus nine matrix coefficients encoded as little-endian IEEE-754 binary64. A calibration setting change cannot retrieve another source's future FP32 cache.
- RawDecoder::calibratedFloatTile is an explicit laboratory adapter: after profile preflight it invokes the existing source-SHA-verifying LibRaw pre-development Bayer unpack, then this Stage 6B conversion. It does not call the default RAW developer, create persistent cache entries, change project content or display itself as an available UI option.
- Output images are marked experimental and tagged linear ProPhoto. ImagePipeline::validFloatSource accepts finite scene RGB values with magnitude no greater than 65536. A color transform exceeding this guard refuses the full frame; this is not highlight reconstruction.

## Synthetic and existing tests

New tests cover:
- RGGB with two independently calibrated green sites, negative blue, >1 red and matrix-induced channel mixing; exact opaque alpha and linear ICC; valid float input passed into the existing ImagePipeline CPU reference.
- BGGR with nonzero sensor margins: whole source vs left/right separately decoded tiles with a one-site halo must match float channels bit-for-bit. Without halo must refuse; a physical active image corner must not spuriously demand nonexistent sensors.
- Source SHA and profile SHA changes, each gain, matrix coefficients and sensor black change invalidate signatures. Invalid calibration, NaN/singular matrix, malformed samples, cancellation, exceeding float scene range and original-source SHA mismatch refuse output.

Every existing test remains in the full CI workflow: genuine Sony ARW/JPEG and AHD rendering identity, GPU 40/65535 bound, exact Alpha / 1024-bin Scopes, 60-second RAW GUI smoke, safe CLI, and Windows/macOS deployed binary verification. Three-OS CI success is the acceptance criterion, not local skipped GPU tests.

## What Stage 6B does NOT establish

This prototype does not characterize any actual camera sensor, validate the absolute D50 camera matrix or chromatical adaptation, restore sensor-clipped highlights, compare RAW/JPEG color on held-out Sony/Canon/Nikon/DNG scenes across ISO and exposure, process lens-shading/dead pixels/dual gain, implement edge-aware demosaic or automatic camera profile selection.

There is no normal-preview switch, GPU Bayer compute kernel, full-frame tile orchestrator or .jlfs cache adoption. Stage 6C must collect verified RAW/calibration/reference samples, validate per-camera CFA black/white/linearity and the WB + matrix on held-out scenes, then justify enabling this path after meaningful fidelity/performance thresholds are met.

Official LibRaw data contracts: https://www.libraw.org/docs/API-datastruct.html (CFA filters, cblack/maximum) and https://www.libraw.org/docs/API-CXX.html (COLOR, unpack, subtract_black).
