# F3 — reuse completed color pixels for full-resolution analysis

Accepted at implementation HEAD `62d64d11dfd5d1201bfb943d9e41a968c18bdc80` by [CI #40285937542](https://github.com/snailjayxxx/JixelLight/actions/runs/40285937542) on `design/lightcraft-fusion-20261008`; PR #16 remains Draft. This stage adds an optional, independently budgeted `AnalysisFrameCache`. Local component/integration, full-suite, paired-workload and quiet-GUI evidence is retained below alongside this exact-tree three-platform and actual-package acceptance.

## Measured duplicate work

The actual Windows x64 deployed CPU GUI report from [CI #40275215812](https://github.com/snailjayxxx/JixelLight/actions/runs/40275215812), artifact `package-Windows/cpu.json` (`source_commit: fdc24a6`), records this sequence for the final **10,340,960-pixel** full-frame analysis:

| Requested result | Recorded stage | Stage duration | Nested `cpu_pipeline` calls | Sum of their durations |
| --- | --- | ---: | ---: | ---: |
| Exact histogram | `full_resolution_scopes` | 6,275.306 ms | 21 | 6,199.812 ms |
| First scope plot, shared three-mode batch | `cpu_scope_plots_shared` | 8,358.009 ms | 21 | 6,208.872 ms |

Both groups report a summed `cpu_pipeline.pixels` of **12,222,560**, including the regions the existing detail processing requests beyond the logical strip centers. The subsequent two `scope_plot_cached` requests take 0.091 ms and 0.093 ms and contain no color-pipeline calls: the three plots already share a completed plot batch. The missing reuse is between exact histogram rendering and that first plot batch.

These nested measurements establish duplicate color work in this particular package run. They do not establish a general Windows regression, explain all VM or GUI wall-time variance, or predict a fixed speedup. The original CPU GUI limit remains **60,000 ms**. The local and CI paired comparisons below include both rendering saved and full-frame collection cost. The exact-tree deployment evidence later in this document also verifies that the first plot batch reuses completed pixels without another color pass.

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

## Exact-tree three-platform CI acceptance

[CI #40285937542](https://github.com/snailjayxxx/JixelLight/actions/runs/40285937542) completed **Success** for Windows x64, macOS ARM64 and Linux GPU. Its recorded implementation HEAD is **`62d64d11dfd5d1201bfb943d9e41a968c18bdc80`**; checkout tested merge **`fb56bd6265eb696250be88302613905908395090`**. Both commits have tree **`2dd470da79b69e672a1ecbd1b5f2d8ca3a156566`**, establishing that this exact implementation tree was tested.

Each platform passed **CTest 10/10**, **203 Core / 36 mandatory GPU / 39 performance** checks with **zero failed or skipped checks**. All **11 analysis component/integration slots** listed above and the new paired performance slot passed on all three platforms. The original 16K-dab viewport and six full/panned HDR brush/output-space combinations retained maximum RGB error **0/65535**, exact alpha, complete **1024-bin Scopes** and explicit CPU-reference-upload provenance under the unchanged **40/65535** bound. Checksum-pinned public Sony RAW/JPEG, native GUI/canvas, ICC and sensor/refusal/readiness gates passed.

### Three-platform paired samples

These are separate controlled comparisons within this run, using the same cold-result-cache, alternating baseline/reuse procedure as the local measurement: **2,918,067 post-geometry pixels**, three samples per path, histogram followed by all three plots, including frame collection cost. Every sample retained exactly equal histograms/clipping/pixel counts and plot images. Actual thread budgets are recorded rather than inferred from logical processor counts.

| Platform | Thread budget | Path | Three end-to-end samples (ms), sampling order | Median (ms) | Analysis peak charged bytes |
| --- | ---: | --- | --- | ---: | ---: |
| Linux | 2 | Existing histogram + shared plot color | 2495.483311, 2494.546261, 2497.77724 | 2495.483311 | No analysis cache |
| Linux | 2 | Histogram collection + encoded plots | 1444.65999, 1450.009024, 1461.285927 | 1450.009024 | 23344584 |
| macOS | 1 | Existing histogram + shared plot color | 3195.189042, 3129.540042, 3050.572583 | 3129.540042 | No analysis cache |
| macOS | 1 | Histogram collection + encoded plots | 1854.968125, 1845.003625, 1539.737459 | 1845.003625 | 23344584 |
| Windows | 2 | Existing histogram + shared plot color | 4808.4873, 4803.2003, 4843.8032 | 4808.4873 | No analysis cache |
| Windows | 2 | Histogram collection + encoded plots | 2800.4089, 2781.5025, 2799.0413 | 2799.0413 | 23344584 |

All three reuse groups report the same **23,344,584-byte** peak under their **33,554,432-byte** test budget. Each platform records **3 baseline shared plot passes**, **3 reuse encoded plot batches** and **3 complete frame publications**; `timing_gate` remains **false**. Performance suites finished in **18.648s Linux / 22.643s macOS / 37.317s Windows**, below the unchanged **120-second** timeout. These same-fixture comparisons include collection overhead and support a measured benefit for the recorded request sequence; they do not establish general slider/GPU performance or cross-machine speedups.

### Actual deployed packages and complete GUI reports

Both `package-validation-Windows` and `package-validation-macOS` manifests report **`passed: true`**, **`sdk_paths_removed: true`**, and six successful modes: GPU GUI, GPU diagnostic, CPU GUI, CPU diagnostic, Sony native CLI and develop CLI. Every mode returned zero; every GUI/diagnostic mode reported **0 QML runtime errors**. GPU diagnostics retain `gpu-compute` producer identity, while active-brush research tests retain their explicit CPU-reference-upload route.

| Deployed package | Path | GUI elapsed | Original deadline | Full Scopes pixels | PNG captures |
| --- | --- | ---: | ---: | ---: | ---: |
| Windows x64 | CPU | **53,773ms** | **60,000ms** | **10,340,960** | **22/22 successful** |
| Windows x64 | Direct3D GPU | **37,157ms** | **60,000ms** | **10,340,960** | **22/22 successful** |
| macOS ARM64 | CPU | **32,214ms** | **60,000ms** | **10,340,960** | **22/22 successful** |
| macOS ARM64 | Metal GPU | **29,888ms** | **60,000ms** | **10,340,960** | **22/22 successful** |

All four reports record `source_commit: fb56bd6`, `smoke_passed: true` and **one encoded plot batch counting exactly 10,340,960 pixels**. Their `analysis_frame_cache` snapshots have budget **335,544,320 bytes**, peak charge **82,727,728 bytes**, final charged/cached bytes **24,624**, **one hit**, and one latest entry. Building bytes, leased estimate, in-flight entries, bypasses of every kind, allocation failures, cancelled operations, invalid requests and stale publications are all **zero**. The final small entry does not represent the large frame's peak charge.

| Deployed package/path | Publications | Misses | Cache-ownership evictions | Encoded plot passes / pixels |
| --- | ---: | ---: | ---: | ---: |
| Windows CPU | 6 | 6 | 5 | 1 / 10,340,960 |
| Windows GPU | 6 | 6 | 5 | 1 / 10,340,960 |
| macOS CPU | 5 | 5 | 4 | 1 / 10,340,960 |
| macOS GPU | 5 | 5 | 4 | 1 / 10,340,960 |

### Histogram to first-plot color-work evidence

The actual full-resolution `cpu_scope_plots_encoded` batch in **each** deployed report contains **zero `cpu_pipeline` calls and zero geometry/preparation stages**. Its preceding exact histogram still performs the expected 21 color-strip calls. Associating the recorded stage UTC completion times and durations gives:

| Deployed package/path | Preceding histogram duration (ms) | Histogram color calls | Sum of color-call durations (ms) | First encoded three-plot batch (ms) | Color / geometry-preparation calls in plot batch |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows CPU | 6378.6161 | 21 | 6284.6971 | 2079.9662 | **0 / 0** |
| Windows GPU | 6254.7004 | 21 | 6162.7624 | 2078.8414 | **0 / 0** |
| macOS CPU | 4085.421291 | 21 | 4014.554919 | 653.942459 | **0 / 0** |
| macOS GPU | 4197.151584 | 21 | 4109.551165 | 653.950916 | **0 / 0** |

The recorded plot windows contain only encoded plot counting and the enclosing plot-cache timing. Combined with exact output checks, a completed-frame hit and the encoded pixel counter, this establishes the intended histogram-to-first-plot reuse in the actual packages. It is stronger evidence of removed duplicate color work than a comparison of whole GUI elapsed times.

### Deployed screenshot timings

| Deployed package/path | Successful entries | Grab total (ms) | PNG save total (ms) | Maximum single grab (ms) | Maximum single save (ms) |
| --- | ---: | ---: | ---: | ---: | ---: |
| Windows CPU | 22/22 | 459.4402 | 1781.8542 | 41.212, export naming invalid | 127.8489, crop |
| Windows GPU | 22/22 | 808.9297 | 2058.0580 | 89.7318, import dialog English | 182.8231, crop |
| macOS CPU | 22/22 | 250.860500 | 2290.678375 | 34.816041, presets | 202.046125, parade |
| macOS GPU | 22/22 | 273.169875 | 2226.673253 | 42.401792, presets | 196.500167, parade |

Screenshot save time includes the existing PNG encoding and file I/O. Cache charges exclude source/geometry images and other process allocations; the 320 MiB cache budget remains distinct from total process memory. **Differences in GUI wall-clock time across CI runs are not attributed to this cache change**: host load, preparation, rendering, presentation and I/O vary. The controlled paired samples and direct zero-color plot windows above provide the performance evidence for this stage.

Acceptance applies only to the exact implementation tree above; later changes require their own unchanged three-platform/package checks. Active-brush size/work and enabled-persistence gates remain unchanged.

## Acceptance evidence

| Validation | Status |
| --- | --- |
| Component tests, local Qt 6.8.3 Release | **Passed:** five targeted slots, QtTest total **7 passed / 0 failed / 0 skipped**, 5 ms including init/cleanup. This verifies the component tests, not full application acceptance. |
| Six integration tests, local Qt 6.8.3 Release | **Passed:** six targeted slots, QtTest total **8 passed / 0 failed / 0 skipped**, 829 ms including init/cleanup. |
| Initial full local CTest, including controller diagnostic, real Sony/DNG, Metal GPU parity and brush research regressions | **Passed 10/10 in 78.32 s:** **203 Core / 36 Metal GPU / 38 performance**. This run preceded addition of the paired performance slot. |
| New paired performance case | **Passed 3/0/0 in 4,251 ms**, with exact results and the three-sample measurements above. No timing gate was added. |
| Final combined full local CTest including the new performance case | **Passed 10/10 in 77.61 s:** **203 Core / 36 real Metal GPU / 39 performance**, real Sony fixtures, zero failed or skipped. The original 120 s suite timeout is unchanged. |
| Quiet local CPU and native Cocoa/Metal GUI | **Passed:** both 22 successful screenshots, complete 10,340,960-pixel scopes, one encoded plot batch, zero QML markers and elapsed time below the unchanged 60,000 ms gate; evidence above. |
| Three-platform final-tree CI and actual deployment packages | **Passed at `62d64d11dfd5d1201bfb943d9e41a968c18bdc80`, CI #40285937542:** equal implementation/merge trees, three-platform 203 Core / 36 GPU / 39 performance, CTest 10/10, zero skips and both actual six-mode deployed packages; exact evidence above. |
| Final Windows/deployment performance comparison | **Recorded:** three-platform paired samples and four deployed zero-color encoded-plot windows above. These establish benefit/reuse for the measured requests, without attributing cross-run GUI wall-clock variation to this implementation. |

An early local app run overlapped compilation and is excluded from performance evidence. Acceptance must use the final integrated tree and retain the original pixel, provenance, cancellation, public-camera-fixture and GUI assertions.
