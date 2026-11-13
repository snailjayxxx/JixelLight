# F6 XMP review cancellation — M114

## Behavior

XMP review joins the operation-specific cancellation dispatcher from M113: `xmp.cancel {operation_id}` requires the exact current `xmpMergeOperationId`, a nonzero lowercase UUID without braces. Each review gets a fresh identity. Malformed, stale, idle and foreign-thread commands refuse before changing a review, GUI, catalog or file. The legacy Cancel button keeps its existing method and now uses the same guarded dispatcher. Its owner-thread idle return remains true; the explicit command requires a current operation and rejects idle requests.

The existing cancellation body retains its Committing/ReloadRequired refusal and original worker cancellation and review cleanup. Cancellation does not authorize applying fields or bypass durable commits. Accepted traces freeze command/project/version facts before completion callbacks; no controller read occurs after a callback can destroy it. Review-start traces expose the operation identity before GUI callbacks. No Develop/history/SQL/image/ICC/export algorithm, worker count, resource bound or deadline changes.

## Acceptance scope

Pure Core and offline session cases now include XMP cancellation alongside export/transfer cancellation. Two new XMP workflow cases verify an actual foreign thread cannot cancel through either entry point, an old operation cannot cancel a newer review, the newer review can still apply its original six fields, and completion callbacks may destroy the owner without losing the accepted trace or modifying files/catalog. The existing actual Cancel-button case also requires the canonical command and identity. Expected totals: Core255, session22, XMP workflows22, move-import21, soft-proof workflows16 and CLI90. Original88 CLI labels retain their order; two XMP-cancellation categories are appended. Python281 passes0.768s; C++ and complete native/platform acceptance remain pending at this snapshot.

Intermediate38155af (M113) independently passes its Release build, all10 targeted groups/64.75s, actual normal App CPU/Metal/DPR1/2/full10.34M source pixels, complete Canvas4, proof2, Relink1 and Move2. Its actual move/recovery identities differ and stale cancellation is refused. It has no frozen full102 or platform qualification claim. This combined candidate will have its own clean build/full102/all96/original29+Canvas4 (33 distinct), real-Sony/CLI PNG16/ICC oracles, six cross-RAW exports/576rows and thirteen binary guards, followed by independent three-platform/both88 installed-package/strict-delivery gates. Intermediate source and validation logs remain preserved.

Parent1058b25 completes its own local full102/951.44s/all33/twelve-binary closure and CI430 remains independent. Final-panff5377e completes local full102/926.04s/all33/eleven-binary closure; CI429 Linux and macOS installed88/strict succeed, Windows remains pending. Original CI427 macOS pan failure and separate first XMP-picker readiness failures remain retained, without a root-cause repair claim for the picker.

Main and protected PR15 remain unmerged. A7R VI real RAW/JPEG samples are not supplied; physical tablet/printer calibration, native-FP32 RAW decoding and overall F0–F6 completion are not claimed.
