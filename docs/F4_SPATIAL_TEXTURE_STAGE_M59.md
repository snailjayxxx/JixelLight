# F4 M9 — independent scene-linear texture, CPU reference

Texture is a signed control separate from independent clarity, denoise,
sharpening and Sony Look parameters. Positive amounts strengthen medium-scale
lightness detail; negative amounts soften it. The default0 retains the existing
pixel path. The bilingual card explicitly marks CPU processing. High positive
amounts may also emphasize noise; this is not a denoiser.

## Algorithm, order and ownership

The independent implementation follows denoise and precedes clarity,
WB/exposure, masks, tone, look, output encoding, grain and sharpening. It uses
two alpha-associated Gaussian Oklab-lightness neighborhoods with2/6source-pixel
radii (sigma is half the radius), scaled by prepared/full geometry density.
Fractional support uses the same partial edge convention as clarity. The
correction is

`0.8 * amount01 * (meanSmallL - meanLargeL) * 4 * clamp(L,0,1) * (1-clamp(L,0,1))`.

Center chroma and alpha are retained; RGB is not clipped. Relative sums and
local visible references exclude hidden RGB and preserve constant visible
fields exactly through irregular alpha holes. The tests independently evaluate
both full two-dimensional neighborhoods in double precision. A frequency test
requires stronger response to the medium band than to fine/broad variation.
A serial/parallel comparison, HDR/signed fields, fractional radii, cancellation
and invalid transforms are additional checks.

Peak stage storage is36bytes per expanded pixel: owned RGBA32F16B, guideL4B,
horizontal associated record12B and the small-band relative result4B. Two scales
reuse the horizontal buffer. Denoise, texture and clarity run sequentially;
texture-active requests therefore reserve36B, while existing denoise/clarity
requests retain32B. Their halos sum before the existing detail halo. Frozen
original-frame density is validated and retained in admitted brush requests.
The32Mi-pixel/3GiB brush limits and60,000ms operation limit are unchanged.

Completed16B/pixel pre-tone results and8B/pixel encoded results share the same
64MiB rendered-preview LRU. The texture key includes actual source identity,
format, encoding/input matrix rows, denoise settings/density, texture amount/
radii and clarity amount/radius. Downstream color, output, grain and sharpen
reuse the complete stage. Old denoise-only/clarity-only keys remain unchanged.
Brush requests bypass stage retention and retain their owning lease accounting.
A cache-test-only observation cancels after a real complete FP32 stage and
before color/publication; no cancelled stage or partial result enters the LRU.
Core/App compile without this observation.

## State and visible workflow

Optional canonical `texture:{schema:1,amount:-100..100}` is omitted at0; unknown,
missing, zero-redundant, future-schema, wrong-type and nonfinite/out-of-range
sections are refused. Old snapshots restore0. The shared scalar registry grows
22to23 with `textureAmount`/`texture.reset`;56vec4 plans occupy896B with texture
in slot55. The engine/profile identity remains
`jixellight-linear-v5-base2-look4` and RAW decoding remains RGBA64.

Projects, virtual copies, coalesced history/Undo/Redo, presets/XMP, detail-only
transfer, CLI, scopes and PNG16/TIFF16/WebP8 exports carry the independent state.
Invalid photo/history/copy writes are rejected before SQL work and malformed
catalog reads retain the current writer. Reset retains unrelated edits. Actual
compiled English/Chinese panels exercise both signs, Reset and Undo at300/320/
350px widths. Sony Look clarity is retained under its original separate key.

Texture currently selects a complete CPU reference in ordinary QRhi previews
and CPUPolicy for native brushes. Diagnostics explicitly report
`spatial_texture:cpu-reference`, producer `cpu-reference-upload` and
`texture-cpu-stage`. The existing GPU-display CPU fallback does not reuse a
texture stage cache; its zero-hit observation is explicit and is not counted
as GPU computation. CPU previews do reuse the shared LRU. Actual QRhi texture
computation/cache reuse remains follow-up work.

## Prepublication observations

All observations here use fb00101 plus the changed texture sources, not a clean
texture commit. The clean parent fb00101 separately passes84/84 in783.60s and
its own CPU/Metal clarity App checks; those are separate source identities.

- New kernel10/workflow17/cache10 pass with no failures/skips. The first three
  CTests complete in24.79s. Complete GPU50 passes with zero failures/skips;
 64 new integer/FP32, two-encoding/four-output texture comparisons require byte-
  exact CPU upload pixels, complete histograms and the explicit producer/policy.
  Native admission separately refuses both signs on both source formats.
- Core217, Performance39 and ten other original related CTests pass. The first
  CLI safety run rejects its old hardcoded22-parameter expectation; it is
  corrected to the strict new23 count and all original/new export/type/source
  guards pass in11.34s. The original failed report remains distinct.
- One769x513 M2/four-thread cache sample: three warm color edits14.8622ms versus
 48.7855ms fresh processing, identical pixels,18,940,928 charged bytes. This is
  one observation, not a general performance promise.
- A24MP FP32 Paint/Erase + denoise + texture + clarity + Sony detail/sharpen/grain
  observation takes12,624ms and reserves/peaks at2,672,225,792 accounted bytes.
  Whole/edge/panned ROIs match, the source hash is unchanged and leases return
  to0 under the original3GiB/60s gates. This is accounting, not measured RSS.
- One public Sony07 ISO1600 RGBA64 crop1600x1000 checks all four output spaces,
  three uneven/edge ROIs, alpha, histograms and unchanged source SHA. Saved
 384x384 before/after crops are inspected. This is not broad camera/ISO/skin
  or blind photographic-quality acceptance.

The normal Main.qml/PhotoCanvas owned307x211 fixture runs actual QPA positive/
negative pointer changes, Reset/whole-state Undo/Redo, durable project reopen,
all four1024-bin histograms/three plots and actual PNG16 export. CPU/English and
Metal-display/Chinese drivers both pass. The64,777 exported RGBA16 pixels and
sRGB ICC independently match the reference and frozen diagnostic preview;
original diagnostic ZIP provenance identifies the actual CPU stage. Native
elapsed times are4655/4170ms against the original60,000ms/75s limits. Export
SHA256 for both is
`bb0aa0524cca8c7525848806a1ca955bac45a650e2e7584cbd3ff357a3c6ff98`.
CPU records one cache hit; GPU display records0 and explicitly says uncached
complete CPU reference upload. Terminal phase16 and checkpoints0..15 retain
their original meaning. Six Python evidence methods and seven actual argument/
output refusals pass. A first copied rejection fixture for zero GPU cache hits
is corrected to the explicit new CPU-versus-uncached-display contract; CPU0,
false compute claims and altered cache policy are rejected. The first failure
is retained. Original denoise/clarity validators and their requirements remain. Their CPU and Metal normal-App workflows also pass after the new card is added (four separate original-context reports).

Source CI adds both normal-App contexts and retains generated evidence. Two
package contexts append after the byte-equivalent original74-mode prefix, for
76; all original product steps, upload paths, operation/job limits and strict
artifact-delivery gate remain. A clean texture87-test full run and its own
three-platform/76-mode packages are still required. Texture GPU compute,
physical-device input, native float RAW and whole-product completion are not
claimed. Logs/JSON/PNG/ZIP evidence is under
`/Users/jaysnail/.cache/jixellight/texture-stage/`; later checks use fresh
namespaces.
