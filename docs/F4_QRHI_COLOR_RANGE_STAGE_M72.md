# F4 M72 — QRhi color range masks

Color selection now runs in the existing QRhi color pass. It uses immutable post-spatial ProPhoto input transformed to working linear sRGB/Oklab before white balance, global exposure or any local gain. The shared hue/chroma/lightness weight, neutral confidence, circular hue distance and inversion match the unchanged CPU selector. The color gain is applied before the luminance/linear/radial masks and vignette. Disabled/zero/unselected adjustments bypass it; alpha is unchanged.

The existing Point Color selection-weight expression is factored into a shader helper shared by both tools, preserving operation order and its complete regression matrix. Color mask slots63/64 and the65-vec4/1040-byte layout are unchanged. No image plane, texture, spatial pass, readback, native brush kernel, CPU export or profile/engine version changes.

## Precision and source policies

Color selection shares Point Color's below45-degree hue and below.003 chroma/lightness CPU precision region. This is a conservative policy based on the actual narrow Point Color FP32 failures retained in M69/M71. The same packed-plan predicate drives diagnostics and the new bilingual precision notice; it is derived rather than persisted. Accepted frame diagnostics attribute both masks to their actual producer. Signed ProPhoto, tiny-alpha spatial, Adobe grain, backend color and brush policies remain; positive color-mask EV composes with the existing high-exposure perceptual fallback. Native brush admission still explicitly refuses either range mask.

The actual App harness and Python evidence verifier now require real GPU color-mask computation and completed denoise reuse in GPU mode. They retain every source/picker/revision/history/scope/window/export proof and require a fresh evidence namespace. A CPU reference upload cannot pass as GPU compute.

## Scoped checks before freezing

All 82 Metal GPU methods pass in 29.857 s with zero failures/skips/blacklists. The combined 64 cases, with both range masks and every spatial stage active, distinguish 44 compute/20 inherited CPU policies and measure maximum 28. The 624 center/range/EV/inversion/output cases distinguish 144 compute/480 exact precision uploads, maximum 28. Resident compute edits measure maximum 1 while preserving all 15 upstream counters through each mask's own and inherited CPU transitions; eight Sony crop comparisons maximum 5; 16 bright-perceptual policy comparisons maximum 2.

The color workflow passes all 18 methods in 22.312 s, including derived precision boundaries and neutral/disabled bypass. The 24MP FP32 Paint/Erase + all spatial stages + Point Color + both masks + sharpen check measures 13,493 ms, reservation/peak 2,672,225,792 bytes, three exact ROIs, unchanged input and zero remaining charges under the existing 3GiB/60s gates. All 217 Python methods and complete shader baking pass. Original RGB40/65535, exact alpha, histogram, memory and operation limits remain unchanged.

Actual Main App color CPU/Metal compute workflows pass in 4761/4323 ms with real denoise reuse deltas 2/5. They retain all nine QPA inputs, fixed source-Oklab selection, complete staging/state/history/reopen/scopes/plots/four windows and frozen diagnostics. Both complete PNG16/ICC files retain SHA-256 f63434b464d9f86c5bb50fc270d5014bfe564d296a51e28375cd1325ebd14fb2. Existing luminance and Point Color Metal compute flows pass in 3976/3988 ms (cache 5 each), with their prior full export hashes. Native GPU color-mask windows are visually reviewed.

These pre-freeze source-content results carry the parent build stamp. Exact-head acceptance is separate.

## Frozen gates still required

Frozen clean 95, all 90 Qt-result groups, exact-head native/whole-Sony export, three-platform CI and both 84-context deployed packages remain independent gates. This does not establish whole-product or platform acceptance. Native FP32 RAW decode and physical-device/photo-quality evidence remain separate work.

The CPU color parent565829c passes frozen clean95 and eight native flows. QRhi luminance parent3e5eb93 also passes frozen clean95/95 in880.71s, all90 groups (GPU77), and eight exact-head native flows, including actual luminance compute/cache5 and unchanged whole-Sony pixel/ICC proofs. CI384 and its integrated CI385 retain the actual Windows5-degree Point Color failure; only the new45-degree guard in3e5eb93 and this descendant is under fresh Windows validation. No failed result is discarded or counted as accepted.
