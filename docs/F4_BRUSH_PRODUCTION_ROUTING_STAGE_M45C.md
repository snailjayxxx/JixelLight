# F4 M4.5C — retained image payloads and production preview/export routing

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Candidate working tree based on381078d; own three-platform/deployed-package acceptance pending. Saved enabled brushes, painting UI, full-scopes/CLI routing remain gated.

## Behavior

The actual CPU preview cache and QRhi preview item now select admitted requests for enabled in-memory brushes. CPU previews validate original intent even on cache hits; prepared sources keep geometry/viewport behavior and avoid double Look. Cache misses render once and return an immutable QImage view whose shallow copies retain pixels and their payload reservation. Evicting the cache or destroying the renderer cannot release a client-visible buffer's charge. Ordinary no-brush/disabled-draft routes remain the existing paths. Cache cost now counts a retained LUT's allocated RGB capacity.

The actual asynchronous `ExportQueue` resolves trusted as-shot metadata as before, freezes source/state, and uses the admitted original-source layout for enabled brushes: Whole for PNG16/TIFF16/WebP,128-row strips for JPEG. Admission precedes output creation and geometry/color processing. Existing source loading, sequential scheduling/prefetch, naming/publication, destination protection and interactive JPEG pause remain. Memory/work refusal leaves the target unchanged and reports file failure; the same queue can recover after the competing operation releases its reservation. Real strip cancellation preserves the target and permits a later healthy job. The old public low-level export/CPU research signatures keep their2Mi limits.

## Payload lifetime

M4.5A/B conservatively kept the entire operation reservation alive whenever an output survived. A cached preview or GPU frame could consequently retain source/geometry/codec allowances long after those allocations were freed, preventing otherwise legal image jobs. This stage splits the **already reserved** operation token into independently owned payload tokens without increasing aggregate charge. A child is allocated before the atomic parent reduction; allocation failure cannot lose or double-refund the charge. Parent plus surviving children account for remaining operation allowances and live payloads. Final request destruction refunds only transient allowances; live output children remain charged.

Whole RGBA64 output retains its actual stride bytes. Streaming center images transfer their own bytes, with total possible centers still limited to8P and one-use execution; dropping a completed strip releases its bytes because it cannot be rendered again. The original3GiB admission estimate, source32Mi and8Mi/tile96Mi/operation work limits are unchanged. This is more precise retained-payload accounting, not larger admission or a hard whole-process RSS cap.

`BrushImageLease::sharedImage()` uses a const-data QImage with cleanup ownership of both the original QImage and child token. Metadata is copied without moving pixels; the address is checked. Shallow copies share that ownership, while an intentional writable QImage detach creates a separate caller-owned copy. Internal consumers use leases during synchronous access or this retaining view when returning a QImage. They do not escape unleased read aliases.

For GPU display, conversion finishes under the operation allowance, then a private one-use transfer retains the actual16P FP32 upload buffer in its QImage cleanup. The temporary8P RGBA64 result and original source can then be freed. Replacement overlaps old upload payload plus a new admitted operation; ordinary compute/destruction releases the upload. GPU resources and the independently capped caches remain separate. Bounded intent/cache-key metadata is control state, not an image allocation or a whole-process memory guarantee.

## Verification

Admission tests additionally verify identical pixel addresses/ICC/metadata on returned views, safe writable detach, exact payload charge after request destruction and release only after the last shallow alias. Existing exact-admission, concurrency, cancellation and retained strip tests now distinguish active transient allowances from surviving actual pixels. GPU tests additionally require retained charge to equal the actual FP32 upload image bytes.

Four new120-second CTest shards exercise actual production entry points:

- CPU cache with2,659,021 prepared pixels: independent legal-strip oracle, original Look/geometry, hit reuse, changed-plan refusal before hit delivery, eviction/destruction with surviving clients, memory refusal and allocated LUT capacity;
- actual asynchronous four-format queue, frozen state after caller edits, exact PNG16/TIFF16 pixels/ICC and WebP100 encoded8-bit pixels, independent CPU JPEG100 comparison within8/255 on this smooth synthetic scene; previous real-photo5/255 and FP32 JPEG gates remain;
- competing admission, unchanged existing target, same-queue recovery, cancellation after actual JPEG row progress, staging cleanup and healthy subsequent export;
- actual public Sony07 RAW queue load with no source-image override, trusted as-shot ST, crop/straighten/orientation and Display P3 TIFF16 compared to an independently decoded/processed reference.

The last case pins the same69,120,000-byte ARW SHA-256 `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d`, consumes full7028×4688=32,947,264 nativeRGBA64 pixels and exports7,949,956 cropped/oriented pixels exactly. A32MiB cache cannot hold the full source and is never primed by the independent reference load. Actual queue output, ICC, original-file and decoded-pixel hashes match; final charge0 and peak870,426,112 bytes. It does not establish native FP32 RAW calibration or GPU brush compute.

Focused working-tree verification passed **9/9 CTest in48.04s**, including17 admission results, four routing and four admitted GPU shards, actual Metal and public Sony. The first three routing checks also passed independently in6.91s. Final working-tree **30/30 CTest passed349.85s**, with actual Metal/public Sony/DNG and zero failures/skips:212 Core/36 GPU/39 performance/21 Sony Look/9 DB/9 missing-source/17 admission results, nine large CPU, four admitted GPU and four new production-routing shards. Native existing-app GUI passed CPU13,958ms/Metal14,772ms, each23/23 screenshots,10,340,960 full scopes pixels, selected-export checks and zero QML errors; independent mask914ms/relink485ms passed all required flags and recovery screenshots (`file_picker_automated:false`). Own CI remains pending. Evidence resides in the physical build `local-brush-routing-focused.xml`, `local-brush-routing-full-tests.xml`, `brush-routing-*-results.txt` and `brush-routing-focused-final.log`. GUI evidence is in the physical build `local-brush-routing-gui/`. Report source commit381078d identifies the precommit base, not this candidate's exact committed acceptance.

## Remaining production work

Full histogram/plot caches still need original-source admission before geometry, crop-edit geometry handling and completed-frame cache collection. Deliver visible failure/retry for shared-budget competition through Controller jobs; connect CLI and diagnostic references. Then validate enabled-document project/XMP/preset/history/undo, painting gestures, source/version changes, full scopes/reopen/export. No user-visible brush is enabled by this stage.
