# F4 M71 — QRhi luminance range mask and Point Color precision

Luminance masking now executes in the existing color compute pass. Selection uses post-spatial, scene-linear ProPhoto Y before white balance or any exposure gain. It uses the unchanged CPU smooth shoulders, inversion and `exp2(EV * weight)`; the gain follows white balance/global exposure and precedes other local masks. Disabled and zero adjustments bypass the gain. Alpha is unchanged.

Existing slots 61/62 are reused. The 65-vec4/1040-byte layout, image planes, spatial passes, textures and memory budgets do not grow. Luminance edits preserve source uploads and every completed denoise/dehaze/texture/clarity stage. Export and CPU color math are unchanged.

## Numeric policies and truthful diagnostics

The CPU fallback solely for an active luminance mask is removed. Color range masks, fine Point Color ranges, signed ProPhoto output, tiny-alpha spatial processing, Adobe grain and existing backend/brush policies remain. Positive luminance-mask exposure is included in the existing D3D/OpenGL high-exposure perceptual policy, just as linear/radial/vignette exposure already was. Native brush admission retains its explicit CPU policy because that separate kernel has not acquired these masks.

Accepted diagnostics label luminance `gpu-compute` only after actual compute; a complete CPU upload reports `cpu-reference`. The independent CPU diagnostic remains the reference. Main App GPU validation now requires actual compute, denoise reuse and the accepted source/revision. Chinese/English panel copy removes the superseded CPU-only statement.

## Point Color Windows failure and correction

CI384 at `97b5af6` retains a second Windows WARP failure. At the newly tested 5-degree hue boundary, the positive 180-degree shift measures maximum 78/65535, with five pixels above the unchanged 40 limit. The negative shift measures 22. This is comparison 66: first center, range 8, sRGB output. Pixel (17,22), input16 RGB 52180/32892/38271, becomes CPU 4023/65458/58994 versus GPU 3945/65458/59004.

The original 942,137-byte failed log SHA-256 is `a140133e32d1db03176954ae50065e30377ecc0315c1380769a57d5cc8d7bad4`. CI384 passes 91/92 and has no Windows/package acceptance. Its previous 1-degree failure and first 5-degree guard remain historical observations.

The shared precision region is conservatively widened to active hue radius below 45 degrees, alongside the existing chroma/lightness thresholds below .003. This encloses both observed narrow-hue failures while preserving the ordinary 45-degree default compute path. Persisted state, CPU math, engine/profile compatibility and export stay unchanged. The same predicate drives the existing precision notice. The expanded 312-case matrix requires 240 exact CPU uploads and 72 compute comparisons, including 1/2/3/5/10/20/44.9-degree precision cases and the 45-degree compute boundary. Windows acceptance requires a fresh successful job at the final head.

## Scoped observations before freezing

The first luminance implementation passes all 76 Metal GPU methods in 26.331 s. Its 64 combined cases measure maximum 21; 192 lower/upper shoulder cases at .001/.003/.03/.2 feather widths measure maximum 6; resident compute edits maximum 1; eight Sony crop comparisons maximum 8. Signed/HDR input, negative Y, alpha holes and all four output spaces are included. All original RGB, exact alpha and 1024-bin histogram gates remain.

After the 45-degree Point Color guard, the complete 76-method GPU suite passes in 26.040 s and all 19 controller/workflow methods pass in 19.449 s. The 312-case matrix measures maximum 10 on Metal. That local result does not replace the failed Windows observation. The final 77-method GPU suite passes in 25.985 s, including 16 high-exposure composition comparisons with Metal maximum 2. All 217 Python methods pass. Actual Main App luminance CPU/Metal-compute flows pass in 4640/4115 ms with denoise cache deltas 2/5, eight real QPA events, complete staging/state/history/reopen/scopes/plots/windows and frozen diagnostics. Both retain identical full PNG16/ICC bytes (SHA-256 0c4fb818681e565bb3ea17cc27a38ecf1063ca9e5a05e8b6328d7ff790b10a08). Existing Point Color Metal compute passes in 3860 ms (cache 5, unchanged dbc57546 PNG); color-mask Metal display passes in 4158 ms (cache 0, unchanged f63434b4 PNG). Original reports retain complete hashes and proofs.

Original first/second results and every failed cloud log remain outside Git in the stage evidence directories. No deadline, memory admission or parity limit is relaxed.

## Remaining acceptance gates

At implementation time, frozen clean 95, all 90 complete Qt-result groups, exact-head native luminance/color/Point Color and whole-Sony pressure export, all Python evidence checks, three-platform CI and both 84-context deployed packages remain required. This stage does not establish whole-product completion, physical-tablet acceptance, broad camera/ISO coverage or native FP32 RAW decoding.
