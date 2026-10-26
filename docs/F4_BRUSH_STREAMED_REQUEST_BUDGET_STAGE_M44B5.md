# F4 M4.4B.5 — whole-operation brush budgets for streamed rendering

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. This is a candidate stage and requires its own exact-tree three-platform validation.

## Problem

`processRegion` intentionally supports a bounded region of a larger source. JPEG export, full-resolution histograms and scope plots call it once per 128-row strip. Each strip previously received an independent brush research preflight, so a complete operation could exceed the 2,097,152 logical-pixel or 100,663,296 candidate-visit limits even though each individual strip was legal. The whole-frame raster-export route refused the same request. Enabled saved brushes remain gated, so this inconsistency concerns the opt-in in-memory research path.

A deterministic example needs only a 2048×1024 source and 97 identical radius-0.5 dabs at the center. Their bounding boxes require 101,711,872 candidate visits across the image, exceeding the 96 Mi limit. Every 128-row strip independently remains below that limit. Repeated dabs keep the same maximum-alpha visual contribution while still charging their real candidate work.

## Change

A shared `ImagePipeline::preflightTiledResearchRequest` checks an entire operation before color conversion, streamed color processing or output-file creation. Callers provide the logical post-geometry frame size and their actual stripe height. The logical active-brush frame must remain within the existing research size cap. Candidate accounting follows each strip's real expanded region and its 128×128 tile grid; repeated detail halos count again because they are actual repeated raster work. Tile and complete-operation work caps retain their existing values.

The preflight and `processRegion` share the region-expansion rule. Candidate bounds are prepared only for counting; this check does not copy brush documents, create a cross-request cache or allocate full-frame EV/color scratch. Cancellation or any invalid document, viewport, detail geometry or exceeded budget refuses the operation before rendered strips are published. Ordinary no-brush and valid disabled-brush large-frame rendering retains its existing behavior.

The check covers tiled JPEG export, exact full-image histograms, individual/shared scope plots and whole-frame raster export. Standalone `processRegion` keeps its accepted bounded-ROI behavior; it is not redefined as a full-source operation.

This stage does not enable persistent brush layers, add a drawing UI, enlarge research limits or implement native GPU brush compute. Reusing prepared plans across strips and parallel tile execution remain separate work.

## Verification

Core regressions must demonstrate:

- a legal isolated strip can belong to an over-budget complete operation, which is refused before successful-render counters change;
- detail-halo overlap is charged across strips, including a request whose unique full-frame count fits while repeated strip work does not;
- legal complete operations retain histogram/plot/export behavior and ordinary no-brush large images are not subjected to brush research caps;
- JPEG/raster refusal leaves existing destinations unchanged and does not publish new output; scope refusal does not return partial counts or plots;
- cancellation, malformed inputs and invalid stripe/detail geometry return an explicit failure without partial output.

Local Qt 6.8.3 Release compilation and the four targeted budget/cancellation regressions passed. Complete local CTest passed **10/10 in 73.37 s**, including **182 Core / 35 mandatory Metal GPU / 36 performance** checks, checksum-pinned public Sony RAW/JPEG and zero skips. This is working-tree verification before publication, not three-platform acceptance.

Final acceptance still requires unchanged Windows x64/macOS ARM64/Linux GPU tests, public Sony RAW/JPEG, original 40/65535 GPU tolerance, 1024-bin Scopes, native GUI and both actual deployed packages. The original real-RAW CPU GUI 60,000ms deadline remains unchanged. Do not mark this candidate accepted from an earlier stage's CI.
