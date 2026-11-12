# F4 M10 — independent CPU dehaze with full-frame context

Signed Dehaze is now an independent Develop control. Positive amounts reduce an estimated atmospheric veil; negative amounts add a uniform veil. The amount ranges from -100 to100 and defaults to zero. The Chinese/English panel explicitly says CPU, provides independent Reset and warns that high amounts can emphasize noise or alter bright areas. Controls remain usable at300/320/350px panel widths.

This is a CPU-first stage. Ordinary GPU previews upload the complete CPU reference and identify `dehaze-cpu-stage`; they do not compute dehaze on QRhi. Native brush admission also retains its explicit CPUPolicy. Existing denoise, texture and clarity computation remains available when dehaze is disabled.

## Algorithm and context

The processing order is decoded linear ProPhoto → denoise → dehaze → texture → clarity → WB/exposure, followed by the existing color/output stages. Positive dehaze estimates local transmission from a normalized dark channel, refines it with an alpha-weighted intensity/spatial bilateral neighborhood and applies a bounded radiometric inverse. Dark support is at most7 source pixels and refinement support3, both scaled to prepared-image density. Negative dehaze uses a uniform transmission and has no neighborhood halo. RGB remains unclipped before tone/output; alpha is unchanged. Hidden/tiny-alpha pixels cannot provide visible neighbor color.

