# F4 M8 — independent scene-linear clarity, CPU reference

Clarity is now an independent signed control, separate from the recorded Sony
Look clarity parameter and independent sharpening. Positive values strengthen
local midtone lightness contrast; negative values soften it. The default0 keeps
the exact existing integer/float processing path. This is an explicitly marked
CPU stage, including when the final preview is displayed by QRhi.

## Algorithm and dependencies

The stage follows optional denoise and precedes WB/exposure, masks, tone, look,
output transfer, grain and sharpening. It converts linear ProPhoto to Oklab,
uses an alpha-associated separable Gaussian lightness neighborhood and retains
the center a/b components. Source radius24px scales by prepared/full geometry
density, including original-pixel viewports and fractional radii. Sigma is half
the effective radius. The lightness correction is

`0.6 * amount01 * (L - weightedMeanL) * 4 * clamp(L,0,1) * (1-clamp(L,0,1))`.

No scene RGB clamp is added. Neighbor alpha at or below1e-20 contributes no
weight and those centers remain unchanged. Local visible references and
center-relative sums preserve constant fields through irregular transparent
holes; hidden RGB cannot influence visible colors. The two-dimensional double
oracle is independently evaluated in tests, including fractional support and
clamped frame edges.

Stage storage is32 bytes per expanded pixel: RGBA32F linear16B, guideL4B and a
three-float horizontal record12B. Denoise and clarity execute sequentially with
the same maximum32-byte stage reserve. Their halos **sum**, followed by the
existing detail halo. The32Mi-pixel and3GiB brush limits, payload leases,
preflight/cancellation and60,000ms operation deadline remain unchanged.

Completed pre-tone stages share the same64MiB rendered-preview LRU with final
RGBA64 results and retained LUT payloads. The new clarity key includes source
identity/format, input encoding/matrix rows, optional denoise amounts/density,
clarity amount and effective radius. Downstream WB/EV, output, grain and
sharpening reuse the complete stage. Brush paths bypass stage retention and
retain their owning admission/lease accounting. Existing denoise-only keys and
counters remain; new clarity counters distinguish the additional stage.

## State and interfaces

- One optional canonical section: `clarity:{schema:1,amount:-100..100}`; redundant
  zero defaults, unknown/missing fields, future schema, nonnumbers and invalid
  ranges are refused. Legacy absent sections restore0.
- The shared scalar registry grows21→22 with `clarityAmount` and `clarity.reset`.
  GUI gestures coalesce into one `clarity_adjustment` history entry. Reset retains
  all unrelated edits; virtual copies and project reopen retain history/redo.
- Detail-only transfer includes denoise, clarity and independent sharpening.
  Projects reject malformed reads before replacing their current writer, and
  reject invalid photo/history/copy writes before SQL work. Presets/XMP and CLI
  carry the canonical state. CLI keeps strict types and original-file protection.
- The bilingual collapsed Clarity card explicitly says CPU. The amount control
  supports both signs and Reset/Undo at300/320/350px panel widths.
- Packed plans grow54→55vec4 (880 bytes); slot54 stores amount and frozen source
  density. The engine/profile version remains `jixellight-linear-v5-base2-look4`.
  Default pixels and existing Sony profile compatibility remain unchanged.
- Any active clarity selects complete CPU reference upload in the normal QRhi
  engine and explicit native-brush `CPUPolicy`. Diagnostic output reports
  `spatial_clarity:cpu-reference`, producer `cpu-reference-upload` and reason
  `clarity-cpu-stage`; no clarity GPU computation is claimed. CPU capture includes
  bounded complete stage input/output FP32 fingerprints before monitor ICC.

## Targeted local evidence

Qt6.8.3 Release, macOS26.5, Apple M2/8GiB, four CPU threads. These are
pre-publication observations at parent stamp928a811 plus this working source;
full committed-source/platform/package acceptance remains required.

