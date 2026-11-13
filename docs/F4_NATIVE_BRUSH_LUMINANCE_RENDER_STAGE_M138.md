# F4 M138: luminance selection in committed native brush colour

The test-only 8ecddca baseline builds in 27.95s and runs on actual Metal/Apple
M2. Its first source/output case is refused with `CPUPolicy` before dispatch:
2 pass / 1 failure / 0 skips / 0 blacklists, 137ms. All 18 original evidence
files (4,540,159 bytes), source copies verified against Git blobs, the binary
and build graphs remain separate and failed. This establishes the existing
feature limitation, not a previously supported product regression. The raw
log SHA256 is `e6db0962a5cbebb58b3b8f969801cd5c644fa8bcd09a12e30b2e0d91e93657b6`.

The native colour shader now declares the existing full 65-slot/1040-byte
processing plan. Its luminance selection uses the same expression and order
as the CPU and ordinary GPU routes: post-input-conversion scene-linear
ProPhoto Y, before white balance, camera/global exposure, linear/radial masks,
brush EV and vignette. Inversion, zero-width ranges and feather shoulders use
the accepted parameters without clipping source RGB. The gain is applied
after white balance/global exposure and before the other local masks.

Native admission no longer refuses an otherwise supported luminance mask.
The restricted D3D/OpenGL bright-perceptual bound also includes positive
luminance-mask EV, retaining the common CPU fallback when combined positive
exposures reach its existing threshold. Point Color, colour masks, spatial
stages, negative-ProPhoto input, Adobe grain, hard-edge brushes and every
remaining policy stay explicit. The inherited film-grain method now requires
luminance admission while keeping its other refusal and numerical checks.

No extra source upload, texture, dispatch, plane, readback or budget increase
is introduced: the existing native colour dispatch reads the already uploaded
plan. Ordinary GPU/CPU colour processing, exports, schema, public APIs and
default RAW decode stay unchanged. This extends committed soft-brush preview;
it does not establish per-packet native live-brush rendering, calibrated float
RAW, physical input, full F0–F6 completion or support for other refused effects.

Three new mandatory methods require actual native coverage/colour dispatches,
independent CPU output, maximum RGB16 error40, exact float alpha, exact actual
pixel histograms and complete accounted lease release. Expected GPU results
are85; CTest104/all98/sixteen guarded workflow runtimes stay the same. A fresh
physical second build, focused tests, mandatory real-Sony/GPU preflight,
full104, all33, own three-platform CI and both installed88 packages remain
pending at this checkpoint.
