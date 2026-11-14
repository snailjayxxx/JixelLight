# F3 M157: prepared GPU source upload diagnostic baseline

The independently accepted parent is 1bd4128. Its local 106 tests, all 100 Qt
groups, 33 validation contexts, three original CI451 jobs and actual macOS
shipping CPU/Metal checks pass. Main and protected draft PR15 remain untouched.

The ordinary GPU engine currently fingerprints only the final pre-monitor
working output. Add a diagnostic-only fingerprint of the actual prepared FP32
source upload texture so upload identity can be checked independently from
colour processing. This is prepared/cropped/resampled input, not native sensor
float data, RAW demosaic boundaries or calibration evidence.

This test-only checkpoint adds one finite, padded FP32 fixture. It retains the
independent final-output assertions, then requires an upload-stage fingerprint
and frozen source/revision identity for linear ProPhoto and encoded sRGB.
Production is unchanged. A missing-stage failure must be retained before the
implementation starts in a separate source worktree and physical build.

Implementation must preserve the existing final-output fields and availability,
reserve its bytes first within the unchanged aggregate 64MiB diagnostic budget,
and explicitly mark an optional source capture unavailable when space is
insufficient. Normal interaction continues to read only 16,400 histogram bytes;
no shader/uniform change, extra ordinary readback, texture or per-frame finish
is authorised by this diagnostic extension. CPU reference uploads must be
labelled accurately. Native brush capture remains a separate producer.

Baseline852b6fa executed as an expected missing-stage failure (2/1 Qt); all26 files plus complete source archive are frozen. [Implementation and acceptance scope](F3_GPU_SOURCE_STAGING_RENDER_STAGE_M158.md). New-source acceptance remains pending.