| Check | Result |
|---|---|
| Standalone kernel |9 passed,0 failed/skipped in74ms; analytic impulse, independent double2D, signed HDR, serial/parallel, cancellation and irregular alpha-hole identity |
| Clarity workflows |17 passed,0 failed/skipped in22,295ms; regions/strips, four spaces, all histograms/plots, state/CLI interfaces, controller, project/copies and positive/negative bilingual controls |
| Core |217 passed,0 failed/skipped in22,354ms |
| GPU and explicit policies |45 passed,0 failed/skipped in8,134ms; the new16 clarity combinations are complete exact CPU uploads, with separate native-admission refusal checks |
| Related CTests |6/6 in21.59s: Performance39, CLI schema/safety, old denoise cache9, clarity kernel9 and clarity cache10 |
| Warm CPU sample |769×513: three cached color edits14.8655ms versus64.2898ms fresh, with exact pixels; charged18,940,928B. One observation, not a general speed claim |
|24MP owned combination |Paint/Erase + denoise + clarity + Sony detail + sharpening/grain passed under the unchanged60s gate; first sample11,290ms, peak2,576,225,792 **accounted bytes**, not RSS |

Evidence is outside Git under
`/Users/jaysnail/.cache/jixellight/clarity-stage/`: `kernel-alpha-holes-v2.txt`,
`workflows-alpha-holes-v2.txt`, `gpu-all-alpha-v3.txt`, `core-all-first.txt`,
`related-alpha-v3.log` and `build/clarity-owned/`. The existing complete normal-App denoise CPU and
Metal workflows also pass at this pre-publication build in
`prior-denoise-cpu.json` / `prior-denoise-gpu.json`. The public held-out Sony07
source SHA is `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d`.
A1600×1000 crop of its7028×4688 RGBA64 decode passes four output profiles and
three independent regions; before/after384px crops and compact bilingual panels
were visually inspected. This remains one ISO1600 scene, not broad ISO/camera,
skin or blind quality acceptance and not native float RAW decoding.

The first build command requested nonexistent target`JixelLightCLI`; the actual
existing target is`JixelLightCli`. Its original failed log remains separate.
No tests were omitted or thresholds loosened to address it. Later alpha-hole
checks strengthened the reference implementation while retaining the32-byte
stage reserve. Earlier successful observations are not substituted for current
committed-source or cross-platform evidence.

## Remaining acceptance

New CTests82–84 are `spatial-clarity`, `clarity-workflows` and
`clarity-preview-cache`. Existing81 names and test limits remain. Current deployed
validation retains its72-mode count and existing full-App denoise gates; the
shared deployed CLI safety script additionally exercises signed clarity and
independent Reset. There is no new74-mode package claim.

Complete clean84/84, three native platforms and both actual72-mode shipping
packages are still required at this entry. Native real-camera float calibration,
clarity QRhi compute, physical tablet evidence, broader camera/ISO quality and
the other unfinished F0–F6 modules remain separate work.

## Clean committed-source follow-up

Exact clean commit`08be052f94591b198418865fc875e44dc4531853`, tree
`c98b8c2c09bd401fc1114179fcdd02c5b6005edb`, subsequently passes the complete
84/84 run in821.17s. Core217/GPU45/Performance39, denoise9/workflow17/cache9,
clarity9/workflow17/cache10 and independent sharpening14 have zero failures or
skips. The full original log SHA256 is
`465aeb319cff37139b64b885282a5e3d38cda7c3a2930d62a2cf51e84d3aa667`.
`validation-summary-08be052.json` freezes source/tree/clean status and Qt totals.
The exact-commit full normal-App denoise CPU and Metal probes also pass in
`exact-08be052-denoise-cpu.json` and `exact-08be052-denoise-gpu.json`. These are
local observations, not current three-platform/shipping acceptance. The later
artifact-delivery-only merge keeps this original tested commit distinct.
