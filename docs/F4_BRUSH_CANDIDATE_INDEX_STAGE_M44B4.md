# F4 M4.4B.4 — request-local brush candidate index

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. M4.4B.4 is accepted at exact HEAD `b8f371c0cb56ec23e6969ea67b63c1a3504d0d0a` by [CI #40275215812](https://github.com/snailjayxxx/JixelLight/actions/runs/40275215812), following accepted M4.4B.3.

## Implementation

The accepted M4.4B.3 path prunes zero-candidate tiles but still scans the entire brush document when rasterizing each remaining tile. M4.4B.4 prepares an index once per research frame: each active dab's clipped integer candidate rectangle maps directly to intersecting 128×128 tiles. Every nonzero dab/tile intersection retains a compact layer/stroke/dab reference. Insertion follows original document order, so the renderer preserves dab maximum, stroke source-over union and layer EV addition order. Candidate visit counts still include bounding-box pixels outside each circle, using the same bounds and coverage arithmetic as the original mask-only/RGBA oracle.

`FramePlan` owns an immutable document, geometry, tile/count and index snapshot. `exposureOnly(plan, tileIndex, ...)` consumes that snapshot rather than consulting the caller's current document. Copies share only the immutable prepared data; caller document edits, destruction and public tile-summary edits cannot redirect trusted candidates. The selected public tile summary and total visits must still agree with the snapshot before output is touched. The real CPU `processColorFrame` uses this plan API for nonempty tiles; empty tiles and all-empty frame behavior remain as accepted in M4.4B.3.

Index storage has a separate ceiling of **1,048,576 references**. A lower caller budget may force the original document scan; zero disables indexing. If reference count exceeds the clamped budget, the plan releases all index capacity and keeps planning exact work/counts for that scan fallback. This does not enlarge or reduce the accepted render-request work limits. `hasCandidateIndex()` reports the selected path; `candidateReferences()` reports actual nonzero intersections even after fallback; `candidateIndexBytes()` reports allocated index capacity, including per-tile vector storage, and is zero for fallback. Vector spare capacity means this byte count is distinct from reference count, and it excludes the owned document and tile snapshot. New cumulative CPU diagnostics record these references, capacity bytes and fallback requests.

The **2,097,152 requested-pixel**, **8,388,608 per-tile candidate-visit** and **100,663,296 per-frame candidate-visit** limits remain. Document limits remain 16 layers, 64 strokes per layer, 4,096 dabs per stroke and 16,384 total dabs. Enabled project/XMP/preset/Controller/GUI/CLI brush documents remain refused. QRhi active-brush display/scopes still reports explicit CPU-reference-upload provenance. This stage does not enable production large-RAW painting, export or a persistent brush editor.

## Verification and measurements

The original four workloads and `brush-workload` / `brush-frame-pruning` output rows remain for historical comparison: sparse landscape, overlap portrait, dense viewport and completely empty viewport. Each keeps the old mask-only and RGBA32F oracle comparison. The new `brush-candidate-index` row reports planning, indexed mask-only and unindexed mask-only three-sample median elapsed times, document dab count, candidate references and actual index capacity bytes. Whole CPU-frame timing remains a separate three-sample median. Timings provide evidence without a cross-machine hard speed threshold or an older-binary speedup claim.

Four additional 512×384 fixtures exercise document scanning costs within the unchanged 120-second CTest timeout:

| Workload | Total dabs | Strokes per layer | Visibility |
| --- | ---: | ---: | --- |
| Distributed sparse 4K | 4,096 | 16 | Distributed over the full frame |
| Distributed sparse 16K | 16,384 | 64 | Distributed over the full frame |
| Mostly offscreen 4K | 4,096 | 16 | Eight dabs per layer in a panned viewport |
| Mostly offscreen 16K | 16,384 | 64 | Eight dabs per layer in a panned viewport |

All eight fixtures compare indexed EV planes and visited counts **bit-exactly** against the original document `exposureOnly` and RGBA oracle. A zero-budget plan additionally checks fallback output, reference counts and released capacity. Full CPU frames must be deterministic and preserve exact cumulative visit/pruned/rasterized counters, while pre-cancelled indexed exposure and full-frame requests publish no partial output. The original all-empty viewport remains identical to normal color processing. Core regressions separately cover document snapshot ownership, ROI/viewport and malformed/changed-plan safety, work budgets and cancellation.

## Local verification at b8f371c

The exact committed code built with Qt 6.8.3 Release on Apple M2/macOS 26.5. Full local CTest passed **10/10 in 86.90 s** with mandatory real Sony files and actual Metal GPU enabled: **178 Core / 35 GPU / 36 performance** checks, zero skips. The new 16K-dab viewport passed all three output spaces with maximum CPU/GPU RGB difference **0/65535**, exact alpha and scopes. Native mask-canvas smoke passed in **914 ms**. The real public DNG CPU GUI smoke passed in **13,728 ms**, retaining the original **60,000 ms** deadline and complete **10,340,960**-pixel histogram.

The local toolchain uses user-installed LibRaw 0.22.2/Exiv2 0.28.9 and an SDK compatibility configuration for this host; it is not a deployed distribution package and does not replace fixed-dependency three-platform CI. Within the same local process, the distributed-16K indexed/unindexed mask medians were **3.289 / 9.015 ms**, and mostly-offscreen-16K **0.481 / 8.833 ms**. Whole serial CPU frames were **19.770 / 16.112 ms**, respectively. These isolated measurements do not compare to an older binary or establish production large-RAW performance; small-document indexed masks were slightly slower than the unindexed oracle in this run.

## Exact-HEAD CI acceptance

[CI #40275215812](https://github.com/snailjayxxx/JixelLight/actions/runs/40275215812) completed **Success** on Windows x64, macOS ARM64 and Linux GPU. The run records HEAD `b8f371c0cb56ec23e6969ea67b63c1a3504d0d0a`; checkout tested merge `fdc24a6594efd063c9d073310a9e0d84ea6bdeeb`, whose tree `08009e6cf0ff7657551bd0a84c5167ba0e1cacc4` equals that HEAD's tree. Every platform passed **CTest 10/10**, **178 Core / 35 mandatory GPU / 36 performance** checks with zero failed or skipped checks. Performance suites completed in **4.237s Linux / 5.170s macOS / 8.786s Windows** under the original 120-second timeout.

The 16,384-dab viewport regression reported maximum CPU/GPU RGB error **0/65535** in **sRGB, Display P3 and ProPhoto RGB** on all three platforms, with exact alpha, complete 1024-bin Scopes and explicit CPU-reference-upload provenance. The unchanged limit remains **40/65535**. Public checksum-pinned Sony RAW/JPEG, native canvas pointer smoke, ICC and sensor/refusal/readiness checks all passed.

Three-sample median elapsed-work timings from this run, in milliseconds:

| Platform | Workload | Plan | Indexed mask | Unindexed mask | Whole CPU frame |
| --- | --- | ---: | ---: | ---: | ---: |
| Linux | Sparse landscape | 0.002936 | 0.619867 | 0.702243 | 26.3543 |
| Linux | Overlap portrait | 0.020317 | 14.7400 | 14.1348 | 41.3311 |
| Linux | Dense viewport | 0.024566 | 28.0836 | 26.2524 | 135.337 |
| Linux | Empty viewport | 0.002355 | 0.256727 | 0.677267 | 24.8189 |
| Linux | Distributed sparse 4K | 0.176981 | 3.35687 | 4.11280 | 28.8182 |
| Linux | Distributed sparse 16K | 0.685801 | 10.2565 | 14.5081 | 36.2037 |
| Linux | Mostly offscreen 4K | 0.055673 | 0.528977 | 3.83154 | 25.4984 |
| Linux | Mostly offscreen 16K | 0.225380 | 0.557419 | 14.0729 | 25.7711 |
| macOS | Sparse landscape | 0.003291 | 0.662167 | 0.759834 | 24.2370 |
| macOS | Overlap portrait | 0.015125 | 6.35371 | 6.92238 | 27.7827 |
| macOS | Dense viewport | 0.019000 | 14.6668 | 13.6431 | 123.164 |
| macOS | Empty viewport | 0.004541 | 0.499916 | 0.778627 | 18.2861 |
| macOS | Distributed sparse 4K | 0.120417 | 2.61021 | 4.48337 | 25.2903 |
| macOS | Distributed sparse 16K | 0.411375 | 5.41967 | 13.8079 | 25.5245 |
| macOS | Mostly offscreen 4K | 0.028417 | 0.843832 | 3.62071 | 25.7945 |
| macOS | Mostly offscreen 16K | 0.097417 | 0.783498 | 12.0627 | 24.9351 |
| Windows | Sparse landscape | 0.008100 | 2.46230 | 3.37320 | 69.6890 |
| Windows | Overlap portrait | 0.033100 | 15.6364 | 15.4974 | 89.9389 |
| Windows | Dense viewport | 0.041300 | 34.4418 | 34.7507 | 343.522 |
| Windows | Empty viewport | 0.004600 | 1.64950 | 2.63470 | 67.5513 |
| Windows | Distributed sparse 4K | 0.276700 | 3.81060 | 6.67020 | 75.4052 |
| Windows | Distributed sparse 16K | 1.155900 | 6.25300 | 18.8647 | 76.5828 |
| Windows | Mostly offscreen 4K | 0.123700 | 2.11570 | 6.56990 | 69.0090 |
| Windows | Mostly offscreen 16K | 0.536600 | 1.82580 | 17.9421 | 69.6748 |

Exact index and tile counts verified by all three platforms:

| Workload | References | Index capacity bytes Linux/macOS | Index capacity bytes Windows | Pruned/rasterized tiles |
| --- | ---: | ---: | ---: | ---: |
| Sparse landscape | 20 | 408 | 420 | 8/4 |
| Overlap portrait | 482 | 4,608 | 3,660 | 0/12 |
| Dense viewport | 432 | 4,380 | 4,098 | 0/48 |
| Empty viewport | 0 | 288 | 288 | 12/0 |
| Distributed sparse 4K | 3,413 | 37,152 | 23,040 | 0/12 |
| Distributed sparse 16K | 13,653 | 147,744 | 115,416 | 0/12 |
| Mostly offscreen 4K | 18 | 408 | 402 | 8/4 |
| Mostly offscreen 16K | 18 | 408 | 402 | 8/4 |

The byte differences reflect standard-library vector capacity growth. Indexed masks are not uniformly faster in the overlap/dense fixtures; all rows are retained. These measurements include planning and mask execution separately and retain full CPU-frame results, without claiming presentation latency, physical GPU throughput, older-binary speedup or production large-RAW performance.

Both actual deployed packages passed all six GPU/CPU/diagnostic/Sony CLI/develop CLI checks with SDK paths removed and **0 QML runtime errors**. Packaged real-RAW CPU GUI elapsed time was **59,593ms Windows / 36,891ms macOS**, below the unchanged **60,000ms** deadline. Packaged GPU GUI was **42,443ms / 37,092ms**, respectively. Each full GUI run counted **10,340,960 Scopes pixels**. The run's `package-validation-Windows` and `package-validation-macOS` artifacts retain the exact smoke reports and successful manifests.

Acceptance applies only to this exact code tree; subsequent implementation commits require their own unchanged three-platform checks. Research limits and enabled persistence refusal remain in force.

Further large-frame/render/export/editor integration remains future work; F0–F6 is not complete.
