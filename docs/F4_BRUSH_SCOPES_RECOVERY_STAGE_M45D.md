# F4 M4.5D — admitted full statistics and recoverable memory contention

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Accepted at `7d76c547b82cb8c798efa522dbe6b81931a1b812` by [CI #40374144853](https://github.com/snailjayxxx/JixelLight/actions/runs/40374144853). Enabled saved documents and painting remain gated in this accepted stage. M4.5C is separately accepted at4189f4a by CI40365763844.

## Actual statistics routes

`FullScopesCache` and `ScopePlotCache` now create original-source admitted brush requests before geometry, source snapshots or color rendering. `createOriginal` validates the compiled plan against its private original intent before copying the caller's trusted view geometry. Crop-edit statistics may therefore use the uncropped view while preserving the same frozen exposure, Look, RAW provenance and masks. Compiling the already-effective plan state again would apply Look twice; this bridge instead retains the original intent. Changed plans refuse even before cache-hit delivery.

Full histograms visit legal128-row center strips under the existing32Mi-pixel/3GiB admission and candidate-work limits. An optional completed-strip observer feeds the independently budgeted analysis-frame writer. Only complete frames publish; failed optional collectors cannot invalidate a valid histogram. Scope plots reuse a completed encoded frame without another color pass or operation reservation. On a miss, a cacheable plot request shares one admitted color pass for three plots; a disabled/oversize plot cache counts only its requested mode. Histogram and plot caches retain completed results and allocated LUT capacity. Existing public research APIs keep their2Mi-pixel refusal and tile/operation gates.

This keeps full native Sony32,947,264-pixel statistics available with enabled in-memory brushes. It does not call synthetic data a native FP32 RAW decoder or CPU-reference-upload a native GPU brush.

## Visible failure and recovery

Admission returns a typed reason distinguishing invalid requests, cancellation and a temporarily unavailable memory reservation. Production CPU preview/full-histogram/plot jobs propagate the actual reason. A failure releases rendering/busy state, retains previous pixels/counts with an unavailable/error label, and never marks old scope ink current. A full-histogram error outranks preview statistics for the same revision, preventing a late successful preview from concealing a failed full request. Stale revisions cannot publish errors or results.

Only memory-admission failures defer. A single-shot five-second timer retries current desired work up to eight attempts; it waits through preparation/interaction and avoids plot submission during a preview render. Actual export completion resumes pending work immediately and refreshes this bounded allowance. New edits and explicit scope/mode changes also request current work. Invalid documents/work-limit refusals do not enter an automatic retry loop. This is bounded recovery, not a promise to keep a rejected job running indefinitely. All production caches, export queue and GPU preview share the controller's default3GiB budget or an explicitly injected test budget.

## Other consumers and remaining gates

The CLI's internal export implementation now selects the same admitted original-source overloads for enabled in-memory state. The external saved-JSON parser still rejects enabled documents until persistence/history/painting are accepted; enabled-brush CLI end-to-end acceptance is therefore not claimed here.

Actual controller diagnostic bundles capture the admitted final CPU preview and, when requested, a reference for the identical frozen GPU source. The existing64MiB diagnostic/readback cap remains. Per-stage brush traces remain explicitly unavailable; the final-frame fingerprint is recorded separately without inventing internal stages. Diagnostic ZIPs stay local. Existing ordinary diagnostic/GPU checks remain mandatory.

## Verification

Four new120-second shards exercise production entry points:

- Signed/HDR/alpha FP32 source2,659,021 pixels, original cropped/rotated/straightened plan with trusted uncropped view, independent legal-strip oracle, all4096 histogram bins/clipping values and three exact plots. Completed-frame plot reuse works even with a one-byte operation budget; two subsequent plot modes hit the same ink cache. Changed-plan hits refuse, changed view geometry misses/refuses, and admitted plot-only/shared paths also match.
- Typed quota/invalid/cancelled failures, no partial cache publication, unchanged old2Mi research refusal, throwing optional collector invoked only once while a valid histogram finishes, cancellation after two actual center strips with untouched caller plot results and no completed frame, then healthy recovery and zero end charge.
- Fresh checksum-pinned public Sony07 ARW69,120,000 bytes, native7028×4688 RGBA64; full uncropped32,947,264 statistics pixels/4096 bins/three plots exactly match an independent streamed reference. Completed frame reuse needs no new operation reservation; peak1,697,143,808 bytes/end0, original file and decoded pixels unchanged.
- Actual controller jobs with a competing reservation: CPU preview/full histogram/plot show recoverable unavailable state without spinning/current-old-ink. After the competing fixture reservation is released, an actual independent export completes and wakes deferred work. Full histogram/plot recover at the same revision, final counts match an independent reference, actual local diagnostic ZIP admits a final fingerprint while declaring per-stage tracing unavailable, and final image/operation charges drop to zero. Private fixture injection avoids prematurely enabling public saved-brush/UI APIs.

Initial focused4/4 passed32.53s: cache5.97s, cancellation0.68s, native Sony13.75s and controller12.11s. Final full working-tree **34/34 CTest passed359.68s**, actual Metal/public Sony/DNG, zero failures/skips:212 Core/36 GPU/39 performance/21 Sony Look/9 DB/9 missing-source/17 admission, nine large CPU/four admitted GPU/four preview-export routing/four new statistics-recovery shards. All original thresholds/time limits remain. Reports reside in the physical build `brush-scopes-*-results.txt`, `local-brush-scopes-first.xml`, `local-brush-scopes-full-tests.xml`, with parent `brush-scopes-{first,full}-tests.log`; machine-readable summary `/tmp/jixellight-brush-scopes-local-summary.json`. Report `source_commit:4189f4a` is the precommit base of this verified working tree, not exact committed D acceptance. Own CI was pending at this local checkpoint and is accepted below.

Existing QML GUI regression passed CPU/offscreen14,201ms and actual Metal/cocoa15,124ms, both23/23 screenshots,10,340,960 full scope pixels, all existing UI/selected-export checks and zero QML errors under the unchanged60,000ms deadline. Independent native mask917ms and source-recovery460ms passed all required conditions, recovery screenshots and explicitly unautomated file picker. Reports/logs/screenshots are in physical build `local-brush-scopes-gui/`; the harness summary is `summary.json`, parent log `brush-scopes-gui-validation.log`. These protect existing UI flows and do not constitute a new paintable brush interface.

The initial new test build rejected an omitted required input-encoding argument; the fixture now specifies LinearProPhoto explicitly. Both failed and corrected build logs are retained. No validation gate was relaxed.

## Exact-HEAD acceptance

All three jobs completed successfully with34/34 CTest: Linux551.15s, Windows940.19s and macOS875.45s,212 Core/36 GPU/39 performance/21 Sony Look/9 DB/9 missing-source/17 admission plus all nine large CPU/four admitted GPU/four preview-export/four new statistics-recovery shards, zero failures/skips. The tested merge `d1a1dc52d83e748f11d8b7527f7b3d4ac5f67f57` has parents main `4efe6aa2ec81eb83053dea239be5d7fa3435d500` and7d76c54; tree `e617e8c3dded879c518ce1e4068e9f815d8396c8` equals the source HEAD tree. Binary/report `source_commit:d1a1dc5` identifies this matching merge.

Every platform verified full32,947,264 native Sony pixels/4096 histogram bins/three exact scope plots and completed-frame reuse, peak1,697,143,808/end0 operation bytes, unchanged source, throwing/cancelled collectors and actual controller quota recovery/diagnostics. Prior production queue checks still exported7,949,956 exact TIFF16/P3 pixels; all27 prepared GPU cases remainedmax0/65535 with explicit CPU-reference-upload provenance. The original32Mi/3GiB, work, precision and120-second gates remain.

Both actual SDK-stripped packages passed all seven modes. Windows CPU55,312/GPU38,262ms and macOS CPU33,182/GPU31,855ms satisfy the unchanged60,000ms limit, each23 screenshots,10,340,960 full-scope pixels, selected-export checks and zero QML errors. Recovery3,135/1,515ms passed all conditions/three screenshots, picker explicitly unautomated. All five evidence ZIP SHA-256 digests/CRCs and full three job logs are retained under `/tmp/jixellight-40374144853-*`; complete `/tmp/jixellight-40374144853-summary.json` is also copied to physical build `ci-40374144853-summary.json`. This acceptance is specific to7d76c54, not later persistence/painting changes.

Next production slice: enabled-document project/XMP/preset/history/Undo, a real pointer-driven brush gesture with one undo entry, source/version/crop/reopen behavior and actual painted full-scopes/four-format export UI. Current passing internal routes do not complete F4 or the F0–F6 product.
