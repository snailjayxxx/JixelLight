# F3 Stage 6C.1 — checksum-pinned real sensor evidence and coverage gate

Branch: `design/lightcraft-fusion-20261008` / PR #16 (Draft).
Status: implementation staged; **exact-commit Windows/macOS/Linux CI required**.

## What Stage 6C.1 actually establishes

This stage validates measurement provenance, completeness and scientific **coverage gaps** on *real camera files*, not image-quality equivalence of the new FP32 RAW pipeline.

- Reuse the already checksum-pinned three Sony ILCE-7M4 ST RAW/JPEG pairs in `tests/fixtures/sony-real-manifest.json`, with two **train** and one separate **validation** camera capture. This is a limited preproduction single-camera/single-look fixture family, not a calibration dataset for all Sony/Canon/Nikon/RAW.
- Native `JixelLightRawRenderMatrix` now records read-only camera EXIF context (make/model, capture time, ISO, shutter, exposure bias, lens and recorded look) alongside the existing source SHA-256, sensor U16 pre-demosaic audit, camera JPEG statistics and RAW CPU/GPU render comparisons. Raw files are never edited.
- `tools/ValidateSensorEvidence.py` cross-checks every RAW/JPEG hash against the pinned original manifest, unique train/heldout source and capture identities, scene role, real decoder success, 2x2 *parity-not-CFA-color* sample distribution, active sensor bounds, sample counts, min/mean/percentiles, nominal black/white threshold counts and raw-pixel fingerprint. A missing required Sony mosaic or an implausible statistic rejects the entire report.
- For each real scene the report retains *measured* RAW U16 tail and parity summaries and **512-pixel baseline RAW-vs-paired-camera-JPEG** neutral/As Shot RMSE + JPEG highlight clipping. These are diagnostics of the **existing RGBA64 legacy path**. They are not a Stage 6B FP32-vs-camera validation, HDR reconstruction evidence, calibrated transfer function, or sensor color truth.
- `coverage.remainingEvidence` explicitly records missing additional cameras, ISO, shutters, illuminants, camera-specific phase/white/gain nonlinearity, trusted 4-site white balance and independent D50 camera matrix. Unparseable camera ISO/shutter are null, never guessed.
- A successful CI report **must** set `evidenceIntegrityPassed:true`, `cameraCalibrationAccepted:false`, `stage6BRealCameraFidelityValidated:false` and `defaultRawReplacementAllowed:false`. Correctly rejecting false claims is an acceptance criterion. CI never labels photos, calibrated Sony Look or captured sensor codes as validated FP32 color.
- Results are atomic numeric JSON in `build/sensor-evidence.json`, included in the existing three-platform validation artifacts. No copyrighted photos or their source data are added to Git, release artifacts or this report. The existing temporary RAW-render-matrix preview PNGs are handled by the preexisting validation workflow and are not part of this new report.
- `tests/TestSensorEvidence.py` includes positive evidence integrity and negative gates for wrong RAW/JPEG hashes, reused hold-out data, missing/duplicated camera captures, sensor area/parity/count/percentile mismatch, unavailable required audit, malformed RMSE, absent comparison route, and missing ISO coverage. All three OS CI steps run this suite and then analyze the **actual** native real-file output.

## Acceptance is separate from fidelity

Success means **existing real-file identity / native output / evidence policy** is audited across OSes. It does **not** imply that `SensorFloatDevelop::calibratedFloatTile` was run against genuine camera data or that any new FP32 color value matches a camera reference. The only Stage 6B correctness evidence remains its synthetic RGGB/BGGR/halo/CPU tests.

In particular, `sensorUnpackedAudit` samples are pre-demosaic LibRaw U16 sensor codes. `nominal_white_code` may be altered by camera-specific rules, the 2x2 even/odd parity does not establish CFA colors, and Sony camera JPEGs include in-camera gamma, white balance, color rendering, sharpening, tonal curves and Creative Look. Comparing the legacy rendered RAW to those JPEGs is not a sensor-linear oracle.

## Stage 6C.2 experimental real-photo calibration work still required

1. Obtain **independent** RAW reference evidence per camera/ISO/light/exposure: physically measured chart or separately validated raw pipeline, known CFA phase, site black/white/transfer function, true white balance and an XYZ D50 / ProPhoto conversion matrix with documented provenance and SHA-256. Include repeated scenes with highlights, neutral gray, real skin tone and difficult colored edges; keep train/validation camera files disjoint.
2. Build a manifest-specified **opt-in** real-file Stage 6B candidate (using the two existing calibration SHA signatures), reconstruct only safe bounded halo tiles and compare them with independent calibrated reference in the same image geometry/encoding. Never infer a calibration matrix from the paired camera JPEG by itself.
3. Set predeclared color-error/DeltaE, neutral-grayscale, highlight roll-off / saturation, chromatic artifacts, sensor-white, transform-domain and performance limits; compare training and held-out scenes without tuning to validation.
4. Only after cross-camera correctness, ROI alignment, full-frame/tile memory and cache/CPU/GPU parity are documented should Stage 6C authorize `.jlfs` adoption and an opt-in real RAW preview. The default LibRaw RGBA64/Sony baseline remains in force.

Reference: `docs/F3_SENSOR_FLOAT_DEVELOP_STAGE6B.md`; stage 5/6A audits are prerequisites but not independent color calibration.
