# F4 M10A — QRhi dehaze and resident context

Ordinary dehaze previews now execute on QRhi. Positive amounts use four passes: intensity/alpha/dark preparation, horizontal minimum, vertical transmission and alpha-weighted refinement/inversion. Negative amounts use only the final uniform-veil pass. Four lazy RGBA32F textures reserve64 bytes per preview pixel; exact CPU-generated refinement weights occupy32 additional bytes. Denoise, texture and clarity retain their own resources.

The CPU algorithm and full-frame airlight estimator are unchanged. The shared refinement-weight function uses the same FP32 operations and padding for its32-byte GPU uniform. Eight CLI comparisons cover both signs and four output spaces with denoise/texture/clarity; every PNG and ICC byte matches the CPU parent. The independent kernel's nine observations also pass after the refactor. Engine/profile identity and RAW decoding remain unchanged.

## Dependencies and source order

The dehaze input key includes source identity/encoding/input rows, denoise settings and frozen full-corrected-frame airlight. Positive guide/coarse/output validity is tracked separately. Amount changes rebuild the final output; density changes rebuild neighborhoods/output. Downstream WB/exposure/output changes reuse every dehaze stage.

A negative veil does not dispatch guide or neighborhood work. Returning to a positive amount can reuse earlier positive helpers when input is unchanged. A different source/encoding/denoise/airlight during a negative veil invalidates those retained helpers. Twenty resident observations cover that transition, source uploads made with dehaze off, later activation, custom input rows, resizing and downstream texture/clarity invalidation.

Texture and clarity have bindings for dehazed input, including late denoise/detail/texture allocations. The complete source order remains denoise → dehaze → texture → clarity → WB/exposure. The packed58-slot/928-byte plan stays unchanged; only the temporary GPU uniform copy carries effective radii and prelinear-input flags.

Existing numerical/brush policies remain explicit. Tiny/subnormal-alpha dehaze uses `dehaze-near-zero-alpha`; signed linear ProPhoto and Adobe-grain/color policies retain their original meanings. Native brush dehaze remains CPUPolicy. Diagnostics describe actual GPU compute versus complete CPU upload, including the frozen source and revision. The ordinary UI now displays generic Dehaze because its normal path computes the stage.

## Rejected resize correction

Extending the prior accepted-frame test reproduces two failures: a resized source with an invalid LUT atlas or invalid floating-point sample resets the accepted revision to zero. The original five-pass/two-failure Qt report and source hashes remain retained separately.

Candidate bounds, spatial context, floating-point source facts and new LUT atlas are now checked before initialization can replace size-dependent resources. Existing source/LUT counters and producer/revision still publish only with accepted commands. All five rejection rows then pass, including explicit readback of the prior revision and equality of its full RGBA32F pixel fingerprint. Fatal-error behavior and resource/test limits remain unchanged.

## Current local evidence

The complete GPU group passes64 Qt observations, zero failures/skips. The new64 combined source-format/encoding/output/sign/density cases distinguish44 actual compute and20 explicit CPU policies. Another32 dark/fractional/alpha-hole and eight public-Sony crop comparisons require compute. Metal maximum channel differences are11/1/8, respectively, below the unchanged40/65535 bound; alpha remains exact. The20 resident comparisons have maximum4.

Separate64 subnormal-alpha/brush combinations require byte-exact complete CPU uploads and exact four-channel histograms, with denoise/texture/clarity/detail/grain enabled. Native brush checks preserve CPUPolicy for both signs and formats. The five rejected-frame rows pass after correction; the full64 group also passes with the new fingerprint assertions.

Actual Main.qml/PhotoCanvas CPU/English and Metal/Chinese dehaze workflows pass gestures for both signs, independent Reset, whole-state Undo/Redo, durable reopen, all four1024-bin histograms/three plots and actual PNG16 export. Precommit native operations take4,877/4,126ms and observe1/2 actual stage hits. GPU diagnostics require real dehaze/denoise computation and no numerical CPU policy. All64,777 RGBA16 pixels and ICC bytes are independently checked; both exports have SHA256a5f1af44101c52ff32d2d2f58da8ab7b1e789dbd50007abafb16062a4d87a7f1. Six adversarial evidence methods reject CPU uploads called compute, invalid airlight, stale identities and incomplete operations. The unaltered full-window Metal capture is inspected. Seven actual invalid-argument/occupied-output invocations preserve owned files. Both original clarity and texture Metal main-App workflows pass after the new bindings and source-validation order.

The CPU parent d449920 separately passes clean90/90 in844.04s, all85 Qt-result groups without failures/skips, and exact-commit CPU/Metal-display native workflows. Its complete log SHA256 is e5a119e44eb7ef66671ad8e6702f5ba2a5368131a80d15d48e9cffbbad0bd9e2. That identity and its CPU-upload behavior remain separate from this QRhi follow-up.

Current clean90/platform/deployed78-mode acceptance remains required. All original package calls and deadlines remain. The one Sony scene/crop is not broad sky/skin/ISO dehazing quality, camera coverage or native floating-point RAW acceptance. Whole F0–F6 development remains incomplete.
