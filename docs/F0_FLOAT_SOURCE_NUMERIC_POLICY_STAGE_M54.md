# F0 M9 explicit float-source numerical policies

Extended source-boundary checks expose two real failures beyond the original
RGB40/65535 limit. A positive subnormal source alpha (`1.4013e-45`) makes the
Metal denoise result diverge from the CPU guide by up to2769/65535, including
hidden RGB at an alpha that quantizes to0. Separately, a signed linear scene
with magnitude4 reaches67/65535 at a ProPhoto gamut boundary; magnitude256
reaches43. The latter also occurs with denoise disabled and is an existing
color-path numerical region. Original failed reports remain retained, including
the first2758 failure and the later complete boundary collection.

Source validation can now return immutable numerical facts alongside its
unchanged validity result. It records positive alpha below1e-18 using its
actual FP32 bits and whether any RGB channel is negative. Facts are published
only for a complete valid source, collected once per new GPU upload or native
canonical capture. Parameter edits reuse them. Invalid/cancelled sources do
not publish partial facts; RGB bounds and alpha validity are unchanged.

Active denoise with near-zero positive alpha uses the complete CPU reference.
The floor also stays safely above the CPU kernel's existing1e-20 normalization
epsilon. Signed Linear ProPhoto input targeting ProPhoto output uses CPU
reference upload with denoise either on or off. Ordinary nonnegative camera
sources and the other output spaces retain their existing execution policy.
Native brush admission explicitly returns `CPUPolicy` for the signed-ProPhoto
case. The CPU algorithm, source alpha, output pixels, engine identifier,
profile compatibility,32Mi-pixel/3GiB and time limits are not changed.

Working-frame diagnostics record `numeric_source_policy` as
`denoise-near-zero-alpha`, `signed-linear-prophoto`, an existing policy or
`none`, alongside the actual compute/upload producer. Tests require exact
RGBA16 bytes for the new CPU routes; no CPU upload is counted as denoise GPU
compute. Sources uploaded while denoise is off still receive correct facts.

The complete local GPU suite passes44 Qt observations, zero failures/skips,
including72 added alpha/range comparisons, explicit producers, source-fact
validation/reset, source-upload reuse and signed/unsigned native admission.
The76 combined denoise cases now comprise48 mandatory compute and28 explicit
CPU-policy cases. Pure noisy/dark and real Sony cases retain their separate
policy assertions; the original40 bound and exact alpha remain. Existing
signed-HDR ProPhoto tests now require the new CPU route rather than pretending
it is compute. The first full-suite41/44 result contains three stale policy
expectations; its numerical comparisons passed and its failure log is retained.

Six related CTests pass in55.96s: Core217, Performance39, brush GPU admission,
brush GPU HDR, denoise workflow17 and CPU denoise-cache9. Every reported Qt
observation has zero failures/skips. These are scoped checks using the
prepublicatione92d0a0 identity plus changed sources, not an exact-head full81
or three-platform/package acceptance claim. The correction is added to PR18;
its earlier CI369 is superseded and retained, while PR16/17 validation remains
independent. All affected current-head gates still have to complete.
