# F3 M159: native-brush prepared upload diagnostic baseline

Accepted parent26ebacc has local106/all100/all33/eighteen-runtime/five-graph,
private compatibility, all three original CI452 jobs, both shipping ZIPs and
actual macOS shipping CPU/Metal App/diagnostic acceptance. Its qualification
index SHA256 is5aefbaa156a8b2a761cca352b343dd96203f386cbe5dce81bd18ec1f4ee9292d.
Main and protected draft PR15 remain unchanged.

The ordinary engine now fingerprints its actual prepared upload, while native
brush diagnostics still capture only final working output and32 samples. This
test-only checkpoint requires a separate actual native upload fingerprint.
Production remains unchanged. Its focused missing-stage failure must be frozen
before implementation begins in a separate source worktree and physical build.

The finite padded FP32 fixture preserves independent final-output fingerprint,
32-sample, identity, callback, dispatch/readback and zero-budget-leak assertions
before requiring the input stage. It uses actual worker admission and Metal,
with RAW intent/base exposure and prepared crop metadata. It is a prepared
preview texture, not native sensor float or camera calibration evidence.

The existing source texture already supports transfer readback. A computed frame
releases its temporary source handle and plan when settled; a future extension
must preserve only a weak source ID/metadata during normal operation, then keep
an available source alive only while an explicit diagnostic is outstanding.
Released inputs must leave the original final-output capture usable and report
an unavailable source child. Target retirement and shutdown must drain both
results safely and notify once.

Final output stays first within the same64MiB aggregate diagnostic cap. Optional
source capture must also fit the existing3GiB BrushRenderBudget, including
staging/host reservations; insufficient optional capacity must preserve the
original final capture. No new ordinary texture/upload/dispatch/readback/finish,
shader/uniform/colour math, histogram precision or deadline change is intended.
The existing producer, sample and retirement checks remain authoritative.

Baseline3413268 reproduces the missing input stage on Metal (2/1 Qt). All28 evidence files plus complete source archive are frozen. [Implementation](F3_NATIVE_SOURCE_STAGING_RENDER_STAGE_M160.md); new-source acceptance remains pending.
