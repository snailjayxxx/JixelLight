# F4 M9A — five-pass QRhi texture and resident stage reuse

Ordinary texture previews now execute on QRhi. The existing CPU2/6source-pixel
Oklab lightness-band algorithm, parameter interpretation, default pixels and
engine/profile identity are retained. The explicit CPU-only card marker is
replaced by the bilingual Texture label. Native-brush and numeric CPU policies
remain identifiable in diagnostics.

## Compute, storage and dependencies

Five passes prepare the Oklab/alpha guide, calculate the small horizontal and
vertical neighborhood, calculate the large horizontal neighborhood and apply
the final band correction. The two horizontal passes reuse one texture. A
small-band relative result survives for the final amount calculation. CPU-
generated FP32 Gaussian weights are packed into96bytes of uniforms; fractional
support and operation order match the CPU reference. Four RGBA32F textures
reserve64bytes per preview pixel, separately from denoise32B/pixel and clarity
48B/pixel. The CPU texture stage still reserves36B per expanded brush pixel;
these GPU figures are resource accounting, not process RSS.

Guide reuse depends on actual original source identity, encoding/input matrix
rows and completed denoise settings. Density changes rebuild both neighborhoods
and final output; amount changes rebuild only the final texture result.
WB/exposure/output/grain/sharpen retain completed texture stages. Texture edits
also invalidate downstream clarity's guide. Late denoise, detail and textured-
clarity bindings, feature off/on, source uploads while texture is disabled,
size transitions, encoding and matrix-row changes are explicitly checked.
The stage order is denoise -> texture -> clarity -> color/detail. Source resize
releases all associated resources and validity flags. Alpha/hidden-color
isolation and zero-correction input retention match the CPU implementation.

All active spatial stages retain the existing tiny-positive-alpha floor policy;
texture reports `texture-near-zero-alpha` when it selects complete CPU reference.
Signed linear ProPhoto to ProPhoto, Adobe-grain/HSL and brush policies remain.
Normal compute reports `spatial_texture:gpu-compute`, actual source/revision and
`numeric_source_policy:none` before monitor ICC. Native brush texture still
requires CPUPolicy; no new native-brush computation is claimed.

## Actual computation and main-window evidence

The first complete GPU run passes54 Qt observations without failures/skips.
After two additional rejected-frame data rows are added and fixed below, the
complete GPU run passes56 in14,315ms without failures/skips. The new64 combined
texture+denoise+clarity/detail/grain comparisons distinguish44 actual compute
from20 explicit CPU policies. Thirty-two dark/fractional/alpha-hole and8 public-
Sony crop comparisons require compute. Metal maxima are7,1 and1/65535, below
the unchanged40/65535 bound with exact alpha. Sixteen resident dependency
observations have maximum2. Sixty-four separate complete-reference cases cover
FP32 subnormal alpha and integer brush policies, with exact upload pixels and
histograms; native admission checks both signs/formats separately.

All15 related original/new CTests pass in130.15s, including Core217,
Performance39, texture kernel10/workflow17/cache10, clarity and denoise groups,
CLI/export safety, independent sharpen and grain. The original actual clarity
Metal main-App workflow passes with texture disabled. These are prepublication
5662c3e-plus-source observations; the clean CPU parent5662c3e separately passes
87/87 in789.01s and both actual texture CPU/Metal-display contexts.

The normal Main.qml/PhotoCanvas CPU/English and Metal/Chinese texture workflows
pass positive/negative QPA adjustment, independent reset, whole-state history,
project reopen, full1024-bin histograms/three plots and actual PNG16 export.
CPU records one stage hit; Metal records two actual texture-filter hits. Both
now require positive reuse. The GPU diagnostic must identify actual texture
and denoise computation, current source/revision, finite complete frames and
no monitor ICC. CPU upload mislabeled as compute, stale frames and zero reuse
are rejected by the six Python evidence methods. The original GPU display's
uncached CPU policy remains a historical M59 observation, not this result.

First native elapsed observations are4735/4039ms under the unchanged60,000ms
native/75s process limits. Every64,777 exported RGBA16 pixel and sRGB ICC match
the independent reference and original frozen diagnostic preview. Export
SHA256 remains
`bb0aa0524cca8c7525848806a1ca955bac45a650e2e7584cbd3ff357a3c6ff98`.
Actual whole-window active captures are inspected. The subsequent Metal driver
also passes after the provenance correction; exact source-commit runs use
separate fresh namespaces. GPU diagnostic readback is a full-frame fingerprint;
the numerical40-bound proof comes from the separate per-pixel comparisons.
The original76 package modes remain; the texture GPU context now requires
actual compute and resident reuse. The preceding74-mode prefix/order remains.

## Rejected requests preserve accepted-frame provenance

Review exposes two real failures, preserved before correction. A source with
subnormal alpha first produces a complete CPU-upload frame. A later rejected
positive-alpha source with invalid texture settings changes the stored CPU/
GPU producer before rendering; an explicit capture of the accepted old revision
then falsely reports `gpu-compute`. With an invalid LUT atlas, an unsubmitted
source update also increments the upload count from1to2. Neither request
renders its proposed new revision.

Numeric source facts and the execution choice are now local until the output's
resource update/compute commands have been accepted. Source identity/facts,
source and LUT upload counters/keys, producer and revision publish together.
Rejected requests retain the accepted frame's source key, revision, CPU producer
and explicit numeric policy. Existing fatal-error handling remains. Two actual
Metal data rows reproduce the original producer/count failures and then pass;
callback capture proves the previously accepted source/producer remain. The
first failed correction-script guard and its unchanged failing observations
are retained separately; no failed run is adopted as a pass.

The56-observation full GPU rerun and the actual Metal texture driver pass after
this correction. CPU math, source read-only behavior,40/65535, alpha,1024 bins,
32Mi-pixel/3GiB brush ownership and all native/job deadlines are unchanged.
A clean current87-test run, three-platform compute/UI and both76-mode packages
remain required. Shader baking covers GLSL430/310es, HLSL50 and MSL12; actual
local compute evidence is Metal on M2/four CPU threads. One Sony scene is not
broad camera/ISO/skin quality, physical input, native float RAW or whole-product
acceptance. Original logs, before/after failures, JSON/PNG/ZIP and derived
receipts are retained under
`/Users/jaysnail/.cache/jixellight/texture-gpu-stage/`.
