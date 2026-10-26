# F4 M4.5B — prepared brush preview and GPU display handoff

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Candidate working tree based on `ea31a48`. This stage's own three-platform/deployed-package acceptance is pending. It builds on the explicit M4.5A owned CPU request, without enabling saved brush documents or a painting UI.

## Problem and behavior

`ProcessingPlan::state` contains the effective Sony Look adjustments. Recompiling it as original intent applies those adjustments twice. Preview preparation has also already applied geometry and may have selected a 1:1 panned viewport. A large brush GPU fallback must preserve original Look intent, skip repeated geometry, use the captured viewport and retain its memory charge through display upload.

Plans compiled with an enabled brush now privately retain a shared immutable original-intent snapshot. Ordinary no-brush plans do not allocate that snapshot. `BrushRenderRequest::createPrepared` accepts only a compiled brush plan, reconstructs the original rendering specification and verifies the provided plan against a fresh compile before any source pixel snapshot or rendering. Checks include packed color slots, double-valued CPU state, geometry, complete brush document, Look parameters/error and LUT identity. The runtime `FrameRect` remains the explicit viewport input; `Dimensions` and `PixelMap` are regenerated from the actual render resource. Manually assembled or modified plans refuse, rather than silently rendering a different adjustment state. Validation temporaries remain within the reserved operation allowance. Uniform upload still uses the same **51 Float4 / 816-byte** array.

`GpuEngine::processAdmittedBrush` is an explicit prepared-source API. It refuses incompatible sources, texture axes above the existing4096/hardware limit, malformed intent or exhausted operation memory before GPU initialization/uploads. These admission failures use a per-call error, leave the current GPU revision/source/frame unchanged and permit a healthy retry. The ordinary `process` entry point and public2Mi research CPU APIs retain their existing limits.

An accepted request renders exact CPU-reference RGBA64 pixels and converts them into the existing FP32 upload image. A `BrushImageLease` remains in the GPU engine alongside the upload image until replacement, ordinary GPU computation or engine destruction. Its reservation outlives both RGBA64 output and the retained float upload bytes. Creating a replacement request temporarily charges both old and new leases; peak accounting includes that overlap. Returning to ordinary GPU compute clears the old fallback image and lease. Rendering/driver failures retain the established GPU-engine failure behavior; recoverable admission refusal does not create that sticky failure.

Working-frame capture identifies **`producer:cpu-reference-upload`**, `gpu_upload_executed:true`, `native_gpu_brush:false`. The default3GiB operation budget,32Mi source cap,8Mi visits/tile,96Mi visits/operation, detail-halo accounting and independent GPU texture bounds remain. This is not a new native GPU brush shader or a whole-process RSS guarantee. The code does not automatically route Controller, export queue, CLI or full scopes through this new API.

## Verification

Two additional admission checks cover original-versus-effective Look, crop/straighten/orientation applied once, viewport mapping, full and streamed prepared output, and frozen intent after caller edits. Nine changed/manually assembled plan cases refuse and release the reservation; legitimate runtime uniform fields remain accepted. The component suite therefore has16 QtTest results including setup/cleanup.

Four new CTest shards each retain a120-second timeout:

- recoverable texture/memory/changed-plan refusal before source upload, a lease that survives completed GPU upload, ordinary-compute release and destruction release;
- a2,124,881-pixel signed/HDR/hidden-color/fractional-alpha FP32 viewport in three output spaces;
- an actual freshly decoded public Sony07 RAW prepared through `preparePreview` into a fitted preview;
- the same complete native source prepared into a panned2049×1293 viewport at100%.

The real source remains full7028×4688=32,947,264RGBA64 pixels, pinned ARW SHA-256 `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d`. Each prepared frame exceeds the old2Mi pixel cap; the old whole-frame research API still refuses it. Independent existing legal128-row CPU strips provide the per-pixel reference. Real texture readback checks RGB at the unchanged40/65535 bound, alpha exactly, all1024 histogram bins, complete pixel count and pre-monitor capture provenance. Source file, original decoded pixels and prepared FP32 pixels are hashed before/after. No generated photo artifacts are uploaded.

Initial focused verification passed4/4 CTest in26.81s, including16 admission results, recoverable GPU refusal and the native fit/pan cases. On M2/Metal the three output spaces (sRGB,Display P3,ProPhoto RGB) each had maximumRGB error0/65535:

| Native viewport | Prepared/scopes pixels | Normalized frame | Peak reserved bytes across replacement requests |
| --- | ---: | --- | ---: |
| Fit | 3,763,200 | 0,0,1,1 | 641,073,152 |
| 1:1 pan | 2,649,357 | .4769230769,.2620027435,.3060492905,.2956104252 | 498,501,248 |
| Signed/HDR/fractional-alpha FP32 viewport | 2,124,881 | .19,.11,.63,.71 | 431,368,320 |

Final working-tree **26/26 CTest passed in293.80s**, with mandatory actual Metal, checksum-pinned public Sony/DNG and zero failed/skipped tests. Existing212 Core/36 GPU/39 performance/21 Sony Look/9 DB/9 missing-source checks passed, with16 admission results, the nine original large CPU shards and four new admitted GPU shards. All nine new viewport/output-space combinations, including signed/HDR/fractional-alpha, had maximumRGB error0/65535, exact alpha and all1024 histogram bins. Each operation's retained charge returned to zero after ordinary compute; cancellation/source-immutability and the original limits remain covered by the existing M4.5A suite. Local source metadata identifies the precommit base, not an exact committed CI acceptance. Evidence lives in the physical build `brush-gpu-*-results.txt`, `brush-admission-results.txt`, `local-brush-prepared-focused.xml` and `local-brush-prepared-full-tests.xml`.

Native existing-app GUI also passed CPU14,042ms and Metal14,707ms, each23/23 screenshots,10,340,960 full scopes pixels, selected-export checks and zero QML errors under the unchanged60,000ms gate. Independent mask pointer912ms and missing-source recovery486ms passed, with all required flags and recovery screenshots; recovery still reports `file_picker_automated:false`. Evidence is in the physical build `local-brush-prepared-gui/`. These verify the current application, not a paintable brush UI.

## Remaining work

Integrate owned requests into the actual preview/analysis/export scheduling with visible refusal/retry and shared-budget competition handling; hand off completed frames into independently charged caches; validate full Controller/CLI/queue routes. Only then enable document persistence, history/undo and native painting with reopen/export/scopes evidence. Native GPU brush computation and calibrated native FP32 RAW remain separate unfinished work.
