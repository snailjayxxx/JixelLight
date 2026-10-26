# F3 — reuse completed color pixels for full-resolution analysis

Implementation and acceptance work in progress on `design/lightcraft-fusion-20261008`; PR #16 remains Draft. This stage adds an optional, independently budgeted `AnalysisFrameCache`. Local component/integration tests, the final combined full suite, a paired workload and quiet GUI runs passed as recorded below. Final-tree CI and actual deployment packages remain pending.

## Measured duplicate work

The actual Windows x64 deployed CPU GUI report from [CI #40275215812](https://github.com/snailjayxxx/JixelLight/actions/runs/40275215812), artifact `package-Windows/cpu.json` (`source_commit: fdc24a6`), records this sequence for the final **10,340,960-pixel** full-frame analysis:

| Requested result | Recorded stage | Stage duration | Nested `cpu_pipeline` calls | Sum of their durations |
| --- | --- | ---: | ---: | ---: |
| Exact histogram | `full_resolution_scopes` | 6,275.306 ms | 21 | 6,199.812 ms |
| First scope plot, shared three-mode batch | `cpu_scope_plots_shared` | 8,358.009 ms | 21 | 6,208.872 ms |

Both groups report a summed `cpu_pipeline.pixels` of **12,222,560**, including the regions the existing detail processing requests beyond the logical strip centers. The subsequent two `scope_plot_cached` requests take 0.091 ms and 0.093 ms and contain no color-pipeline calls: the three plots already share a completed plot batch. The missing reuse is between exact histogram rendering and that first plot batch.

These nested measurements establish duplicate color work in this particular package run. They do not establish a general Windows regression, explain all VM or GUI wall-time variance, or predict a fixed speedup. The original CPU GUI limit remains **60,000 ms**. The local paired comparison below includes both rendering saved and full-frame collection cost; a final Windows deployment comparison remains pending.

## Request flow

`PhotoController` gives its `FullScopesCache` and `ScopePlotCache` one shared `AnalysisFrameCache`, defaulting to **320 MiB**. Standalone scope-cache constructors have no analysis cache unless one is explicitly supplied. The existing histogram-result and plot-result cache budgets remain **4 MiB** and **16 MiB**, respectively; the preview render cache is separate.

On a histogram-result miss, `FullScopesCache` first looks for a matching completed analysis frame. A hit counts those existing encoded RGBA64 pixels directly. A miss applies the requested geometry, completes the existing B5 whole-operation preflight, and attempts optional frame admission. The existing `ScopesEngine::analyzeFull` loop continues to render and count its **128-row central strips**. Only after a strip has been counted successfully does its observer copy those central pixels into the admitted frame, in increasing `y` order. `processRegion` has already cropped away any detail halo; halos are not appended to the full output.

The histogram neither requests nor computes plot counts or plot ink. Only a complete, uncancelled histogram pass can publish the collected frame. A writer refusal, append failure, metadata/allocation exception or stale publication discards the optional collector while the original valid streamed histogram continues. Observer exceptions are caught and disable subsequent collector calls for that pass.

On a full-resolution plot-result miss, `ScopePlotCache` performs only `find` on the analysis cache. A completed-frame hit counts its pixels without another geometry or color pass. An unfinished producer is invisible; the plot request immediately follows its existing streamed path instead of waiting, joining the producer or adopting its cancellation token. Plots do not populate the analysis-frame cache. The existing cacheable three-mode batch remains shared only after a plot has been requested. If the plot-result cache is disabled or cannot admit its ink, only the requested mode is computed. Preview plot requests do not use full-resolution analysis frames.

The retained image contains the pipeline's final encoded RGBA64 output, before monitor presentation. It does not replace the FP32 scene source or change where HDR values are processed and finally encoded.

## Frame identity and ownership

`StageGraph::analysisFrameKey` accepts only a non-null source and a complete `FrameRect` of `(0,0,1,1)`. Partial-frame/viewport plans have no reusable analysis-frame key. The key includes:

- The existing render key: source `QImage::cacheKey`, dimensions and format; engine version; input encoding and output space; RAW/base exposure; packed plan slots; kernel-read double state and curves; active brush document; active LUT identity, size and digest.
- Geometry serialized before applying it, so a fresh geometry output allocation cannot change the request's original source identity.
- Digests of the source ICC profile and selected output ICC profile.
- The analysis policy version, RGBA64 format, 128-row collection policy, post-detail central-strip semantics and pre-monitor boundary.

Monitor ICC, display LUT and delivery revision are absent. Source identity uses the existing in-process image identity rather than hashing the entire image on the slider path. LUTs are immutable while keyed; each retained entry also owns the active LUT, preventing its pointer identity from being recycled while an entry or consumer lease still relies on it.

The cache retains **one latest completed entry**. An entry contains the completed image and optional LUT, with no source image, geometry image, processing plan or adjustment-document snapshot. Writers may coexist within the independent byte budget. A valid new request attempt for a different key advances the generation, including an attempt that later cannot reserve its budget. An older writer cannot then replace newer state. Duplicate in-flight or already-completed keys bypass admission without invalidating their existing producer or result.

`Writer` is move-only and cleans up pending registration and reservation on abandonment, failure or cancellation. Appends require RGBA64, the exact width, strictly consecutive `y`, a fitting positive strip height and consistent color space. The first valid strip supplies color space, device-pixel ratio, dots-per-meter and text metadata. Pixels are copied row by row with cancellation checks; strip pixel storage is never shared with the completed entry. Partial frames are never visible to `find`.

`Lease` is a copyable immutable entry handle. Consumers must keep it alive for the entire use of `image()` and must not retain a shallow `QImage` copy or reference beyond that handle. Current histogram and plot consumers retain the lease while counting. Cache destruction prevents later writer publication but does not invalidate an already-held consumer lease.

## Independent memory budget

The **320 × 1,024 × 1,024-byte** limit charges the following together:

- In-progress frame reservations, before allocating their RGBA64 pixel storage.
- The latest cached completed frame.
- Evicted frames still held by consumer leases.
- `Entry` storage, the retained `LookLut` object and its numeric **allocated capacity** (`rgb.capacity() * sizeof(float)`), not merely its logical sample count.

A 33,000,000-pixel RGBA64 output requires approximately **251.8 MiB** of pixel storage, plus the charged entry/LUT cost. Source pixels, geometry outputs, allocator/control-block/key overhead and variable metadata are outside this cache's budget. For example, an FP32 source, a separate FP32 geometry result and the RGBA64 analysis frame can together occupy approximately 1,259 MiB at that pixel count, before other working buffers and caches. The analysis budget is not a process-wide resident-memory cap.

Retiring the cache's ownership does not release its charge while any consumer lease remains. A following frame can therefore be refused even though it would fit alone. Reservation uses shared atomic accounting; charge release does not acquire the cache mutex. Image and LUT members are destroyed before their charge is released. Admission does not wait for a consumer, an older writer or spare budget. Checked dimension/cost arithmetic and an individual oversize check precede image allocation. Allocation and metadata failures return an invalid writer/false, preserving the existing stream fallback.

Snapshot fields distinguish `charged_bytes`, `building_bytes`, `cached_bytes`, `peak_charged_bytes` and `leased_estimate_bytes`. The lease estimate includes an actively borrowed cached entry as well as retired leased entries, so it can overlap `cached_bytes` and is **not additive**. Counters include find hits/misses, admission bypasses and their reasons, cache-ownership evictions, observed cancelled operations, stale publication refusals, allocation failures and successful complete publications. These are component events rather than counts of unique GUI requests. `inflight_entries` describes registered writers, not an execution queue. Diagnostic snapshots are recorded after request-owned leases/writers are released.

The active-brush research limits remain **2 Mi logical requested pixels, 8 Mi candidate visits per tile and 96 Mi visits per whole operation**, including repeated strip halos. The analysis cache does not grant brush access to 24 MP or 33 MP images. Brush UI, persistence and production admission gates are unchanged.

## Regression coverage

Five component tests exercise ownership and publication independently of scopes:

| Component test | Coverage |
| --- | --- |
| `analysisFrameCachePublishesOnlyCompleteIndependentPixelsAndMetadata` | Exact multi-strip RGBA64 pixels and alpha, independent strip storage, first-strip metadata, immutable copied leases, completed-only visibility and byte snapshot fields. |
| `analysisFrameCacheMalformedCancelledAndAbandonedWritersAreAtomic` | Overlapping/out-of-order rows, wrong format/width/height/color space, empty strips, incomplete publication, cancellation at begin/append/publish/find, move/abandon cleanup, no partial entry or leaked reservation. |
| `analysisFrameCacheLeasesRetainBudgetAndProducersNeverWaitOrPublishStale` | Eviction with multiple old leases, refusal until the last lease releases charge, same-key in-flight/completed immediate bypass, newer producer wins, budget-refused newer attempts still prevent stale publication. |
| `analysisFrameCacheChargesLutAndGuardsDisabledOversizedAndOwnerLifetime` | LUT retention and object/numeric-capacity charge, deliberately reserved spare LUT capacity, disabled/negative budgets, oversized and arithmetic-overflow guards before allocation, leases/writers outliving the cache owner. |
| `analysisFrameCacheConcurrentReservationsStayBoundedAndCancelIndependently` | Eight real worker threads, three-frame budget with a pinned old lease, overlapping admissions, bounded charged/peak bytes, one accepted producer cancelled without changing other tokens, reservation cleanup and independent recovery. Barrier timeout paths unblock and join workers before asserting. No particular concurrent producer is required to publish. |

Six integration tests compare exact histograms, clipping percentages, pixel counts and plot ink against independent CPU references:

| Integration test | Coverage |
| --- | --- |
| `analysisFrameHistogramCollectsStripsAndFullPlotsReuseExactPixels` | FP32/HDR/alpha source with geometry and active bounded brush/detail; more than one central strip; complete encoded pixels/metadata match; all three plots match full-frame counts; the plot batch adds zero geometry or color stages; subsequent two modes use the existing plot cache. |
| `analysisFrameCompletedPixelsRebuildHistogramWithoutGeometryOrColor` | Histogram-result caching disabled for the producer, then a separate histogram cache rebuilds exact results from the shared completed pixels with no geometry or color stages. |
| `analysisFrameKeysTrackSourcePlanGeometryIccAndRemainPreMonitor` | Source edit, packed and precise double plan edits, geometry, source/output ICC, brush/LUT changes and partial-frame exclusion; monitor presentation does not contaminate statistics; changed-source requests publish and consume their own pixels. |
| `analysisFrameDisabledOversizePinnedBudgetAndPreviewUseStreamFallback` | Disabled/oversize admission and pinned-old-lease budget refusal preserve the original exact streamed histogram and plots; preview plots ignore full-resolution frames. |
| `analysisFrameInFlightProducerNeverBlocksHistogramOrPlotFallback` | A held partial writer remains unfinished while independent histogram and plot requests complete through streaming; no partial publication; release and later recovery. |
| `analysisFrameCancellationDoesNotPublishAndCollectorFailureKeepsHistogram` | An observer throwing `std::bad_alloc` is disabled after one call without changing the histogram; pre-cancelled and observed in-flight histogram cancellation publish nothing and release charges; a later independent request succeeds; source pixels remain unchanged. |

The existing `controllerDiagnosticHashesUseCurrentParametersAndGeometry` test also asserts the shared `analysis_frame_cache` diagnostic object exists, reports its 320 MiB budget and never reports charge above that budget.

`PerformanceTests::analysisFrameReuseMeasuresColdHistogramAndThreePlotRequests` adds three alternating baseline/reuse pairs for a 2048×1536 explicit FP32 signed/HDR/mixed-alpha source, mild straighten/crop, active detail and no brush. Each group requests a full histogram and waveform/parade/vectorscope with fresh 4 MiB histogram and 16 MiB plot caches; reuse also receives a fresh **32 MiB** analysis cache. Every sample checks all four 1024-bin histograms, clipping percentages, pixel counts and three plot images exactly, histogram miss 1/hit 0 and plot miss 1/hit 2. The baseline must record one shared plot pass, while reuse records one encoded plot batch, one complete frame publication and bounded peak charge. Counter deltas and cache snapshots are cumulative and do not depend on the recorder's bounded 2,048-sample timing deque. There is no time or speedup assertion and the suite timeout remains unchanged.

## Local paired measurement

Qt 6.8.3 Release on Apple M2/macOS 26.5, with CPU thread budget **6**, passed the new targeted performance case: **3 passed / 0 failed / 0 skipped**, 4,251 ms including init/cleanup. The three paired groups each analyze **2,918,067 actual post-geometry pixels**. The timer covers the complete histogram request followed by all three plot requests, including geometry, rendering and optional frame allocation/copy. Fixture creation, cache constructors, assertions and measurement reporting are outside it. Every group begins with cold result caches and no completed analysis frame.

| Path | Three end-to-end samples (ms), in sampling order | Median (ms) |
| --- | --- | ---: |
| Existing histogram + shared plot-color pass | 897.560792, 847.931583, 862.787875 | 862.787875 |
| Histogram collects frame + plots count completed pixels | 538.433208, 536.491375, 531.856750 | 536.491375 |

Groups run in **baseline/reuse, baseline/reuse, baseline/reuse** order. Across the three reuse samples there are exactly **3 publications and 3 encoded plot batches**; the baseline has **3 shared plot passes**. Each baseline plot batch already serves all three modes with one color pass. Both paths produce exactly equal histograms, clipping values, pixel counts and three plot images. Peak charged analysis storage is **23,344,584 bytes**, below the **33,554,432-byte** test budget.

Reuse lowers the measured median by **326.2965 ms for this device, fixture and end-to-end request sequence**. This includes collection cost, rather than timing an isolated cache lookup. It is not a general CPU-slider latency result, a cross-machine guarantee or a Windows GUI wall-time claim. Raw sample evidence is retained in `/Volumes/MacNAS-Dev/JixelLight-Build/analysis-frame-performance-targeted.txt`. The paired case also passed inside the final full suite; the dedicated targeted run above remains the primary local performance measurement.

## Quiet local GUI evidence

Separate local runs without overlapping compilation passed the unchanged GUI assertions using the public DNG. CPU and native Cocoa/Metal are different presentation paths; these elapsed times are **not a CPU-versus-GPU speed comparison**.

| Local presentation path | GUI elapsed | Successful screenshots | Full scope pixels | QML error markers | Screenshot grab / save total |
| --- | ---: | ---: | ---: | ---: | ---: |
| CPU | 13,923 ms | 22/22 | 10,340,960 | 0 | 13.030 / 1,715.453 ms |
| Native Cocoa, Metal, Apple M2 | 14,959 ms | 22/22 | 10,340,960 | 0 | 227.503 / 2,202.693 ms |

Both reports record one `scope_plot_encoded_passes` batch counting exactly **10,340,960 pixels**. Each analysis cache reports **5 publications, 4 evictions, 1 hit and 5 misses**, peak charge **82,727,728 bytes** under the **335,544,320-byte** default budget, and zero bypasses/allocation failures. Final snapshots have one smaller cached result charged at **24,624 bytes**, no building reservation, no in-flight entry and no leased estimate. These counters confirm completed-frame reuse in the actual GUI sequence without interpreting the final small entry as the large frame's peak allocation.

Whole-process `/usr/bin/time -l` memory measurements are separate:

| Local presentation path | Maximum resident set (bytes) | Peak memory footprint (bytes) |
| --- | ---: | ---: |
| CPU | 668,794,880 | 744,375,856 |
| Native Cocoa/Metal | 660,815,872 | 786,515,648 |

These process peaks include source/geometry images, other caches, renderer/UI resources and allocator overhead. They are not analysis-cache charges and are not constrained by its 320 MiB limit. Screenshot save time includes PNG encoding and file I/O. The combined local summary is retained at `/tmp/jixellight-analysis-local-summary.json`; these working-tree runs do not establish a final committed-tree or deployed-package acceptance.

## Acceptance evidence

| Validation | Status |
| --- | --- |
| Component tests, local Qt 6.8.3 Release | **Passed:** five targeted slots, QtTest total **7 passed / 0 failed / 0 skipped**, 5 ms including init/cleanup. This verifies the component tests, not full application acceptance. |
| Six integration tests, local Qt 6.8.3 Release | **Passed:** six targeted slots, QtTest total **8 passed / 0 failed / 0 skipped**, 829 ms including init/cleanup. |
| Initial full local CTest, including controller diagnostic, real Sony/DNG, Metal GPU parity and brush research regressions | **Passed 10/10 in 78.32 s:** **203 Core / 36 Metal GPU / 38 performance**. This run preceded addition of the paired performance slot. |
| New paired performance case | **Passed 3/0/0 in 4,251 ms**, with exact results and the three-sample measurements above. No timing gate was added. |
| Final combined full local CTest including the new performance case | **Passed 10/10 in 77.61 s:** **203 Core / 36 real Metal GPU / 39 performance**, real Sony fixtures, zero failed or skipped. The original 120 s suite timeout is unchanged. |
| Quiet local CPU and native Cocoa/Metal GUI | **Passed:** both 22 successful screenshots, complete 10,340,960-pixel scopes, one encoded plot batch, zero QML markers and elapsed time below the unchanged 60,000 ms gate; evidence above. |
| Three-platform final-tree CI and actual deployment packages | Pending; no earlier CI run accepts this new cache implementation. |
| Final Windows/deployment performance comparison | Pending. The local paired result establishes a benefit only for the measured device, fixture and request sequence. |

An early local app run overlapped compilation and is excluded from performance evidence. Acceptance must use the final integrated tree and retain the original pixel, provenance, cancellation, public-camera-fixture and GUI assertions.
