# F3 — defer full scopes until preview preparation completes

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Accepted at HEAD `96e487a13dd89e1e26d18f9b70e41831e02f58a5` by [CI #40280382817](https://github.com/snailjayxxx/JixelLight/actions/runs/40280382817), including both deployed packages.

## Observed problem

The accepted Windows deployment report from CI #40275215812 shows a full-scopes straighten request spending 1,240 ms on geometry and 63.9 ms on statistics before cancellation, followed by another geometry/statistics request. `finishInteraction` starts the 600 ms exact-scopes timer while preview preparation can still be running. Preparation later publishes a new revision and restarts that timer, invalidating the early request.

`requestFullScopes` now defers while `m_preparing` is true and records `full_scopes_deferred_while_preparing`. The existing preparation-completion callback remains responsible for scheduling exact statistics for the new revision. Requests made after preparation retain their original behavior. Turning exact scopes off still stops its timer and cancels its job; no new queue, cache, algorithm or debounce interval is introduced.

## Verification and diagnostics

Two regression tests were first run against the old controller and failed. They exercise straighten/crop through public methods, finish the interaction and enable exact statistics without processing queued preparation completion. The new guard must record one synchronous deferral. After completion, all four 1024-bin histograms, pixel count and clipping percentages must equal an independently loaded and geometrically processed CPU reference. A second test disables exact scopes during preparation and verifies no full-scopes cache work occurs, then confirms re-enabling after preparation succeeds. Existing version-switch/cache reuse remains covered.

GUI smoke also records each existing screenshot's `basename`, phase, `grab_ms`, `save_ms`, success and dimensions. Saving uses the original `QImage::save` parameters; `save_ms` includes encoding and file I/O. The final screenshot is included. Screenshot paths/order/count, every existing assertion, the elapsed-time sampling point and original 60,000 ms RAW CPU GUI gate are unchanged. The final screenshot still occurs after the elapsed sample, as before. These measurements distinguish capture from save cost; they do not assume the Windows CPU/GPU smoke difference is PNG encoding or promise a wall-time improvement.

Local Qt 6.8.3 Release verification passed all three targeted scheduling/cache regressions and complete CTest **10/10 in 73.60 s** (**189 Core / 35 Metal GPU / 36 performance**, real Sony, zero skips). Real public-DNG GUI smoke retained all **22 screenshots** and complete **10,340,960-pixel** scopes: offscreen CPU **14,945 ms**, native Cocoa/Metal **16,353 ms**, both under the unchanged 60,000 ms limit with zero QML error markers. Timing arrays include all successful screenshots and the final image. Different presentation paths mean these two local times are not a CPU/GPU speed comparison. The final-tree three-platform/deployment evidence is recorded below. Public Sony RAW/JPEG, original 40/65535 GPU parity, exact Scopes, native mask controls and both actual deployment packages remain mandatory.

## Exact-tree CI and deployed packages

CI #40280382817 passed Windows x64, macOS ARM64 and Linux GPU: **10/10 CTest, 189 Core / 35 GPU / 36 performance per platform, zero failed or skipped**, public Sony RAW/JPEG, all five JPEG and both scheduling regressions. The tested merge `0b82829f7af4db159d3baec5d8211c46d271c6cb` and HEAD share tree `2a9b6d93edc017333a3f5b342167a31a54ff22c2`. The retained 16K-dab GPU viewport passed all three output spaces at 0/65535 under the original 40/65535 bound.

| Deployed package | CPU RAW GUI | GPU RAW GUI | CPU screenshot grab / save total | GPU screenshot grab / save total |
| --- | ---: | ---: | ---: | ---: |
| Windows x64 | 59,890 ms | 43,000 ms | 503.566 / 1,769.941 ms | 789.643 / 2,101.317 ms |
| macOS ARM64 | 43,201 ms | 40,724 ms | 457.110 / 3,070.375 ms | 283.775 / 2,970.936 ms |

All four reports contain **22 successful screenshot timing entries**, full **10,340,960-pixel** scopes and zero QML errors. Both package manifests passed all six checks with SDK paths removed. The unchanged 60,000ms CPU deadline passed on Windows by only **110ms**; this is acceptance evidence, not a robust performance margin or a demonstrated speedup. Screenshot save includes encoding plus file I/O and accounts for much less than the Windows CPU/GPU wall-time difference.

Windows CPU/GPU recorded `full_scopes_deferred_while_preparing` as **1/2**. Neither macOS report recorded an increment. Full-scopes and scope-plot caches each reported **zero cancelled requests in all four package runs**; preview-render cancellation is a separate metric (Windows CPU 34, macOS CPU 33, both GPU runs 0). This run does not establish that all wall-time variance is caused by full-scopes scheduling. Reusing completed analysis color for the first plot is separate future work.
