# F3 Stage 6C.3 — offline, SHA-pinned private RAW/JPEG sensor audit

Branch: design/lightcraft-fusion-20261008, PR #16 Draft.
Status: implementation prepared, mandatory exact-HEAD 3-platform CI pending.

## Objective

A **private, low-memory, reproducible** camera-RAW check for genuine owned originals. Previously the user-provided samples were opened and unpacked locally with LibRaw, but not exercised in the JixelLight native pipeline. This stage supplies a JixelLight-native tool to test the pre-development unpack contract on those originals without shipping them to GitHub, allocating full-scene FP32 RGB or altering default photo output.

Two distinct verification levels MUST NOT be confused:

1. **Offline identity/sensor check**: original SHA-256 matches, LibRaw can unpack within configured memory budget, U16 sensor geometry/black/white/histogram and 2x2 parity are measured with no demosaic/white balance/RAW RGB rendering. An optional independently SHA-pinned camera JPEG is checked with Exiv2 for same capture identity, including subsecond/exposure fields when available.
2. **Color / new FP32 scientific validation**: requires separate measured CFA phase, black/white linearity, four-site white balance, D50 matrix, aligned reference color patches, and held-out photo fidelity; **NOT** carried out here. A same-name camera JPEG with tone/Creative Look is not physical sensor-linear ground truth.

## Native implementation

- `RawDecoder::inspectUnpacked(path, expectedSha256, error, cancel)` reuses the serial RAW lease and streaming source hash, rejects malformed/lowercase hash identities, excessive file or image sizes and cancellation, then calls LibRaw `open_file/unpack` **without `dcraw_process()`**. It uses the existing `SensorMosaicAudit` U16 statistics implementation, records actual sensor array dimensions, LibRaw visible active bounds, black/white, per-channel corrections, CFA filter code and unverified WB/linear maximum metadata. All fields are explicitly observational. No path or filename is returned.
- `JixelLightOfflineRawProbe` is an **offline-only, uninstalled** native executable accepting `private-manifest.json NEW-report.json` with exactly 2–12 source cases. Every original RAW and optional same-capture JPEG must have an exact 64-digit lowercase SHA-256. Training and held-out cases must be separate and source digests unique. A JPEG is not claimed to be aligned just because its metadata match.
- Per-case output deliberately excludes original path, Drive URL/ID, GPS, RAW/JPEG pixels, thumbnails and image files. The report contains SHA-bound evidence, source-free LibRaw/Exiv2 metadata, signed/nominal code statistics and whether each pair passed capture identity. It is created with a same-filesystem temp directory, non-overwriting rename; rejected input produces no success report.
- Explicit report policy: `fullRgbDevelopExecuted:false`, `realFP32TrialExecuted:false`, `cameraCalibrationAccepted:false`, `defaultRawReplacementAllowed:false`. A successful source unpack cannot authorize RAW pipeline replacement or sensor highlight recovery.

## Private local test manifest

**Do not commit this input to public source control.** It contains full local file paths and per-photo digests.

~~~json
{
  "schema": 1,
  "purpose": "offline-local-raw-sensor-audit-v1",
  "cases": [
    {
      "id": "high_iso_train",
      "role": "train",
      "expectedCameraModel": "CAMERA-MODEL",
      "source": {"file": "/private/photos/source-one.arw",
                 "sha256": "<SHA-256 of source-one.arw>"}
    },
    {
      "id": "heldout_pair",
      "role": "validation",
      "expectedCameraModel": "CAMERA-MODEL",
      "source": {"file": "/private/photos/source-two.arw",
                 "sha256": "<SHA-256 of source-two.arw>"},
      "jpeg": {"file": "/private/photos/source-two.jpg",
               "sha256": "<SHA-256 of source-two.jpg>"}
    }
  ]
}
~~~

Run from a local JixelLight build:

~~~bash
path/to/JixelLightOfflineRawProbe PRIVATE-MANIFEST.json NEW-REPORT.json
~~~

If local memory is tight, set `JIXELLIGHT_RAW_MEMORY_MB=512` or a higher appropriate budget. The tool decodes **one RAW at a time** and never allocates a full RGB development image. If the hardware or file requires more memory, failure is expected and must not silently fall back to a cached JPEG.

Do not place this manifest or result inside the public checkout unless covered by local ignore rules. The 3-source user-private collection and any Drive identities remain **only** in the private test workspace, not README, PR, CI logs, artifacts or code.

## Public CI / regression

After the existing checksum-pinned public Sony RAW/JPEG matrix + Stage 6C.1 evidence and Stage 6C.2 fail-closed readiness validation:

- Build the same `JixelLightOfflineRawProbe` native binary on Windows x64, macOS arm64 and Linux.
- `tools/ValidateOfflineRawProbe.py` constructs a **temporary manifest of two already public Sony ARW/JPEG pairs**, one train and one held-out, and checks source SHA, camera model, sample count, correct sample fingerprint and matched camera JPEG Exiv2 identity.
- Reject duplicate train/validation bytes, mismatched SHA and overwriting a previous numeric report.
- Upload only the **public-fixture numeric** `build/offline-raw-probe.json` as an additional validation artifact. No user/private original is copied to public CI.
- Preserve all earlier 154 Core, 30 mandatory GPU, 27 performance, real Sony ARW/JPEG rendered matrix, monitor color and GUI/deployed package gates.

## Further work (F3 Stage 6C.4)

Run this native tool locally on the owned high-ISO Sony and paired Sony RAW/JPEG across at least two bodies, verify the expected sensor array vs active photo bounds, and report any unsupported pitch/format honestly. For genuine FP32 scene color validation, obtain independently measured/verified WB and D50 matrix, then run `JixelLightFloatRawTrial` against held-out reference chart patches; never fit and test on the same photos.

If the private source includes people, locations or personal information, use only numeric aggregate summaries in public reports and keep any preview/checkerboard/ROI screenshot entirely private.
