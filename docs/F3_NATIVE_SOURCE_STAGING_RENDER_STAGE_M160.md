# F3 M160: native brush upload fingerprint and bounded ownership

The independently accepted parent26ebacc remains frozen. Main and protected
PR15 are untouched; this extends only explicit native-brush diagnostics.

## Frozen unsupported-stage baseline

Test-only3413268 leaves production unchanged. Python291 passes in1.555472s;
fresh configuration76.289817s and native GPU target build51.737782s pass.
Actual Metal focused execution exits1 (2 passed/1 failed/0 skipped). The final
working-output fingerprint,32 samples, stamp, dispatch/readback counts, normal
finish and zero resource-leak assertions pass before the absent input child
fails. All28 files (5,214,400 bytes) plus complete source archive remain in place
with verified retained copies. Manifest SHA256:
2491e4307102408b240030d5477338881574dc799337d3f5e586bcfa5bac5a73.
The original baseline test body and all its assertions remain byte-identical.

## Change

- Native diagnostics add the same `source_staging` prepared-upload boundary as
  the ordinary engine. The actual existing FP32 source texture is read back;
  no CPU surrogate, re-upload, sensor float or calibration claim is introduced.
- Settled frames keep only a weak input handle. Inputs retain small source
  description metadata after upload CPU payloads are released. Only an explicit
  diagnostic takes a GPU-use hold on a still-available source, releasing it after
  both readbacks complete; the existing frame hold protects final output.
- Final output retains priority within the unchanged64MiB aggregate cap. Optional
  source readback also reserves staging/host capacity in the existing3GiB native
  budget. If that larger reservation fails, the original final-only reservation
  is attempted. `budget`, `resource-budget` and `source-retired` children are
  explicit and carry no pixel hash. Final-output samples and fields are preserved.
- Completion, pending-work and teardown checks wait for both results. Generation,
  target/revision and owner retirement continue to reject stale diagnostics.
  Reservations and both payloads are released before the single delivery call.
- Existing source textures already have the transfer-source flag. No ordinary
  texture, upload, dispatch, readback or finish is added. All shaders,65vec4/1040B
  uniforms, CPU/RAW kernels, admission policies,1024-bin histograms and existing
  RGB40/65535, exact-alpha/resource/deadline conditions are unchanged.

Three GPU methods cover padded uploads, budget boundaries plus native capacity
fallback, two outstanding captures with released inputs, source-already-released
final-output availability, target retirement and shutdown. Independent final
GPU readback and all legacy32-sample conditions remain. Reusable fixtures release
CPU canonical payloads before the new diagnostic to exercise real GPU ownership.
Native route and post-revision GUI diagnostics compare actual uploaded pixels
with frozen CPU FP32 input. Portable validation checks source identity, complete
fingerprints, declared refusals and aggregate accounting, retaining all original
native conditions. Three new validator tests bring Python291 to294; native16
becomes19, while ordinary legacy85 remains. Preflight rejects an incomplete18.

## Acceptance pending

Portable native evidence35 and preflight-wrapper6 checks pass during review.
Implementation is committed before a separate fresh NAS Release build. Python294,
actual focused GPU5/0, complete native19/ordinary85, full106/all100/all33/eighteen
runtimes/five graphs, private compatibility, all three own CI jobs and actual
shipping-package verification remain required. No full-App/new-diagnostic ASAN,
physical-input or photographic calibration acceptance is claimed.
