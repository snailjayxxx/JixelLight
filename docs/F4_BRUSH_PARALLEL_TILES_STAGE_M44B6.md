# F4 M4.4B.6 — bounded parallel CPU brush tiles

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Accepted at implementation HEAD `5c6667864d284c4e4bdf084f55eafe680dc907b5` by [CI #40282628790](https://github.com/snailjayxxx/JixelLight/actions/runs/40282628790), including all three platforms and both actual deployed packages. Local verification is retained separately below; later implementation commits require their own acceptance.

## Change

The nonzero-candidate research brush path now distributes complete color tiles between the calling thread and helpers from the existing `ParallelRows` pool. One atomic `fetch_add(1)` claims one tile at a time. This avoids assigning a 32-tile row batch to one worker when a frame contains fewer than 32 tiles, and allows sparse and expensive tiles to be claimed dynamically. The tile grid, candidate pruning, original dab/stroke/layer arithmetic and final pixel order do not change.

Helpers are eligible only when all of the following hold:

- the caller requested parallel processing;
- `ParallelRows::threadBudget()` is greater than one;
- the request contains more than one tile;
- processed-region pixels plus planned candidate-pixel visits are at least **262,144**.

For `processRegion`, processed-region pixels include the existing clipped detail halo. This scheduling threshold is separate from the unchanged research admission/work limits.

The implementation attempts at most `min(threadBudget - 1, tileCount - 1)` helpers with `tryStart`. A rejected task is destroyed rather than queued. The caller also runs the tile-claim loop, then joins every successfully submitted helper with a semaphore. Pool saturation therefore permits caller-only completion without waiting for a queued helper behind unrelated blocked work. A successful submission does not guarantee that its helper claims a tile before the caller or another helper finishes the request. Helpers may also have claimed all tiles before the caller's loop begins; caller participation does not guarantee that it processes at least one tile.

## Shared inputs and output ownership

Each request still prepares one immutable `FramePlan` snapshot and one immutable color kernel before workers start. Workers read its private candidate index/document and the already-acquired source pointer/stride. The output image is allocated and detached once before worker submission. Workers receive its raw destination pointer/stride and write disjoint tile rectangles; no worker calls a QImage detach or mutable scan-line API. The caller retains all plan, kernel, image and stack storage until every submitted helper has joined.

Each nonempty tile creates its own mask-only scratch. A proven zero-candidate tile skips mask rasterization while still processing every color pixel. A completely zero-candidate frame retains the historical row renderer; it does not enter the new brush scheduling counters. No full-frame exposure plane, per-worker document copy, cross-request index cache or per-worker color image is added.

## Failure and cancellation

A request-local failure flag stops subsequent claims and prevents partially written output from escaping. Tile-render/allocation exceptions are caught within the worker boundary. Helper-creation/submission exceptions also mark the request failed, after which the caller still joins the helpers already accepted by the pool. A worker already inside coverage rasterization retains its existing caller-token cancellation checks; a local failure does not promise instantaneous interruption of another tile's coverage calculation.

After joining, cancellation, a local failure or a total-visit mismatch returns a null image. The caller-owned cancellation token is never set merely to broadcast a local render failure, so an invalid request cannot poison an independent request sharing that token or a later render. Detail finishing runs only after the tile workers have joined. Source snapshots remain unchanged.

## Diagnostics

Existing tile/pruned/rasterized/visit/reference/index-capacity/fallback counts remain. After all color tiles complete successfully and exact total visits agree, the nonzero-candidate branch records:

| Counter | Meaning |
| --- | --- |
| `brush_research_helper_tasks` | Number of helper tasks accepted by `tryStart` for that successful color-tile pass |
| `brush_research_parallel_frames` | One when at least one helper was submitted, otherwise zero |
| `brush_research_serial_frames` | One when no helper was submitted, otherwise zero |

Serial processing, budget one, a small/single-tile request or pool saturation can produce zero submitted helpers. A completely empty-candidate frame leaves all three new counters unchanged, even if its historical row renderer uses helpers. Failed/cancelled tile passes join their tasks but do not publish these counts.

The counts are recorded **before** `finishColor`, as are the existing brush tile counts. They describe completion of the color-tile pass, not final image publication, display presentation or native GPU execution; a later detail failure/cancellation does not roll them back. Submission counters alone do not prove concurrent useful work or a performance improvement.

## Memory accounting

A tile contains at most **128 × 128 = 16,384 pixels**. Reviewing the mask-only `exposureOnly` scopes gives the following maximum simultaneously live numeric element payloads for one active tile:

| Phase | Live arrays | Maximum payload |
| --- | --- | ---: |
| Coverage rasterization | Three double planes: accumulated EV, layer alpha, stroke alpha | **393,216 bytes = 384 KiB** |
| EV conversion | One double EV plane plus one float EV plane | **196,608 bytes = 192 KiB** |
| Color application after coverage returns | One float EV plane | **65,536 bytes = 64 KiB** |

The two alpha planes have left scope before the float output plane is allocated. Counting all three double planes and the float plane simultaneously as 448 KiB would overstate the live numeric element payload in this implementation. Both the indexed path and the original scan fallback have the same three-double-plane peak. A zero-candidate tile allocates none of those planes.

With `W = 1 + submittedHelpers`, at most `W` tiles are executing for this request, and `W <= min(threadBudget, tileCount)`. Its peak simultaneous coverage element payload is therefore at most `W × 384 KiB`: **3 MiB at budget eight**, or **12 MiB at the configurable maximum budget 32**. These are element-payload bounds, not exact allocated-byte or process-RSS bounds: vector spare capacity, allocator bookkeeping, task/stack storage, the shared document/index/tile metadata, source/output images and other requests are excluded. Releasing a vector also need not immediately return memory to the OS allocator.

The full RGBA64 output remains a single request allocation (up to **16 MiB** at the unchanged 2 Mi-pixel research cap). Existing input conversion and subsequent full-image detail buffers are separate from per-worker mask scratch. Workers join and destroy their tile scratch before detail finishing starts. Multiple callers still have their own request images and caller scratch; sharing the helper pool is not a process-wide image-memory limit.

## Tests and measurements

Core regressions cover exact serial/parallel whole-frame and ROI output for a **541×509 FP32 HDR/alpha** source, full/panned normalized viewports, nonzero Sony detail halos, partial edge tiles and thread budgets **1/2/8**. A representative request contains fewer than 32 tiles. Isolated pool capacity plus submission counters demonstrate that eligible requests accept helpers; exact visit/reference/prune/raster counts and source fingerprints remain unchanged. Separate tests cover runtime EV refusal without setting an independent cancellation token, in-flight cancellation alongside healthy requests, recovery and a deliberately saturated shared pool that must finish on the caller before blockers are released.

Performance tests retain all eight accepted indexed/unindexed/RGBA-oracle workloads and their existing `brush-workload`, `brush-candidate-index` and serial `brush-frame-pruning` rows. Two **1024×768** sparse/overlap workloads are added. Each of the ten rows records three serial whole-frame samples and three parallel-request samples, checks exact image/count identity, and emits `brush-frame-parallel` with configured thread budget, scheduling eligibility, actual helper/frame counts and both medians. Budget one or a busy pool may legitimately record no helper. There is no portable speed threshold or assertion that a parallel request is faster.

The new GPU regression exercises a **541×509 FP32 HDR/negative-RGB/mixed-alpha** source in full and panned viewports across sRGB, Display P3 and ProPhoto RGB. CPU serial/parallel images must match exactly. The original **40/65535** CPU/GPU bound, exact alpha, complete **1024-bin Scopes** and explicit `cpu-reference-upload` provenance remain mandatory. This validates the CPU fallback displayed/statistically consumed by QRhi; it does not implement native GPU brush compute.

## Local working-tree verification

Local Qt 6.8.3 Release compilation passed. The three targeted Core regressions passed in **1.235 s**. The added GPU regression ran on actual **Metal / Apple M2**, passed in **0.548 s**, and reported maximum RGB error **0/65535** for all six full/panned-viewport × output-space combinations, exact alpha and full Scopes. The mandatory bound remains 40/65535 and the producer remains `cpu-reference-upload`.

Initial complete local CTest passed **10/10 in 76.44 s**, with **192 Core / 36 mandatory Metal GPU / 38 performance** checks, checksum-pinned public Sony RAW/JPEG and zero skips. These results verify the working tree locally; they are not exact-tree three-platform or deployed-package acceptance.

The cancellation regression was subsequently strengthened to start the heavy request alone and observe an active shared-pool helper before launching the independent requests and cancelling. This targeted check passed in **0.479 s**, covering an active helper rather than only the serial planning/source-validation preamble. The final complete-suite rerun after that test change passed **10/10 in 72.58 s**, again **192 Core / 36 Metal GPU / 38 performance**, real Sony and zero skips.

A separate explicitly single-threaded run (`JIXELLIGHT_CPU_THREADS=1`) passed all ten workload rows in **2.673 s**, exercising caller-only scheduling and its unchanged counters. The ten-workload default-budget targeted run passed in **2.147 s**. It used the local default thread budget **six**. The table retains separate three-sample medians in milliseconds for serial processing and a parallel-request flag; the latter can legitimately execute with no helper. Helper/frame counts are totals across the three parallel-request samples.

| Workload | Serial median | Parallel-request median | Eligible | Helpers | Parallel passes | Serial passes |
| --- | ---: | ---: | --- | ---: | ---: | ---: |
| Sparse landscape | 16.7887 | 16.7908 | No | 0 | 0 | 3 |
| Overlap portrait | 21.3771 | 6.87154 | Yes | 15 | 3 | 0 |
| Dense viewport | 77.5178 | 19.2706 | Yes | 15 | 3 | 0 |
| Empty viewport | 15.9744 | 15.6900 | No | 0 | 0 | 0 |
| Distributed sparse 4K | 17.3652 | 17.3474 | No | 0 | 0 | 3 |
| Distributed sparse 16K | 19.5222 | 19.4617 | No | 0 | 0 | 3 |
| Mostly offscreen 4K | 16.0177 | 16.0423 | No | 0 | 0 | 3 |
| Mostly offscreen 16K | 16.7307 | 16.1851 | No | 0 | 0 | 3 |
| Parallel sparse landscape, 1024×768 | 64.0201 | 14.4761 | Yes | 15 | 3 | 0 |
| Parallel overlap landscape, 1024×768 | 83.8639 | 19.5208 | Yes | 15 | 3 | 0 |

The empty viewport has no new pass counts because it follows the established zero-candidate row renderer. All rows retained exact images and tile/work/index counters. These measurements describe this run's elapsed CPU work; they do not establish a cross-machine, production-RAW, presentation/GPU or earlier-binary performance improvement.

Local evidence files are `b6-core-targeted.txt`, `b6-gpu-targeted.txt`, `b6-performance-targeted.txt` and `b6-full-tests.txt` in the local JixelLight build evidence directory.

## Exact-HEAD CI acceptance

[CI #40282628790](https://github.com/snailjayxxx/JixelLight/actions/runs/40282628790) completed **Success** on Windows x64, macOS ARM64 and Linux GPU. The run records implementation HEAD `5c6667864d284c4e4bdf084f55eafe680dc907b5`; checkout tested merge `806a4793d376691da1170a7b5495d970794d7ba9`. Both commits have tree **`74995e4afe429d331d558da2e0fa9dcec374c1c2`**, so the tested code tree equals this implementation's tree.

Every platform passed **CTest 10/10**, **192 Core / 36 mandatory GPU / 38 performance** checks, with **zero failed or skipped checks**. All three new Core regressions passed on all platforms: exact whole/ROI/viewport/detail serial/parallel pixels; runtime-failure isolation and cancellation after observing an active helper; and caller completion with a saturated shared pool. The new GPU regression passed all **six full/panned viewport × sRGB/Display P3/ProPhoto RGB** combinations at maximum RGB error **0/65535**, retaining exact alpha, complete **1024-bin Scopes** and explicit **`cpu-reference-upload`** provenance. The existing 16K-dab viewport likewise retained **0/65535** error in all three output spaces. The mandatory bound remains **40/65535**.

The original five FP32 JPEG and two Scopes preparation-scheduling regressions, checksum-pinned public Sony RAW/JPEG, native canvas pointer smoke, ICC and sensor/refusal/readiness checks also passed. The performance suite completed in **6.738s Linux / 7.945s macOS / 12.226s Windows**, under the unchanged **120-second** timeout.

## Three-platform workload measurements

Every row below retains three serial and three parallel-request samples. Times are median whole CPU-frame elapsed milliseconds. Helper and frame counts are totals across the three parallel-request samples. Linux and Windows used thread budget **two**. Their four eligible rows each accepted **one helper per sample**, recording **three helper tasks / three parallel passes / zero serial passes**. All other nonempty rows ran on the caller.

The macOS CI job used thread budget **one**: all ten requests were ineligible for helpers and recorded **zero helper tasks**. Its parallel-request medians therefore measure the caller-only path and do not demonstrate parallel execution or parallel speedup. Independently, the Core regressions passed explicit budgets **1/2/8**, including observed helper submission and the strengthened active-helper cancellation case, on each platform.

| Platform | Workload | Budget | Serial median | Parallel-request median | Eligible | Helpers | Parallel passes | Serial passes |
| --- | --- | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| Linux | Sparse landscape | 2 | 25.8152 | 25.2566 | No | 0 | 0 | 3 |
| Linux | Overlap portrait | 2 | 40.6293 | 21.1359 | Yes | 3 | 3 | 0 |
| Linux | Dense viewport | 2 | 133.903 | 66.5006 | Yes | 3 | 3 | 0 |
| Linux | Empty viewport | 2 | 24.924 | 24.8859 | No | 0 | 0 | 0 |
| Linux | Distributed sparse 4K | 2 | 28.1351 | 28.1579 | No | 0 | 0 | 3 |
| Linux | Distributed sparse 16K | 2 | 35.5126 | 35.1389 | No | 0 | 0 | 3 |
| Linux | Mostly offscreen 4K | 2 | 25.1605 | 25.1252 | No | 0 | 0 | 3 |
| Linux | Mostly offscreen 16K | 2 | 25.3513 | 25.464 | No | 0 | 0 | 3 |
| Linux | Parallel sparse landscape, 1024×768 | 2 | 100.892 | 50.0906 | Yes | 3 | 3 | 0 |
| Linux | Parallel overlap landscape, 1024×768 | 2 | 158.03 | 79.0379 | Yes | 3 | 3 | 0 |
| macOS | Sparse landscape | 1 | 27.0459 | 25.5574 | No | 0 | 0 | 3 |
| macOS | Overlap portrait | 1 | 39.843 | 40.6076 | No | 0 | 0 | 3 |
| macOS | Dense viewport | 1 | 151.956 | 122.442 | No | 0 | 0 | 3 |
| macOS | Empty viewport | 1 | 20.1394 | 25.4991 | No | 0 | 0 | 0 |
| macOS | Distributed sparse 4K | 1 | 23.428 | 27.3015 | No | 0 | 0 | 3 |
| macOS | Distributed sparse 16K | 1 | 39.7863 | 28.3768 | No | 0 | 0 | 3 |
| macOS | Mostly offscreen 4K | 1 | 24.1306 | 23.4004 | No | 0 | 0 | 3 |
| macOS | Mostly offscreen 16K | 1 | 26.2665 | 27.4196 | No | 0 | 0 | 3 |
| macOS | Parallel sparse landscape, 1024×768 | 1 | 113.945 | 98.1293 | No | 0 | 0 | 3 |
| macOS | Parallel overlap landscape, 1024×768 | 1 | 120.193 | 113.291 | No | 0 | 0 | 3 |
| Windows | Sparse landscape | 2 | 60.2033 | 60.2084 | No | 0 | 0 | 3 |
| Windows | Overlap portrait | 2 | 76.4194 | 38.8753 | Yes | 3 | 3 | 0 |
| Windows | Dense viewport | 2 | 294.31 | 154.786 | Yes | 3 | 3 | 0 |
| Windows | Empty viewport | 2 | 59.0878 | 58.7836 | No | 0 | 0 | 0 |
| Windows | Distributed sparse 4K | 2 | 63.0313 | 62.7479 | No | 0 | 0 | 3 |
| Windows | Distributed sparse 16K | 2 | 65.8734 | 66.0993 | No | 0 | 0 | 3 |
| Windows | Mostly offscreen 4K | 2 | 59.9302 | 59.9385 | No | 0 | 0 | 3 |
| Windows | Mostly offscreen 16K | 2 | 61.7869 | 60.5425 | No | 0 | 0 | 3 |
| Windows | Parallel sparse landscape, 1024×768 | 2 | 238.503 | 121.393 | Yes | 3 | 3 | 0 |
| Windows | Parallel overlap landscape, 1024×768 | 2 | 294.989 | 162.853 | Yes | 3 | 3 | 0 |

The empty viewport leaves the new scheduling counters unchanged because it uses the accepted zero-candidate row renderer. All ten workloads retained exact serial/parallel images and exact tile/visit/reference/index/pruning counters; the original indexed/unindexed/RGBA-oracle rows remain in the run's performance reports. Submitted helpers do not by themselves prove which worker processed a particular tile. These measurements describe this run's elapsed CPU work; they do not establish cross-machine, production-RAW, presentation/GPU, older-binary or general speedup claims.

## Actual deployed-package acceptance

Both `package-validation-Windows` and `package-validation-macOS` artifacts report **`passed: true`**, **`sdk_paths_removed: true`** and six successful checks: GPU GUI, GPU diagnostic, CPU GUI, CPU diagnostic, Sony native CLI and develop CLI. Every check returned zero; all four GUI/diagnostic checks in each manifest report **0 QML runtime errors**. The GPU diagnostic producer is `gpu-compute`; this ordinary package diagnostic is separate from active-brush tests' explicit CPU-reference-upload route.

| Deployed package | CPU GUI elapsed | GPU GUI elapsed | Original deadline | Full Scopes pixels | Source commit |
| --- | ---: | ---: | ---: | ---: | --- |
| Windows x64 | **54,329ms** | **37,345ms** | **60,000ms** | **10,340,960** | `806a479` |
| macOS ARM64 | **38,109ms** | **36,384ms** | **60,000ms** | **10,340,960** | `806a479` |

All four full GUI reports have `smoke_passed: true`; each contains **22 successful screenshot timing entries**. Packaged GPU backends were Direct3D 11 / Microsoft Basic Render Driver on Windows and Metal / Apple Paravirtual device on macOS. The real-RAW GUI smoke uses **disabled brush documents**, so differences in its wall-clock time from earlier CI runs cannot be attributed to B6's active-brush tile scheduling. Host load, scheduling and the rest of the GUI workload also vary between runs; these package times serve the unchanged acceptance deadline, not a causal B6 benchmark.

Acceptance is limited to the implementation tree above. The original 60,000ms real-RAW GUI deadline, 120-second performance timeout, 40/65535 GPU bound, exact alpha, complete 1024-bin Scopes, real Sony and actual-package gates were not relaxed.

## Unchanged production gate

The **2,097,152 logical research pixels**, **8,388,608 per-tile candidate visits** and **100,663,296 complete-operation candidate visits** remain. B5 still charges the real expanded strips, including repeated halos, before streamed scopes/export processing. The independent 1,048,576-reference index ceiling and scan fallback remain. Document limits, enabled project/history/XMP/preset/Controller/GUI/CLI persistence refusal and all disabled-draft behavior remain unchanged.

B6 does not admit **24 MP / 33 MP** enabled-brush originals, enable a persistent drawing editor, share prepared brush plans across streamed calls or reuse complete analysis-color frames. Large-frame memory/work admission, production export/editor integration and native GPU brush work remain future stages. F0–F6 is not complete.
