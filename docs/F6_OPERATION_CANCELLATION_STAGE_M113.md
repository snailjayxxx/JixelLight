# F6 operation-specific cancellation — M113

## Implemented behavior

`export.cancel {operation_id}` and `photo.transfer.cancel {operation_id}` are strict desktop commands. The required identity is the current nonzero lowercase UUID without braces, exposed by `exportOperationId` / `copyImportOperationId` and included in the accepted start trace. Unknown fields, wrong types, malformed identities, idle queues, another queue's identity and a previous operation's identity are refused. Every accepted export, copy, move and reviewed move recovery receives a fresh UUID. A move keeps the same identity through its copy, durable-catalog handoff and source-retirement phases.

Existing Cancel buttons retain their public adapters and now use the same controller dispatcher. Both shared and direct adapters check the owner thread and closing state before reading queue state. Accepted cancellation traces contain the exact command, queue kind and `cancellation-requested` completion fact. Cancellation sets the existing cooperative token and does not emit a synchronous completion signal or start another operation.

True means that cancellation was requested for that operation. Completed exports/copies remain. Copy staging, new-file export publication, full byte verification, durable catalog handoff and move journals retain their existing behavior. Unretired move sources remain; a source-retirement call already in progress may finish. This change does not promise rollback or immediate worker termination. Worker counts, image processing, CPU/GPU precision, source bytes, memory bounds and deadlines are unchanged. Offline sessions and nested edit batches cannot cancel desktop work or publish their earlier staged outputs.

## Verification being completed

New Core cases cover strict pure decoding/output atomicity, actual export cancellation followed by a successful newer export, copy cancellation through the existing adapter followed by an exact-byte newer copy, stale/cross-queue refusal and actual foreign-thread refusal. Existing successful move/recovery tests check fresh recovery identity; a new recovery cancellation test preserves original sources and verified copies without retirement. Expected totals are Core255, session22 and move-import21.

The actual Main App move workflow retains both translated pointer confirmations, original image/history/scopes, durable identities, system-trash retirement, reopen, export and diagnostic checks. It now also observes two distinct move/recovery identities and refuses the old move's cancellation during recovery. The independent verifier requires these facts, including strict boolean evidence and an empty identity after completion. Two Python regression methods reject fabricated identity evidence; all281 Python methods pass in0.811s. The initial standalone QA test used the wrong exception expectation; its failed log remains separate, and the cases are integrated into the existing CI-discovered move-evidence suite with the correct ValueError contract.

The original86 CLI check labels remain in order with two cancellation schema/session-refusal categories appended (88). Full102/all96 groups, original29 plus four Canvas CPU/Metal/DPR contexts (33 distinct), actual Sony/CLI PNG16/ICC oracles, cross-RAW exports, thirteen unchanged participating binaries and independent three-platform/installed-package gates remain required. C++ compilation and native acceptance are not yet claimed at this snapshot.

## Parent checkpoint and limits

Parent1058b25 independently passes local full102/951.44s/all96 Qt groups, with its later native/RAW/twelve-binary closure and CI430 still running. The preceding final-pan fixff5377e has its own full102/926.04s/all33/eleven-binary closure; CI429 remains independent. Original CI427 macOS pan failure and first XMP-picker readiness failures remain retained, without relabelling them as repaired evidence.

Main and protected PR15 remain unmerged. A7R VI real RAW/JPEG validation still lacks the sample path. Physical tablet/printer calibration, native-FP32 RAW decoding and overall F0–F6 completion are not claimed.
