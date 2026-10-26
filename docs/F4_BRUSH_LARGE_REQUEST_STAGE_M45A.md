# F4 M4.5A — owned large-image brush requests

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. This stage is a separate explicit CPU rendering admission boundary. Its own three-platform CI and deployed-package acceptance are pending.

## Behavior and ownership

`BrushRenderRequest::create` accepts the original adjustment intent and an explicit specification, compiles its own immutable processing plan, validates the complete operation and reserves memory before copying source pixels or executing geometry. Sources must be RGBA64 or RGBA32FPx4, at most **33,554,432 pixels (32 Mi)**, with a bounded stride and axis. FP32 validity is checked over the complete frozen source, including pixels outside the requested crop. Invalid geometry, ROI, viewport, modes, parameters, typed LUT contents, work limits or cancellation refuse the request. There is no source resize to satisfy admission.

The source is a **deep pixel copy**, including when the caller's QImage wraps an external buffer. State, geometry, output space, viewport and operation layout are captured together. The plan is compiled from original intent rather than an already effective Look state, avoiding duplicate Look adjustments. LUTs retain the existing immutable shared-LUT contract; their allocated RGB capacity is charged and contents validated. `SourceStage::Original` applies stored geometry once; `Prepared` accepts an already geometrized display/ROI source without applying that geometry again.

A request executes once, either as a whole region or as fixed 128-row strips. Repeated/concurrent execution refuses. `BrushImageLease` keeps the reservation alive after request destruction, including cancelled operations whose consumers retain earlier strips. Consumers keep the lease for the entire use of its image; escaping an unleased shallow QImage alias is outside this API contract. All internal consumers retain the lease. Images transferred into future independently budgeted caches will require an explicit ownership handoff.

The existing public research APIs keep their **2,097,152-pixel** limit. Both routes retain **8,388,608 candidate visits per tile**, **100,663,296 per operation**, **1,048,576 indexed references**, and the existing document limits. Whole-operation preflight counts each actual expanded strip, including repeated detail halos, before source snapshots, geometry, rendered output or output-file creation. A large source does not authorize excessive dab work.

## Memory admission

The default shared operation budget is **3,221,225,472 bytes (3 GiB)**, atomically reserved across simultaneous requests and all surviving image leases. Custom budgets support deterministic boundary tests. Refusal or the final lease destruction releases the charge; the high-water mark remains diagnostic.

Let S be the source's actual stride bytes, P the requested region pixels, A its detail-expanded pixels, and b the source bytes per pixel (8 or 16). The fixed allowance is 64 MiB plus 32 × 384 KiB of worker scratch, plus the retained LUT's allocated RGB capacity × sizeof(float).

| Layout | Reserved image/work allowance before the fixed allowance |
| --- | --- |
| Whole | max(3S, 2S + optional ROI copy bA + 24A + 8P, S + 24P) |
| Stripes128 | 3S + 24P + 40 × largest detail-expanded stripe pixels |

The ROI-copy term is zero for a complete frame. The streaming 24P includes the maximum total of retained center strips (8P) and a conservative codec allowance (16P). One-use execution prevents a second output collection under the same charge. Output dimensions are predicted before geometry and checked against its actual result.

This reservation is a conservative operation-payload estimate, **not a hard whole-process RSS cap**. Qt/plugin allocator overhead, metadata/ICC storage, the caller's source and independently budgeted caches remain separate. The codec allowance is not a formal upper bound for every image plugin allocation. Tests record sampled process RSS every 5 ms, including independent references; sampling may miss brief peaks. No older-binary speedup or cross-machine throughput guarantee is asserted.

## Explicit consumers

- `exportJpegTiled(BrushRenderRequest&, ...)` streams admitted strips through the existing libjpeg writer and ICC handling. Cancellation retains the previous target and removes staging files.
- `exportRaster(BrushRenderRequest&, ...)` holds a whole-image lease through PNG16, TIFF16/LZW or WebP8 writing, retaining atomic destination replacement.
- `ScopesEngine::analyzeAdmitted` computes a complete histogram and, only when requested, all three plots in the same color pass. Failure does not publish partial plots.

Existing exporter/scopes signatures retain their research limits. No Controller, export queue, CLI, project/XMP/preset reader or painting UI automatically selects this new route. Enabled saved brushes remain refused. This stage has **CPU-reference** provenance: native GPU brush and GPU upload are both false. The actual Sony decoder still produces RGBA64; the synthetic FP32 fixture does not establish native FP32 RAW color fidelity.

## Verification

The component suite contains 12 behavior checks (14 QtTest results including setup/cleanup): frozen HDR/source/state/geometry/viewport, external-buffer mutation, exact budget boundaries and surviving leases, uneven streamed ROI/detail, prepared geometry, original research/work/halo gates, 32 Mi source boundary and full-source FP32 validation, concurrent admission and one-use execution, in-flight cancellation, allocated LUT capacity, malformed typed LUT refusal/recovery, all four output spaces/formats and shared scopes, plus atomic cancellation/layout failure.

