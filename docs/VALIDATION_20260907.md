# alpha.7 A–E implementation validation

Date: 2026-09-07 (Asia/Tokyo)
Source implementation commit: `66c2c44e53370f8c41dd4028eb7656bbbd2ad89f`
Base: `f0f43472c5fbb0c6e3e4eb540a7c06fdb43be90b` (alpha.6)
Branch: `perf/20260907-a-e`; main has not been changed.

## Recovery checkpoint is superseded

`PERFORMANCE_RECOVERY_STATUS_20260907.md` records the earlier interrupted session, not the current branch status. The complete source has now been recovered, built and transferred with an SHA-256 checked patch. The temporary transfer files and workflows have been removed from the working tree. Historical SDK-collection workflow success must still not be confused with application validation.

## Locally verified implementation

A: latest-only cancellable jobs, immutable parameter/source snapshots, photo/revision guards and a dedicated asynchronous SQLite writer.
B: compiled processing parameters/matrices, neutral adjustment bypass, active HSL bands and bounded CPU row workers; legacy algorithm kept only as an independent test/benchmark reference.
C: memory/disk preview caches with checksums and versioned identity, thumbnail placeholders, background full RAW loading and next-image prefetch, fit/100%/200% viewport preparation.
D: Qt 6.8.3 RHI FP32 compute editing, resident source/output textures and QQuickRhiItem display; capability-based CPU fallback. RAW unpack and final JPEG export remain CPU operations.
E: 1024-bin GPU RGB/luma histogram with asynchronous 16,400-byte readback, independent preview/full-resolution scopes and revision labels.
Also: frozen-parameter export queue, streaming JPEG with ICC, atomic/cancellable destination writes, protection of all imported originals, bounded buffered logs and performance diagnostics.

## Local validation evidence (not native Metal/Direct3D evidence)

Environment: Linux, Qt 6.8.3 Release, GCC 14; Mesa llvmpipe software OpenGL compute backend. Real RAW fixture: Leica M8 DNG, 3920×2638 (10,340,960 pixels).

- Core regression, performance-correctness and GPU-correctness CTest groups passed.
- Standalone required-GPU tests: 5 passed, 0 failed, 0 skipped.
- Four CPU/GPU parameter cases: maximum 16-bit channel error 1, 1, 2 and 11 out of 65,535. Histogram equals the counts of actual GPU-generated pixels exactly.
- Display orientation and unchanged-source upload reuse tested.
- Full application GUI smoke passed with real RAW, repeated edits, 100% viewport and full-resolution histogram. This validates the graphics path, not hardware GPU speed.
- Additional tests cover rapid photo switches, cache corruption (including dimensions), parameter snapshot export, cancellation, source protection, SQLite batching and final log flush.

## Synthetic CPU kernel measurements

Fixture: 2048×1365 generated linear-ProPhoto RGBA64; one warm-up and five runs; median milliseconds. Same host and build settings. These measurements exclude file I/O, RAW development, GUI presentation, GPU processing and JPEG encoding.

| Parameters | alpha.6 reference | alpha.7 serial | alpha.7 bounded parallel |
|---|---:|---:|---:|
| Neutral | 1393.711 | 415.802 | 150.124 |
| Mixed adjustments | 1400.852 | 991.359 | 379.938 |

These are not whole-program speedup claims and not benchmarks of the user's computer.

## Cross-platform gate

The PR build runs Windows x64, macOS arm64 and Linux. Windows requires D3D11/WARP compute tests; macOS reports Metal availability explicitly (SKIP is not a pass); Linux requires compute and GUI GPU/CPU smoke tests. Check the actual completed CI run and its validation artifacts before describing native platforms as verified.

Native hardware latency, other camera RAW families, very high resolution memory pressure, long batch stability, monitor ICC/soft-proof support and signed/notarized distribution remain separate validation or development work. See `PERFORMANCE_ALPHA7.md` for detailed limitations and tuning.
