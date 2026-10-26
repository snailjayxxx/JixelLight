# F3 — defer full scopes until preview preparation completes

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. This candidate requires final-tree three-platform and deployed-package acceptance.

## Observed problem

The accepted Windows deployment report from CI #40275215812 shows a full-scopes straighten request spending 1,240 ms on geometry and 63.9 ms on statistics before cancellation, followed by another geometry/statistics request. `finishInteraction` starts the 600 ms exact-scopes timer while preview preparation can still be running. Preparation later publishes a new revision and restarts that timer, invalidating the early request.

`requestFullScopes` now defers while `m_preparing` is true and records `full_scopes_deferred_while_preparing`. The existing preparation-completion callback remains responsible for scheduling exact statistics for the new revision. Requests made after preparation retain their original behavior. Turning exact scopes off still stops its timer and cancels its job; no new queue, cache, algorithm or debounce interval is introduced.

## Verification and diagnostics

Two regression tests were first run against the old controller and failed. They exercise straighten/crop through public methods, finish the interaction and enable exact statistics without processing queued preparation completion. The new guard must record one synchronous deferral. After completion, all four 1024-bin histograms, pixel count and clipping percentages must equal an independently loaded and geometrically processed CPU reference. A second test disables exact scopes during preparation and verifies no full-scopes cache work occurs, then confirms re-enabling after preparation succeeds. Existing version-switch/cache reuse remains covered.

GUI smoke also records each existing screenshot's `basename`, phase, `grab_ms`, `save_ms`, success and dimensions. Saving uses the original `QImage::save` parameters; `save_ms` includes encoding and file I/O. The final screenshot is included. Screenshot paths/order/count, every existing assertion, the elapsed-time sampling point and original 60,000 ms RAW CPU GUI gate are unchanged. The final screenshot still occurs after the elapsed sample, as before. These measurements distinguish capture from save cost; they do not assume the Windows CPU/GPU smoke difference is PNG encoding or promise a wall-time improvement.

Local Qt 6.8.3 Release verification passed all three targeted scheduling/cache regressions and complete CTest **10/10 in 73.60 s** (**189 Core / 35 Metal GPU / 36 performance**, real Sony, zero skips). Real public-DNG GUI smoke retained all **22 screenshots** and complete **10,340,960-pixel** scopes: offscreen CPU **14,945 ms**, native Cocoa/Metal **16,353 ms**, both under the unchanged 60,000 ms limit with zero QML error markers. Timing arrays include all successful screenshots and the final image. Different presentation paths mean these two local times are not a CPU/GPU speed comparison. Final-tree three-platform/deployment acceptance remains pending. Public Sony RAW/JPEG, original 40/65535 GPU parity, exact Scopes, native mask controls and both actual deployment packages remain mandatory.
