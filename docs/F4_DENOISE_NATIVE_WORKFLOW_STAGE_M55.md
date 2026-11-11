# F4 M7B — denoise through the normal photo application

The Lightness/Color controls already have kernel, controller, persistence and
cross-backend tests. This stage exercises their complete normal application
workflow through `Main.qml` and `PhotoCanvas`, including the actual export worker.
It changes the owned validation entry point and CI/package coverage; it does not
change denoise arithmetic, default RAW decoding or any admission limit.

## Implemented workflow

- `--denoise-smoke-report` admits only a fresh owned JSON namespace, with no
  positional input or other operating mode. `--denoise-language` supports English
  and Chinese only. Settings and the driver home are isolated.
- An owned 307×211 noisy RGBA64 linear-ProPhoto PNG includes partially transparent
  and zero-alpha pixels. Its original bytes remain unchanged. It is synthetic
  workflow evidence, not a camera-quality or physical-tablet claim.
- The normal window must present three stable frames and admit the entire actual
  drawable before input. At most one contained host fit is allowed. The driver
  decodes every PNG row; it neither crops screenshots nor substitutes images.
- Real QPA mouse events operate Lightness, Color and Reset. Whole Develop states
  retain an independent exposure and grain setting. Two Undo/Redo steps recover
  the first slider state and the final state; denoise Reset preserves exposure
  and grain, and Undo restores both denoise amounts.
- A downstream exposure edit must reuse the real CPU or GPU denoise stage.
  Source FP32 staging remains bit-identical throughout. All four 1024-bin full
  histograms, clipping and three current full-resolution plots are checked.
- A real project is created, flushed, read independently and reopened. Durable
  Reset Redo/Undo must recover the complete expected state and history cursor.
- A frozen original diagnostic ZIP must match the current whole Develop, source
  key and revision. CPU denoise input/output fingerprints must be complete,
  finite and different. The GPU context must report `gpu-compute` for both the
  output and denoise, with numeric policy `none` and monitor ICC excluded; a CPU
  reference upload cannot satisfy this gate.
- The real controller exports PNG16. Every decoded RGBA16 pixel and the embedded
  sRGB ICC must equal the complete CPU reference and frozen diagnostic preview.

The native deadline remains 60,000 ms and the driver's process bound 75 seconds.
The terminal phase identifier is16; the recorded timeline contains phases0–15.
The four captures are complete active/Undo/Redo/Reset windows.

## Local evidence

Qt6.8.3 Release, Apple M2/8GiB, macOS Cocoa, actual DPR2. Pre-publication builds
carry parent stamp `093f5c5` plus the new source files; these observations are not
an exact committed-source or three-platform acceptance claim.

| Observation | Result |
|---|---|
| CPU English normal-App workflow | Passed; native5,061ms, actual 1440×928 host-admitted window |
| Metal Chinese normal-App workflow | Passed; native4,012ms, actual1180×780 window; denoise `gpu-compute`, cache-hit delta2 |
| Complete exported pixels |64,777 each; all RGBA16 and ICC bytes match the independent full reference |
| Evidence refusal tests |5 Python methods pass, including stale source/revision, incomplete frames, CPU-upload producer, unrelated-edit mutation and occupied namespace |
| Actual argument/output guards |7/7 refuse with exit2; all existing owned files unchanged |
| Previous tangential normal-App workflow |CPU and Metal both pass at the same pre-publication build |

Original evidence resides outside Git under
`/Users/jaysnail/.cache/jixellight/denoise-native-stage/`:
`denoise-native-cpu-v2.json`, `denoise-native-gpu-first.json`, their unchanged
native reports/PNGs/ZIPs, `evidence-tests-first.log` and
`argument-guards-first.json`. Both active captures were visually inspected.

The original build failure used a nonexistent `exportFile` entry point; the
correct public controller API is `exportCurrent`. The first native CPU process
completed successfully but its Python validator incorrectly looked for
`spatial_denoise` at the diagnostic root. The strict check now uses the actual
`color_stages.spatial_denoise` schema. Both failed original logs/reports remain;
neither is relabeled as a successful run.

## CI and package scope

The three native platform jobs execute CPU-English and GPU-Chinese contexts and
retain their original reports/captures/ZIPs. Deployed validation appends the two
contexts after the complete original70-mode prefix, for72 checks. All original
checks, ordering, thresholds and deadlines remain required. A successful72-mode
package result has not yet been observed for this stage.

Parent run367 at `d27a46c` has separately completed macOS/Windows/Linux78/78 and
both70-mode shipping validations. Parent run370 at `093f5c5` has passed Linux81/81;
its desktop/package jobs remain in progress at this entry. These are API and
original-log observations, not a newly reconstructed independent acceptance
receipt. Earlier cancelled run369 remains cancelled.

GPU diagnostic output here is a bounded whole-frame FP32 fingerprint, not a new
per-pixel40/65535 proof. The existing mandatory GPU numerical suite remains
required. Native real-camera float decoding, broad camera/ISO quality validation,
physical pressure-device validation and the remaining F0–F6 roadmap are unfinished.