Nine independent large CTest shards retain **120 seconds per shard**. They compare the new route against the existing independently rendered, legal <=2 Mi-pixel strips. Existing 120-second performance, 40/65535 GPU, and 60,000 ms GUI gates remain unchanged.

The public Sony07 ARW is 69,120,000 bytes with SHA-256 `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d`. Fresh decoding returns the **complete 7028×4688 = 32,947,264-pixel bitmap**. Camera default-crop metadata (12,8,7008,4672) is distinct. Initial fixture assertions incorrectly assumed the active crop and failed before rendering; the test now verifies the full decoded source without resizing. File and decoded-pixel hashes are checked before/after. Temporary exported photos are removed and are not uploaded.

Final working-tree verification, including typed-LUT refusal and explicit source/engine/snapshot report metadata, passed **22/22 CTest in 314.90 s**, with real Sony/DNG and mandatory actual Metal GPU, zero skips: 212 Core, 36 GPU, 39 performance, 21 Sony Look, 9 DB relink, 9 missing-source, 14 admission results and nine large shards. The earlier complete run passed in259.61s; these are independent local measurements, not a performance comparison.

Measured on Apple M2, 8 GB RAM, Qt 6.8.3 Release/macOS 26.5, from the final314.90s run:

| Large case | Elapsed ms | Peak reserved bytes | Sampled process RSS bytes | Result |
| --- | ---: | ---: | ---: | --- |
| Synthetic 24 MP FP32, geometry + panned ROI | 16,355 | 2,352,954,632 | 1,204,142,080 | Exact full 21,787,072 pixels and 3,608,307-pixel ROI |
| Sony full + ROI, sRGB | 14,367 | 1,661,160,448 | 1,310,064,640 | Every RGBA64 channel exact |
| Sony full + ROI, Display P3 | 14,318 | 1,661,160,448 | 1,244,446,720 | Every RGBA64 channel exact |
| Sony full + ROI, ProPhoto RGB | 14,323 | 1,661,160,448 | 1,078,771,712 | Every RGBA64 channel exact |
| Sony histogram + three full plots | 18,633 | 1,703,890,688 | 824,541,184 | All 1024 bins, clips and all plots exact; 32,947,264 pixels each |
| Sony PNG16, ProPhoto RGB | 48,082 | 1,661,160,448 | 897,204,224 | Exact pixels/ICC; 183,979,989-byte output |
| Sony TIFF16, Display P3 | 20,188 | 1,661,160,448 | 857,473,024 | Exact pixels/ICC; 262,273,686-byte output |
| Sony WebP100 lossless, sRGB | 30,443 | 1,661,160,448 | 1,009,106,944 | Exact encoded 8-bit pixels/ICC; 42,089,590 bytes |
| Sony JPEG100, sRGB | 15,877 | 1,703,890,688 | 824,672,256 | ICC exact; max codec difference 5/255; 41,436,904 bytes |

Every operation ended with **zero charged bytes**, unchanged source hashes and CPU-reference provenance. The Sony ROI contains 4,232,559 pixels with a non-full normalized frame; this is mathematical viewport/ROI validation, not a live editor pan test. JPEG's new natural-photo comparison allows 5/255 codec error and observed 5/255; the existing FP32 uniform JPEG regression and GPU limits are unchanged. Cancellation after the first actual JPEG strip (2% progress) preserved the previous 20-byte target, removed hidden staging files and allowed a healthy subsequent request.

Local raw evidence: physical build `local-brush-admitted-final-tests.xml`, `brush-large-*-results.txt` and `/Volumes/MacNAS-Dev/JixelLight-Build/brush-admitted-final-tests.log`. This host uses locally installed native dependencies and an SDK compatibility configuration; it does not replace three-platform/deployed-package acceptance.

The final native GUI runs passed CPU **14,472 ms** and Metal/M2 **15,301 ms**, each with23/23 successful screenshots,10,340,960 full scopes pixels, selected-export checks and zero QML errors, under the unchanged60,000ms limit. These runs verify the existing application while the new brush route remains explicit. Reports are in the physical build `local-brush-large-gui/`.

Independent native pointer checks also passed: existing radial/linear mask interactions **1,192ms**, missing-source recovery **473ms**, all required flags/screenshots and zero QML errors. Recovery still reports `file_picker_automated:false`. The compact selected-export screenshot was visually inspected for readable controls and naming preview.

## Remaining production work

Carry original intent through preview/analysis plans without reapplying Look; route the queue/CLI/full scopes through owned requests; preserve leases through cache/GPU handoff; schedule/defer competing large operations under the shared budget with visible retry/error behavior. Validate prepared fit and 1:1 ROI display within the existing GPU axis/resource limits before claiming CPU-reference-upload parity. Then implement enabled-document persistence/history/undo and native pointer painting with reopen/export/scopes evidence. Completing this stage does not complete production brush editing or F0–F6.
