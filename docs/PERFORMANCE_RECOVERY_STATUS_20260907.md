# Performance implementation recovery checkpoint

Date: 2026-09-07 (Asia/Tokyo)

## Verified remote baseline

- Repository: `snailjayxxx/JixelLight`.
- `main`: `f0f43472c5fbb0c6e3e4eb540a7c06fdb43be90b` (alpha.6).
- Performance branch before this checkpoint: `perf/20260907-a-e`, `490250f8e3ac07a6aa5f4691ff9491692aa25e29`.
- The performance branch currently contains validation infrastructure changes. Its application source still matches the alpha.6 baseline. The A-E application changes from the interrupted local session have NOT been committed here.
- Successful `performance-validation` and `validation-deps` runs collected a source archive and SDK dependencies. Their success is NOT evidence that the new application source or GPU implementation passed CI.

## Interrupted local session: recoverable implementation records

The previous session's implementation commands and test code are available in the conversation record, but the modified working directory is not available in the current execution environment. GUI screenshots are not a source backup and not proof of release readiness. These items are recovery candidates, not features currently present on this branch:

| Area | Implementation recorded in interrupted session | Verification still required after recovery |
|---|---|---|
| A | LatestJob (one running + one replaceable pending request), cancellation/revision checks, background image loading/preparation/rendering, asynchronous batched SQLite writer, debounced edit persistence | Rapid switch/drag race tests; destruction with running jobs; save failure and flush semantics |
| B | ProcessingPlan; precomputed WB/exposure matrix; inactive tonal/HSL work bypass; shared bounded row pool; preserved legacy reference pipeline | Exact serial/parallel equality; numerical comparison against legacy; neutral-node bounds; benchmark with explicit hardware/fixture |
| C | Bounded decoded-source cache; versioned checked disk preview; embedded JPEG as placeholder only; next-image prefetch; viewport preparation | Cache integrity/header checks; invalidation; memory budget; stale partial results; 100% viewport behavior |
| D | Qt 6.8 QRhi compute pipeline and QQuickRhiItem; persistent source/output textures; CPU fallback; frame revision guards | Shader build on supported platforms; CPU/GPU comparison; orientation; source upload reuse; real Metal/Direct3D validation |
| E | 1024-bin RGB/luma GPU partial/reduction histogram; small asynchronous buffer readback; independent preview/full-resolution statistics | Count conservation; exact histogram of actual GPU pixels; stale revision rejection; full-image versus viewport labels |
| Export | Parameter snapshots; bounded export queue; tiled streaming JPEG; direct target-gamut output; ICC embedding; cancellation and atomic destination writes | Do not overwrite ANY imported original; cancellation preserves existing destination; output profile and snapshot correctness |
| Diagnostics | Buffered log sink; explicit flush; performance counters/timings; diagnostic scope revision context | Final record survives flush; log failures visible; diagnostics identify CPU reference capture rather than GPU screenshot |

## Recovery hardening already identified in the prior session

- JPEG optimized Huffman coding should not silently retain whole-image coefficient buffers when promising streaming memory usage.
- Single export must protect all imported originals, not only the selected image; canonical aliases should be checked.
- Diagnostic captures need parameter/statistics revision metadata and must not relabel stale pixels as current.
- Disk-cache integrity must include the header/dimensions, not only payload; include processing/schema/LibRaw version in the cache identity.
- CPU histogram rebinning must explicitly support only exact compatible resolutions (256/512/1024).
- Production GPU and test sources must not be silently omitted using `if(EXISTS ...)` guards.
- Preserve current supported languages and editing behavior; do not claim a release or whole-program speedup from synthetic kernel benchmarks.

## Current interruption

On resumption, local container and Python execution returned repeated `ClientError` responses. No completion status from the final hardening build can be confirmed. No new Windows/macOS package or complete restored source has been verified during this status check.

## Required completion gate

1. Restore the application source in small durable commits, without overwriting unrelated changes.
2. Run a clean build and existing regression tests against the restored commit.
3. Run scheduler/cache/export correctness tests and GPU tests; a skipped GPU test is not a GPU pass.
4. Exercise real RAW decoding and GUI interactions, record the actual commit and backend in reports.
5. Build Windows x64 and macOS arm64 packages, inspect CI results and artifacts.
6. Only then update version/docs and consider merging to main. Keep this checkpoint accurate until the gate is met.
