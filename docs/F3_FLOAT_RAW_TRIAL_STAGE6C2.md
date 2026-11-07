# F3 Stage 6C.2 — opt-in real RAW FP32 calibration trial and patch evaluator

Branch: design/lightcraft-fusion-20261008; PR #16 stays Draft.
Status: **isolated research tool accepted** at 6fc358d4a8174b1e0aa843634480b11d66e2e507 via [CI 38737985662](https://github.com/snailjayxxx/JixelLight/actions/runs/38737985662), all Windows x64/macOS arm64/Linux GPU Success. Camera-specific FP32 RAW fidelity remains **not calibrated and not accepted**.

## Why this stage exists

Stage 6C.1 accepted real Sony ILCE-7M4 U16 unpacked-sensor evidence (2 train, 1 held-out scenes, integrity checked on Windows, macOS, Linux). It did **not** establish true CFA phase, sensor linearization, an independent four-site WB, or a D50 camera matrix. Stage 6B previously proved only synthetic FP32 Bayer interpolation.

This stage adds a real-file-capable but separately invoked laboratory path, without conflating **a technically executed experiment** with **verified camera color**.

## Actual implementation

- `core/raw/FloatRawTrial.{h,cpp}` parses a small explicit JSON experiment. It **requires** purpose `experimental-research-only`, the original RAW SHA-256, a sensor-calibration SHA-256 **verified against the bytes of an explicit local calibration evidence file**, explicit 2x2 Bayer CFA / per-site absolute U16 black and white codes, a separately identified four-site white balance and 3x3 camera-to-D50-linear-ProPhoto transform, also bound to its own SHA-verified evidence file. Distinct readable calibration evidence files (at most 1 MiB each) are required. SHA integrity is enforced, while human-readable scientific provenance remains an externally supplied claim, not independently authenticated.
- Sensor/color calibration evidence files are read-only and their bytes must match the respective SHA-256. The two files and their digests must differ, and neither may point to the RAW source. This prevents accidental substitution of an unrelated profile, but cannot authenticate the scientific claims in either document.
- Source and input/output pixel rectangles are absolute sensor coordinates. Both tiles have a 1,048,576 pixel ceiling; `RawDecoder::calibratedFloatTile` supplies strict LibRaw source hashing, Bayer phase, black-level checks and halo validation. Unsupported packed/X-Trans/abnormal black patterns do not silently guess a decoder.
- The tool optionally checks a **separate SHA-256-pinned reference JSON** with 2–64 disjoint, absolute sensor-coordinate patches whose means are explicitly stated in *D50 linear ProPhoto RGB*. It rejects an incorrect source hash, reference domain, overlapping/out-of-range patches, non-finite values or missing measurement provenance. For each patch it computes candidate FP32 mean RGB, signed RGB error, and across patches the mean absolute / RMS / maximum-channel absolute error, without clipping to 0–1.
- Candidate images are **never** exported or added to the project. The report contains only source/calibration digests, decoder and pixel SHA-256 fingerprints, bounded numerical metadata and patch errors. The existing RAW file and optional reference file are read-only. Output uses staged, new-file-only publication.
- A good numerical match **cannot** set `cameraCalibrationAccepted` or `defaultRawReplacementAllowed`. Those remain `false`, as the user-supplied calibration's scientific independence and scene geometry have not been independently verified by JixelLight.
- The stand-alone `JixelLightFloatRawTrial` binary is built on all platforms but not installed into ordinary Windows/macOS application packages. `--run experiment.json report.json` performs the opt-in study; `--readiness sensor-evidence.json readiness.json` reports `not-executed` when no calibration data are supplied.
- New C++ regression tests cover strict plan validation and reset on failure, signed/>1 float ROI mean parity, input domain/source identity, patch overlap, missing/reference invalid values and non-finite pixel refusal. Each CI platform also executes `ValidateFloatRawTrial.py`, verifying the earlier genuine Sony evidence refuses false calibration, the research runner rejects fake RAW and degenerate matrices, and existing reports cannot be overwritten.

## Example plan (illustrative only; not a valid camera profile)

The identifiers and numbers below are **placeholders** and MUST be replaced with independently evidenced camera values before a trial can execute. An original RAW file's digest is distinct from the calibration evidence digest.

~~~json
{
  "schema": 1,
  "purpose": "experimental-research-only",
  "source": {"file": "/local/path/original.arw", "sha256": "<64 lowercase hex>"},
  "sensorCalibration": {
    "sha256": "<SHA-256 of sensor-evidence.txt>", "evidenceFile": "sensor-evidence.txt",
    "provenance": "External documented black/white and CFA study",
    "cfa": [0, 1, 3, 2],
    "black": [512, 512, 512, 512],
    "white": [16383, 16383, 16383, 16383]
  },
  "colorCalibration": {
    "sha256": "<SHA-256 of color-evidence.txt>", "evidenceFile": "color-evidence.txt",
    "provenance": "Independent D50 chart XYZ to linear ProPhoto measurement",
    "whiteBalanceBySite": [1.0, 1.0, 1.0, 1.0],
    "cameraToProPhoto": [1,0,0, 0,1,0, 0,0,1]
  },
  "sampleRoi": [100, 100, 66, 66],
  "outputRoi": [101, 101, 64, 64],
  "reference": {
    "file": "independent-reference-patches.json",
    "sha256": "<64 lowercase hex>",
    "domain": "linear-ProPhoto-D50-patch-means-v1"
  }
}
~~~

If the reference is present its separately authored JSON must have `schema:1`, `domain:"linear-ProPhoto-D50-patch-means-v1"`, `sourceRawSha256`, `measurementMethod` (a descriptive claim), and 2–64 `patches`: each patch has a sensor-space `rawRoi:[x,y,w,h]` entirely inside `outputRoi`, and `meanRgb:[R,G,B]` as finite scene-linear values. Patches must not overlap.

## CI and safeguards

Full Windows/macOS/Linux CI still includes real Sony ARW/JPEG/Legacy RAW rendering, independent held-out evidence, CPU/GPU 40/65535 accuracy, exact 1024-bin Scopes, GUI, safe CLI and actual desktop package validation.

The new CI Stage 6C.2 step generates `build/float-raw-readiness.json`, whose key outcomes must be: `status:"not-executed"`, `realRawTrialExecuted:false`, `cameraCalibrationAccepted:false` and `defaultRawReplacementAllowed:false`. **Passing this step does NOT claim any Stage 6B/6C.2 real-photo color fidelity.** It only proves that the trial tool and its negative/authorization gates work and that the real Sony dataset is known to lack an independently substantiated calibration.

## Still unfinished

- An independently verified Sony/DNG/CR3/NEF **camera linear calibration dataset** (CFA phase, black/white, camera WB, D50 chart reference and source identity).
- Genuine real-file FP32 candidate-vs-reference color error measured on disjoint held-out scenes; RGB patch RMSE is not perceptual DeltaE or a whole-frame artifact detector.
- Alignment across RAW crop/orientation, lens vignette correction, illumination/ISO stratification and representative skin/highlight/edge regions.
- Edge-aware demosaic, full-resolution tiling, cross-platform CPU/GPU native FP32 parity, memory/throughput and validated .jlfs cache integration.

Do **not** enable the new decoder in normal application UI, replace the established LibRaw RGBA64 or merge PR #16 before these are fulfilled.
