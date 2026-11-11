# F4 M8A — QRhi clarity and normal-App signed-control workflow

This follow-up to M56 implements ordinary clarity previews as three QRhi
compute passes. The existing CPU algorithm, signed amount, source density,
stage ordering, default pixels and engine/profile identity are retained.
Platform/package acceptance for this follow-up remains pending.

## Compute and resident dependencies

A prepare pass converts the original decoded scene input, or the completed
linear denoise result, to Oklab plus alpha. A horizontal pass stores the
alpha-weighted relative lightness delta, associated weight and local visible
reference. A vertical pass applies the existing midtone correction while
retaining center chroma and alpha. Hidden RGB supplies no visible reference;
zero corrections retain the original linear input without a matrix round trip.
The CPU-generated Gaussian FP32 weights are shared through a208-byte uniform
buffer, including fractional edge support. Three RGBA32F textures reserve48
bytes per preview pixel, separately from denoise resources. This is accounted
GPU storage, not a process-RSS measurement; CPU brush ownership remains32B per
expanded spatial-stage pixel under the original3GiB budget.

Guide reuse depends on original source identity, input encoding/matrix rows and
denoise settings. Radius changes rebuild horizontal/final results; amount
changes rebuild only the final clarity result. WB, exposure, output space,
grain and sharpening retain completed spatial textures. Late denoise/detail
bindings, clarity-off source uploads and size transitions are covered.
Shader resources are explicitly reset with size changes. Diagnostics identify
actual `gpu-compute` or `cpu-reference-upload` producers, complete source keys
and parameter revisions before monitor ICC.

Tiny positive alpha with an active spatial stage retains the explicit complete
CPU policy (`clarity-near-zero-alpha` when clarity is active). Signed linear
ProPhoto to ProPhoto, existing Adobe-grain/HSL policies and active native brush
CPUPolicy remain. A GPU label alone is never counted as clarity computation.
The former CPU-only clarity label is therefore replaced by the bilingual
Clarity label; fallback reasons remain diagnostic facts.

## Actual main-window workflow

The owned307x211 RGBA64 linear-ProPhoto fixture includes noise, fractional alpha
and transparent pixels. The normal Main.qml/PhotoCanvas runs with actual QPA
pointer input and admitted native-window geometry. Its original denoise25/35,
grain17 and exposure edits remain while clarity is set positive then negative,
reset, undone/redone and saved/reopened through the project database. A later
exposure edit requires a completed spatial-cache hit. Four whole-window
captures record active, undo, redo and reset states; three stable admitted
frames precede input, with at most one contained fit on constrained displays.

The report freezes all four1024-bin histograms, clipping counts and all three
scope plots. The actual PNG16 export is decoded independently: all64,777 RGBA16
pixels and sRGB ICC match the CPU reference and frozen diagnostic preview.
The original diagnostic ZIP must report the current source/revision and the
actual denoise and clarity producers. Terminal phase16 is a phase identifier;
the recorded checkpoints are0..15. Physical device input, native float RAW and
whole-product acceptance remain explicitly false.

CPU/English and Metal/Chinese prepublication drivers both pass. Native elapsed
observations are4617ms and4305ms, against the unchanged60,000ms native and75s
outer limits. Export SHA-256 is
`5fa833f3e9c5d48cee69638917f7aa939ab4e3dc5eaca0680ea7726efa145f29`
for both. These builds are stamped dc7d547 with changed sources, not a clean
follow-up commit. Actual active and reset/undo captures are inspected separately.
Five Python evidence methods reject incomplete workflow, wrong labels, stale
frames, CPU upload mislabeled as compute, changed unrelated edits and occupied
namespaces. Seven actual argument/output refusals exit2 without changing owned
files. The original72 package modes retain their exact order/prefix; two new
CPU/GPU clarity modes extend the required package manifest to74. No74-mode
package pass is inferred from source checks.

## Local numerical and regression evidence

The first complete GPU run reports49 passing Qt observations, zero failures or
skips. The64 combined clarity comparisons distinguish44 actual compute cases
from20 explicit CPU policies;32 dark/fractional/alpha-hole and8 public-Sony crop
comparisons require actual compute. Metal maxima are6,2 and1 respectively,
against the unchanged40/65535 RGB limit with exact alpha. Resident dependency
checks and denormal-alpha complete-reference policies are additional cases.
The Sony crop is one public scene, not broad camera/ISO or photographic-quality
acceptance. Original RGBA64 RAW decoding is unchanged.

Ten of the first eleven related CTests pass. The remaining old performance
method assumes its worker is still pending after sleeping10ms; its `pending`
assertion fails once, then passes when reproduced alone. No renderer failure is
inferred from that ordering assumption. A performance-target-only compiled
ImagePipeline observation now pauses after an actual candidate-bearing brush
tile has completed, before publication. Cancellation is set while the worker
is held; all paths release it before assertions. The test requires observed
progress, a different worker thread, pending work, a null cancelled result,
unchanged source and a healthy independent next request. Core/App compile
without this observation. The corrected Performance39 and Core217 pass;
original failure and first reproduction remain retained. Product cancellation,
pixel math and timing limits are unchanged.

The initial GPU build also rejects an ambiguous unsigned JSON byte value;
explicit qint64 accounting corrects it. The first native CPU process passes but
its outer validator rejects a copied DENOISE label; validator/negative fixtures
are corrected to Clarity and both complete drivers pass. Those first failures
remain distinct from subsequent successes. A clean follow-up full84 run and
its own three-platform74-mode package workflow are still required.

Evidence is retained under
`/Users/jaysnail/.cache/jixellight/clarity-gpu-stage/`, including original GPU,
related-regression, performance-observation, native JSON/PNG/ZIP and argument
reports. Fresh exact-commit reports must use separate namespaces. Existing
CPU08be052 full84/84, CI367, and earlier denoise CI results remain separate
source identities.
