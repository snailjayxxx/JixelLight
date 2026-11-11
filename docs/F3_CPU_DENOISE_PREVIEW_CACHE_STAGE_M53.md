# F3 M8 CPU denoise preview reuse within the existing budget

CPU previews now retain a completed denoise input alongside final preview
results. Exposure, WB, saturation, Sony styling, output space and downstream
spatial changes can reuse that immutable scene-linear plane. Both entry types
share the existing64MiB rendered-preview LRU; there is no additional cache
budget. A stage is eligible only when it and the requested final result fit
together. Small/disabled caches preserve their original result-only behavior.
The accounting explicitly excludes allocator overhead and live caller images,
as before; it is not a process-RSS limit.

A dedicated key includes source identity/size/format, encoding, all input-matrix
rows, both denoise amounts and effective density radius. Downstream color,
output, monitor ICC and unrelated editing state are excluded. Changed sources,
input transforms and density cannot reuse old filtered pixels. Only completed,
uncancelled renders publish stages. Concurrent misses can compute twice but
retain one immutable stage and result. Diagnostic cache snapshots add stage
hit/miss/bypass counters without changing the final-render key or schema.

The preparation/render bridge is private to ImagePipeline and its preview
cache friend. Enabled brushes always retain the existing owning admission and
final-result lease; they never use or retain an uncharged denoise stage. Full
scopes, exports, diagnostic captures and regional/strip reference rendering
continue through their unchanged fresh-reference entry points. The32Mi-pixel,
3GiB brush and all original numerical/time limits remain unchanged.

CTest81 `denoise-preview-cache` has9 targeted passing Qt observations with
zero failures/skips: exact reference pixels across integer/FP32 and two
encodings/four spaces, downstream reuse, source/matrix/amount/density
invalidation, shared-budget eviction/bypass, retained-output lifetime, brush
admission/denial, cancellation, invalid metadata and concurrent immutable
misses. The first run's empty fixture-size mistake is retained separately;
correcting the test fixture yields the complete passing report.

The original39 performance observations and16 denoise workflows also pass,
including exact scopes, exports, durable state and the24MP owned operation.
An additional controller test separately confirms that actual CPU exposure/WB
changes reuse the completed stage, that a denoise edit rebuilds it, that the
published preview matches the fresh reference and that source bytes remain
unchanged. This new method is not represented as part of the earlier16-result
report; the expanded workflow suite now contains17 observations.

One Apple M2/four-thread769×513 synthetic sample measures20.345ms cold,
15.868ms total for three cached color edits versus59.672ms for three uncached
references, with18,940,928 charged cache bytes. Pixel results are exact and
only one spatial stage is built. These single-run elapsed measurements are
not a general latency/speedup guarantee. The separate24MP brush-bypass
regression completes in10,445ms with the unchanged2,576,225,792-byte accounting.

This cache work is isolated from denoise PR17/CI368 and the capacity-layout
PR16/CI367; it does not cancel either. The clean80-test CPU parent and Metal43
GPU follow-up remain their own evidence. The new head still requires its
complete81-test three-platform/package workflow. Sensor-adaptive denoising,
broader photographic quality and remaining F0–F6 features are unfinished.
