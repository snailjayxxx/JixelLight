# F4 M7A QRhi denoise and resident stage reuse

This entry records `125c83a` / draft PR17. Subsequent CPU-only preview reuse
is recorded separately in [M8](F3_CPU_DENOISE_PREVIEW_CACHE_STAGE_M53.md).

The CPU denoiser from6effe5c now has a two-pass QRhi implementation for normal
previews. The first pass makes an Oklab guide; the second filters into a
scene-linear ProPhoto texture before the existing color/detail passes. Two
RGBA32F textures add32 bytes per preview pixel, allocated lazily and discarded
on source-size changes. Source decoding, the54-slot plan, CPU reference,
spatial/alpha semantics and Sony profile engine version are unchanged.

The denoise-only CPU policy is removed. Existing brush, FL/HSL and grain/Adobe
numeric policies still upload the complete CPU result; native brush admission
still explicitly refuses active denoise with `CPUPolicy`. Normal compute and
CPU upload have distinct working-frame diagnostic producers and an explicit
`spatial_denoise` execution field. The generic bilingual Denoise section no
longer describes a CPU-only feature. Full scopes and exports retain the CPU
reference; no native floating-point RAW decoder is introduced.

The guide and filtered texture remain resident between frames. Guide identity
includes the actual source cache key, encoding and all three input-matrix rows;
size changes discard it. The filter also depends on both amounts and effective
pixel-density radius. Exposure, WB, saturation and output-space changes reuse
both textures. Amount/density changes rerun only the filter; source/encoding/
input-matrix changes rerun both. A separate guide source identity prevents a
new source uploaded while denoise is off from incorrectly reusing the previous
guide. Returning from CPU fallback cannot overwrite the cached denoise planes.
Counters expose actual submitted guide/filter dispatches and hits.

The complete local GPU suite now passes43 Qt observations, zero failures/skips.
New coverage includes76 combined cases (54 mandatory compute and22 existing
CPU policies),32 pure noisy/dark cases, eight actual Sony crop cases, resource
enable/detail/resize transitions and exact cache invalidation checks. All cases
retain the original RGB40/65535 bound and exact alpha; histogram channels match
the actual output pixels. CPU-policy cases additionally require byte-exact
RGBA16 uploads. Maximum observed denoise error on Metal/Apple M2 is14/65535;
noisy/dark cases reach9 and the existing Sony ISO1600 crop reaches2. These are
Metal observations, not substitutes for Direct3D/OpenGL execution.

The formal App builds. Two complete normal-App Metal regressions pass: canvas
in19,102ms and ordered Paint/Erase in8,935ms, with terminal phase identifiers41
and35. Those identifiers are not counts of executed stages. The deletion
control is fully visible at y64..88 in a240px tool viewport. These runs use the
prepublication6effe5c build identity plus the changed GPU/QML sources; they do
not establish a future commit or packaged denoise interaction. Six renewed
Chinese/English panel PNGs at300/320/350px pass actual sliders/Reset/Undo and
bounds checks. The first shader build's missing-include-extension error is
retained; the corrected qsb bundles include the common file in their depfiles.

The CPU parent itself completes a clean exact-commit80/80 local CTest run in
772.27s: Core217, GPU40, Performance39, XMP20, grain12, sharpening14, denoise
kernel9 and denoise workflow16 all have zero failures/skips. Its24MP combined
owned operation takes10,531ms with2,576,225,792 accounted bytes and unchanged
source/regions under the original3GiB/60s limits. This full run belongs to
6effe5c; the GPU follow-up has the scoped validation above and still requires
its own complete three-platform/package workflow. CPU-only stage caching and
broader photographic quality validation remain unfinished.

Capacity-layout parent CI367 atd27a46c has now completed Linux successfully:
78/78 in1,124.03s followed by all normal-App/GPU checks. The original26,798,796-
byte job log has SHA-256ac9ad9ccda237bd221636835dcae1a6ea07c68020a5b45ea024f27b36a3c95b6.
Desktop jobs remain separate and pending at this entry; no whole-workflow
acceptance is inferred from Linux. Denoise work is kept on a separate branch
so its validation does not cancel that existing capacity-layout run.

Extended boundary validation later exposes source-dependent numerical regions
not covered by these initial fixtures. [M9](F0_FLOAT_SOURCE_NUMERIC_POLICY_STAGE_M54.md)
records the original failures and explicit policies, including a revised
48-compute/28-CPU split for the76 combined cases. The earlier54/22 observations
remain bound to125c83a and are not relabeled as current-head compute coverage.
