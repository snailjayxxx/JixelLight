# F4 M4.4B.4 — request-local brush candidate index

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. This candidate follows M4.4B.3 accepted at `a2520654e2914184b6d77635b75562f0a7f81f8e`. M4.4B.4 acceptance and timing evidence remain pending this implementation's own exact-HEAD Windows/macOS/Linux CI.

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

## Acceptance gate

Do not record M4.4B.4 as accepted until its own exact code tree passes all unchanged Windows x64/macOS ARM64/Linux GPU gates: Core, mandatory GPU with **40/65535** tolerance, complete **1024-bin Scopes**, checksum-pinned public Sony RAW/JPEG, native GUI smoke, ICC, sensor/refusal/readiness checks and both actual deployed packages. The original real-RAW CPU GUI **60,000ms** deadline and performance-correctness **120-second** timeout must not be relaxed. Record exact commit/merge-tree equivalence, per-platform check totals, eight workload timing rows, index bytes/counts and deployed-package evidence after that run completes.

Further large-frame/render/export/editor integration remains future work; F0–F6 is not complete.
