# F4 M11A — QRhi Point Color and explicit precision policy

This stage adds Point Color to the existing QRhi color pass after HSL/Sony styling/B&W/grading and before output gamut/transfer/LUT/grain/detail. It keeps the CPU reference, working-space picker, schema, 30-parameter registry, 61 vec4/976-byte layout, Sony compatibility string, existing source facts, cache budgets and all operation/parity limits. No image plane, spatial pass, new GPU texture or per-pixel allocation is added. Export still uses the unchanged CPU implementation.

## Computation and policies

The shader uses the CPU's three smooth range weights, circular hue distance, low-chroma hue confidence, weighted hue rotation, chroma factor and lightness delta. Disabled, zero and unselected values bypass the extra Lab round trip. The source alpha remains unchanged. Point Color edits retain the source upload and every completed denoise/dehaze/texture/clarity stage; only the final color work changes.

A retained failure defines a precision exception. The first narrow-range probe measured60/65535 (6 pixels above40 in the first failing signed case). An expanded unchanged-math72-case probe retained four failed comparisons, maximum60; its ordinary .003 lightness/.02 chroma set had maximum10, while broader sets passed. Original log SHA-256 is4434ae6fad77e324ec25f96da02eeb59411c6314cc001b11c5da997ea8f1ed8d. Source fingerprints and the first failure remain external. The original40 bound is unchanged.

Active Point Color with a chroma or lightness radius below .003 uses the complete CPU reference upload and reports `point-color-narrow-range`. The threshold is .3% in the panel's range units. The single shared packed-plan predicate drives both GPU policy and the bilingual precision notice; it is derived, not persisted. A one-degree hue range can still compute when chroma/lightness ranges permit it. Source-dependent tiny-alpha spatial, signed ProPhoto, Adobe-grain, backend-specific color and existing brush policies remain intact. Native-brush admission keeps the prior explicit Point Color CPU policy.

Accepted diagnostic frames report `point_color:gpu-compute` only after real compute, or `cpu-reference` for the accepted complete upload. Producer/source/revision facts retain the existing refusal/resize safety rules. The CPU diagnostic boundary remains the reference rather than impersonating GPU observations.

## Scoped local checks before freezing this stage

- Full Metal GPU suite:70 results, zero failures/skips/blacklists,22.754s. This includes all earlier color, spatial, source-refusal, display/ICC and native-brush checks.
- Combined64 comparisons distinguish44 real compute and20 unchanged CPU policies, max5;32 dark/fractional-density/alpha-hole comparisons max2; eight existing Sony crop compute comparisons max15. Alpha is exact and all GPU histogram bins are checked against the actual GPU pixels.
- Expanded range/seam/neutral matrix:144 comparisons,72 real compute and72 exact CPU-policy comparisons, max24 across compute cases. It includes the minimum and threshold ranges, separately narrow chroma/lightness, and one-degree hue with broad ranges. CPU cases additionally require every uploaded RGBA16 channel to equal the complete reference and require the explicit policy/producer label.
- Twenty-one resident comparisons, max9, preserve all15 source/spatial dispatch counters through amount/selection/range/enable changes and GPU→fine-range CPU→GPU transitions. This checks reusable upstream work rather than labeling the final pixel pass a Point Color cache.
- Actual Main App CPU English/Metal Chinese workflows pass in4835/4025ms before the source freeze. They require original QPA picker/slider/reset events, exact working selection, whole snapshots/history/reopen/scopes/plots/complete admitted window captures, CPU reference ZIP and every exported PNG16/ICC byte. Cache deltas2/5 require real completed CPU/GPU denoise reuse. GPU diagnostics require actual Point Color and denoise computation, not an uploaded reference. Both exports remain byte-identical to CPU parent341c25f, SHA-256 41dce1047c75aae3cb8b06e2cd4055798d6b4785a19eb7a4170a3a68624aef15.
- Existing full-App dehaze, texture and clarity Metal workflows also pass unchanged.
- All203 Python methods pass, including adversarial producer, policy, selection, source/revision, scope, history and occupied-output evidence checks. The existing window/DPR/clip/owner/byte-verification guards are preserved.

The CPU parent341c25f independently passes clean92/92 in843.60s, all87 Qt-result groups without skips/failures, and exact-head CPU/Metal-upload native workflows. That result does not replace this stage's required exact clean92 run and exact-head native reruns. Platform CI, all80 contexts in both deployed packages and mandatory original artifact-delivery audits are still required at this source freeze. Local Metal evidence is not Windows/OpenGL acceptance or whole-product completion.

## Cloud records carried forward

CPU texture CI375 is successful after an unchanged-source failed-only macOS retry:87/87 in1670.61s and all76 package contexts. Its first two120s raster timeouts remain recorded; no root cause is inferred from a successful rerun. CPU dehaze d449920/CI377 completes Linux90/90 in1326.75s, macOS90/90 in1935.00s and Windows90/90 in2241.68s, both78-context packages and all primary delivery audits.

QRhi-dehaze CI378 completes Linux90/90 in829.72s and macOS90/90 in1546.60s plus78 package contexts; Windows packaging remains pending in the current observation. PNG-verifier QA CI379 completes Linux90/90 in1431.59s, but macOS90/90 in1610.43s is followed by a retained independent pressure-export verifier timeout: native export98.534s leaves17.454s of the unchanged120s export/reference slice. That harness does not use the accelerated PNG row decoder; its failure is not claimed fixed by M63 or this feature.

External evidence lives under `~/.cache/jixellight/point-color-gpu-stage` and original CI logs remain in their stage directories. No merge, release, physical tablet acceptance, broad photo-quality acceptance or native FP32 RAW claim is made.
