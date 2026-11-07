# F3 — retain linear FP32 inputs through streamed JPEG development

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Accepted at HEAD `96e487a13dd89e1e26d18f9b70e41831e02f58a5` by [CI #40280382817](https://github.com/snailjayxxx/JixelLight/actions/runs/40280382817), which includes the JPEG commit `855e09d`.

## Problem and change

JPEG export previously converted every non-RGBA64 input to RGBA64 before the color pipeline. For an explicit linear RGBA32FPx4 scene, that clipped HDR and negative channels before user exposure and color transforms. A neutral source value of 2 with -2 EV should reach the next linear stage as 0.5; the former intermediate conversion reduced it to 0.25. Raster export and full scopes already preserved the explicit float source.

`exportJpegTiled` now retains RGBA32FPx4 and RGBA64 inputs, converting only other formats. Input preparation and allocation refusal occur after whole-operation brush preflight and before JPEG writer creation. Each strip still enters the same CPU color kernel and produces the same final RGBA64 output; the existing RGB888 conversion, JPEG quality/ICC encoding, geometry, progress, cancellation and QSaveFile publication remain. A failed strip now returns a nonempty rendering error rather than an empty JPEG-writer error.

This correction applies to explicitly supplied linear float scenes. Normal camera RAW still uses the existing LibRaw RGBA64 decode; it does not enable native FP32 RAW, change RAW baseline exposure or claim additional camera calibration.

## Verification

The old implementation was run against the new tests before the fix: a uniform HDR scene differed from the float reference by **51/255**, and signed/alpha color blocks by **255/255**. After retaining float input, both pass the unchanged **3-code 8-bit JPEG tolerance**.

- A 64×160 uniform HDR image crosses the 128-row strip boundary, uses -2 EV and quality 100, and checks sRGB/Display P3 pixels against the direct float pipeline plus final RGB888 conversion. A pre-clipped reference differs by more than 20 codes, confirming the fixture detects the original error.
- Aligned color blocks cover HDR, negative RGB and four alpha values. Interior JPEG pixels match the float reference within three codes; decoded ICC matches the requested output space, and original float bytes/fingerprints remain unchanged.
- NaN, infinity, over-limit channels and invalid alpha are rejected even when cropped away, preserving the existing full-source geometry-validation contract and existing/new destinations.
- A legal preflight followed by an over-8-EV research brush render refusal returns `Image rendering failed`, leaves progress at zero and publishes no partial file.
- RGBA64 and its lossless float adapter produce byte-identical JPEG files, protecting the ordinary integer path.

Local Qt 6.8.3 Release compilation and all five targeted regressions passed. Complete local CTest passed **10/10 in 82.48 s**: **187 Core / 35 mandatory Metal GPU / 36 performance** checks, checksum-pinned public Sony RAW/JPEG and zero skips. Final-tree CI passed Windows x64/macOS ARM64/Linux GPU, **10/10 CTest, 189 Core / 35 GPU / 36 performance per platform, zero failed or skipped**, including all five JPEG regressions. Tested merge `0b82829f7af4db159d3baec5d8211c46d271c6cb` shares tree `2a9b6d93edc017333a3f5b342167a31a54ff22c2` with HEAD. Both actual packages passed all six validation modes with SDK paths removed and no QML errors; CPU RAW GUI 59,890ms Windows / 43,201ms macOS and complete 10,340,960-pixel scopes passed the unchanged gates. Existing public Sony RAW/JPEG, mandatory GPU 40/65535 precision, 1024-bin Scopes, original 60,000ms RAW CPU GUI and Windows/macOS deployed-package gates remain unchanged.
