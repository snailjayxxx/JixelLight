# F3 M158: fingerprint the prepared source GPU upload

Accepted parent1bd4128 remains independently frozen. Main and protected draft
PR15 are unchanged. This extends only explicit ordinary-engine diagnostics.

## Retained unsupported-feature baseline

Test-only852b6fa leaves all production code unchanged. Python291 passes;
fresh Release configuration82.300691s and GPU-only build57.829183s succeed.
Actual Metal/offscreen focused execution exits1, with2 passed/1 failed/0 skipped
in116ms. Independent final-output checks pass; the new requirement fails because
`source_staging` is absent. This is an unsupported feature, not a regression.
All26 evidence/source/runtime/graph files (5,767,537 bytes) plus a complete source
archive are retained with originals in place. Manifest SHA256:
`6d715e1855fdfa62ffea04671191e1eef6bf18d1a689fc55e791e68d116a5eff`.
The original new test body and all its assertions remain byte-identical.

## Behaviour and limits

- `gpu_working_output.source_staging` fingerprints the actual existing RGBA32F
  upload texture. Metadata freezes source cache key, accepted parameter revision,
  encoding, RAW intent, base exposure, active processing sampling frame rectangle,
  straight alpha and backend. It describes the prepared preview/crop/resampling
  input; it is not native sensor float, demosaic or colour calibration evidence.
- Preserve all final-output fields and reserve final-output bytes first. The
  optional upload read uses the remaining portion of the same64MiB aggregate
  diagnostic budget, also clamping callers that supply a larger budget. When it
  cannot fit, the child reports `available:false, reason:budget`; the final
  capture remains usable. Requested and received totals are explicit.
- Both QRhi results retain ownership until completion; a weak callback captures
  metadata by value, hashes/releases each result, and notifies exactly once after
  both settle. Existing teardown/resize drain rules cover the combined request.
  No per-frame wait is introduced. Normal histogram readback remains16,400 bytes.
- Source metadata advances only with accepted processing commands. Rejected
  source/plan candidates cannot replace diagnostic identity. A CPU reference
  upload still records the prepared texture but marks `used_for_compute:false`.
- Source gains its required transfer-source usage flag, with no new texture,
  shader, colour math or uniform change (65vec4/1040 bytes retained). Native
  brush-domain capture is a separate producer and is not extended by this change.
- Bitwise fingerprints report actual texture data. The exact finite padded fixture
  is verified across supported backends; this does not promise universal bitwise
  preservation of subnormal/NaN payloads through arbitrary graphics drivers.

Three additional GPU methods cover padded linear/encoded uploads, aggregate
budget boundaries and unchanged ordinary readback, plus CPU fallback with a
queued input replacement. Existing independent output, rejection and teardown
assertions are retained and extended. Controller report tests retain the frozen
source child; the real GUI diagnostic compares upload with its frozen FP32 input.
Preflight now requires the complete85-result legacy suite and unchanged16-result
native suite; a new incomplete-suite subcase rejects84 even with a zero exit.

Implementation is committed before a separate fresh NAS Release build. Python291,
new focused tests, original full106/all100/all33/eighteen-runtime checks, complete
private compatibility and all three platform jobs remain pending. Diagnostic
ownership changes have not received full-App ASAN qualification.

Primary API contracts: [Qt6.8 asynchronous result ownership](https://doc.qt.io/qt-6.8/qrhireadbackresult.html)
and [texture transfer-source requirement](https://doc.qt.io/qt-6.8/qrhireadbackdescription.html).
