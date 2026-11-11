# F4 M7 CPU spatial noise reduction

Develop adds independently adjustable Lightness and Color noise reduction
(0–100), an isolated Reset, and Chinese/English labels. The section explicitly
says CPU. Preview, full-resolution statistics, exports, gesture-coalesced
Undo/Redo, project/version history, virtual copies, presets, XMP and selective
`detail` transfer share the same state. The scalar registry grows from19 to21
with `denoiseLuma`, `denoiseChroma` and `denoise.reset`. Numeric commands retain
the existing clamp policy; malformed types are refused.

This first stage is a small, fixed-neighborhood joint spatial/range bilateral
filter. It independently blends Oklab L and a/b, then returns to scene-linear
ProPhoto before user WB/exposure. Range distances use all three guide channels,
so equal-lightness color boundaries also limit smoothing. The neighborhood is
2 source pixels after geometry, scaled by actual prepared/full-frame density.
Fractional edge weights avoid integer-radius steps. Gaussian spatial sigma is
0.6 times the effective radius; range sigmas are0.04 for L and0.06 for a/b.
These are the application's initial fixed parameters, not a measured sensor
noise model or a learned denoiser.

The general bilateral idea follows [Tomasi and Manduchi's1998 paper](https://users.cs.duke.edu/~tomasi/papers/tomasi/tomasiIccv98.pdf).
This is an independent implementation with this application's Oklab guide,
alpha weighting, preview-density semantics and cancellation/ownership rules;
it does not reproduce the paper's implementation or claim its quality results.

Two temporary FP32 planes hold linear RGB and an immutable perceptual guide.
The linear plane is reused for output, costing32 additional bytes per expanded
pixel in the shared owning brush reservation. Center-relative weighted deltas
preserve flat fields exactly. Neighbor weights include source alpha; hidden
colors do not contaminate visible neighbors, transparent centers remain
unchanged, and alpha is never filtered. Finite signed RGB and values above1
are retained at the new scene boundary. Camera RAW still decodes through the
existing RGBA64 LibRaw path; this is not native floating-point RAW development.

Spatial halo is the sum of denoise support and the downstream Sony/independent
sharpen support. Regions,128-row strips, exact statistics and exports therefore
match completed-frame centers. Prepared admission validates and freezes the
source-density metadata before its memory estimate. The plan adds slot53,
growing to54 vec4/864 bytes. Default-off snapshots omit the optional strict
`denoise` schema1 section, and default pixel/legacy diagnostic arithmetic stays
unchanged. Packed plan/state keys distinguish the new parameters; Sony profile
engine compatibility remains `jixellight-linear-v5-base2-look4`.

Every active denoise plan currently renders the complete CPU reference. QRhi
uploads that result for display and1024-bin statistics; working-frame capture
reports `cpu-reference-upload`. Native brush admission returns `CPUPolicy` and
uses the complete CPU path rather than omitting the filter. No denoise GPU
compute is claimed. Monitor ICC remains presentation-only. A separate cached
denoise stage, adaptive sensor-noise model, larger-support filtering and a QRhi
implementation remain future work.

CTest79 `spatial-denoise` has9 passing targeted Qt observations, including an
independent double-precision neighborhood oracle, analytic alpha-weighted
impulses, component isolation, flat/hidden-color identity, fractional radii,
serial/parallel agreement, signed/HDR safety and atomic invalid/cancel refusal.
Synthetic guide noise variance falls to0.07254 of the input while a0.36
isolightness color edge remains above0.35. A separate full-pipeline neutral
patch has variance ratio0.07218 and mean encoded-luma drift0.000317. These are
specific synthetic measurements, not general photographic quality scores.

CTest80 `denoise-workflows` covers combined halos, four output spaces, exact
regions/strips/histograms/three plots, strict snapshots and failed-open writer
preservation, invalid-write atomicity, commands/presets/XMP/selective transfer,
actual controller density/full scopes, project copies/history and real panel
input. Twelve PNG16/TIFF16/WebP8 exports preserve ICC and alpha; PNG/TIFF retain
all RGBA16 pixels, WebP retains alpha8 and visible RGB8. Six actual panel images
cover Chinese/English at300/320/350px with both sliders, Reset and Undo. This
focused component host is not packaged normal-App or physical-tablet acceptance.

A24,000,000-pixel FP32 fixture with Paint/Erase, Sony detail, independent
sharpening, grain and denoise completes in10,495ms locally with four CPU threads
on Apple M2. Fixture generation, source hashing and three independent regional
renders are included. Source SHA-256 and regional pixels match exactly; peak
accounted ownership is2,576,225,792 bytes and returns to0. This is the shared
budget's accounting, not process RSS. The32Mi-pixel source cap,3GiB shared
budget and60-second operation gate remain unchanged.

One existing Sony A7 IV ISO1600,1/60s,F2.8 scene is decoded as7028×4688 RGBA64.
Its central1600×1000 crop passes four ICC spaces, unchanged source/alpha and
three exact regions; four before/after384px crop pairs are retained. The
four-space render/fingerprint observation takes3,382ms locally after decoding.
Visual inspection shows reduced wall noise and retained major edges; the
observation covers one scene and does not establish broad ISO/camera/skin or
blind photographic-quality acceptance.

The GPU test adds76 cases requiring exact CPU-reference-upload RGBA16 pixels,
all histogram channels and explicit fallback diagnostics across integer/FP32,
two encodings, four spaces and two densities, including Paint/Erase. Native
admission refusal is checked for L-only, C-only and combined settings on both
integer and FP32 contexts. The original40/65535 compute tolerance is unchanged.
An initial whole-QImage comparison failed because texture readback has no ICC
metadata; the corrected test compares every pixel byte and separately checks
statistics/producer. The original failure remains retained. Initial CLI
out-of-range rejection expectations were likewise corrected to the registry's
existing documented clamp policy; malformed-type refusal is still required.

Targeted checks are not an exact-commit full80-test, three-platform or package
acceptance claim. Early Core/GPU exploratory runs omitted the Sony fixture
context and contain a skip; separate corrected Sony methods pass, and those
incomplete reports are retained. Full validation must run with both required
RAW and Sony fixtures. CPU denoise first-stage completion does not complete
the broader F0–F6 fusion plan.