The atmospheric image model and dark-channel prior follow the ideas in [He, Sun and Tang, CVPR2009](https://www.cvl.iis.u-tokyo.ac.jp/class2013/2013w/paper/colorAnalysis/06_cvpr09_kaiming_he.pdf). This implementation independently adapts those ideas to scene-linear data, associated-alpha sampling, bounded global estimation, bilateral refinement and a negative veil. It does not reproduce the paper's soft matting or claim Adobe equivalence. Bright objects similar to atmospheric light remain a limitation of this prior.

Airlight is estimated from the entire corrected frame before viewport cropping or preview resizing, independently of WB, exposure, output space and denoise settings. At most64×64 cells use associated-alpha bilinear probes. Candidate ranking uses local dark values and brightness; the resulting RGB is finite and positive. Transparent/nonpositive frames retain a deterministic fallback. The sampler performs at most163,840 source reads, independent of full-frame pixel count, and observes cancellation.

Prepared previews retain these small facts beside normal/fast/GPU images under the same cache budget. The controller supplies them to viewport plans. Low-level cropped/downsampled plans without valid full-frame context are refused. Whole-frame rendering, original brush sources, streamed JPEG and full scopes resolve facts before cropping/strips; prepared brush plans preserve validated context. This prevents a different global airlight estimate on every pan or export strip.

## State, memory and cache

Canonical optional state is `dehaze: {schema: 1, amount: ...}` and is omitted at zero. Unknown fields, unsupported schema, nonnumeric/nonfinite values and out-of-range values are refused at persistence boundaries. The registry now contains24 scalar parameters and `dehaze.reset`. Project read/write safety, history coalescing, copies, presets/XMP, selective detail transfer, CLI and all export formats include the amount. Airlight is source context, not persisted edit intent.

The packed plan has58 vec4 slots/928 bytes: amount/density and source-airlight context extend the previous56-slot prefix. Every shader declaration uses the same layout. The existing engine/profile identity and default-zero pixel math remain unchanged.

Positive CPU ownership is32 bytes per expanded pixel: linear RGBA32F16, intensity/alpha8, dark/coarse4 and horizontal-min4. Spatial stages execute sequentially, so texture's existing36-byte maximum remains the largest shared stage reserve. Halos are summed across all active neighborhoods. The32Mi-pixel and3GiB brush caps and all operation/test/job deadlines remain unchanged.

Completed non-brush spatial stages and final images share the existing64MiB LRU. Keys include exact source identity/format/encoding/input rows, denoise, dehaze amount/effective support/full-frame airlight, texture and clarity. Downstream WB/exposure/output/detail/grain edits reuse the completed stage. Failed/cancelled work is not published; brush sources keep owning admission and output leases. GPU CPU uploads remain explicitly uncached.

## Current local evidence

The independent kernel group passes9 Qt observations, including an explicit two-dimensional double-precision oracle, irregular HDR/alpha input, fractional refinement support, exact serial/parallel results, crop halos, invalid parameters and cancellation. The estimator test reads at most163,840 pixels for a procedural80MP frame; one observed time is0.81ms. A separate corrected616×922 frame estimate takes0.792ms and remains identical across fit/ROI/preparation-cache and downstream color changes. These are scoped M2 observations, not a general latency claim.

Workflow18 and cache10 observations pass after correcting a copied texture-specific memory expectation and explicitly supplying full-frame context to a downsampled-plan test. The first failed report remains separate. The first related16-test run also retains an old23-parameter Core assertion and three missing-RAW-environment skips; after updating the strict count to24 and supplying the original DNG, Core217 passes with zero failures/skips. The other15 related CTests pass, Performance39 has zero failures/skips and CLI retains its exact count/type/export checks. Project refusal cases preserve the existing catalog writer and bytes. All four1024-bin histograms and three full plots match their independent encoded frame. Whole/edge/panned regions and admitted strips match the whole image exactly.

The first24MP FP32 Paint/Erase+denoise+dehaze+texture+clarity+detail/grain observation takes13,541ms with2,672,225,792 accounted bytes, within the unchanged3GiB/60s limits. This reservation is not process RSS. A second observation takes13,641ms.

Actual Main.qml/PhotoCanvas CPU/English and Metal-display/Chinese workflows pass positive/negative QPA gestures, independent Reset, complete Undo/Redo state, project reopen, exact scopes/plots and actual PNG16 export. The native operations take5,058/4,219ms; independent Python validation checks all64,777 RGBA16 pixels and ICC bytes. CPU observes a real stage-cache hit; Metal display reports a complete CPU upload and zero stage hits. Frozen ZIP diagnostics preserve source/revision/producer and valid whole-frame airlight. Six adversarial evidence methods reject stale frames, incomplete operations, invalid airlight and false compute claims. Seven actual invalid-argument/occupied-output invocations return2 and preserve every owned file. All six original denoise/clarity/texture CPU/Metal main-App workflows pass after the new panel and preparation metadata are added.

GPU57 observations pass with zero failures/skips. The new64 combined integer/FP32 × input-encoding × output-space × amount × denoise cases require byte-exact complete CPU uploads, exact histograms and explicit producer identity; they include texture, clarity, sharpening and grain. Native brush checks require CPUPolicy for both signs and pixel formats. Existing numerical policies and40/65535 remain intact.

One existing Sony A7 IV ISO1600 decoded1600×1000 crop covers four output spaces, three regions and read-only-source checks. Inspected before/after output shows stronger local contrast and noise at high amount. This indoor scene is not broad photographic dehazing, sky/skin/ISO quality, camera coverage or native floating-point RAW acceptance. LibRaw still decodes to RGBA64.

## Outstanding acceptance and retained failures

Current source is still under development. Its clean90-test run, current three-platform jobs and both deployed78-mode manifests remain required. Packaging preserves every original76-mode call in order and appends two dehaze contexts. CI retains every original product step, deadline and artifact path, adding the new reports.

Parent CPU texture CI375 macOS fails two unchanged120-second33MP erase PNG16/WebP CTests; no package success is inferred. The original810,006-byte log is retained (SHA2563c70232692ece4f4a45367f7b76d6a9a47129e200281ab0034bfc8de6571fc6f). Separate descendant local triage passes the same tests in40.35s/25.26s; this does not identify the cloud cause or override its failure. Large-raster tests now print phase checkpoints before/after loading, export, decode, independent reference and complete pixel/ICC checks, without changing their assertions or limits.

Whole F0–F6 development remains incomplete. Healing, Point Color, broader RAW/physical-device quality, soft proof and remaining catalog/command/import work are separate gates; none is claimed by this stage.
