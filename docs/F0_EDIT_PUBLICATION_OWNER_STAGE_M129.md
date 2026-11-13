# F0 durable edit publication and owner lifetime — M129

Status: repair and regressions implemented, before fresh compilation/full/platform qualification. Parent37c2e4f has independent local102/all33/fifteen-runtime and all-three CI438/installed88/strict acceptance. The root remains that clean accepted commit; the repair is isolated.

## Reproduced failure

Test-only78f4b108111ceb6ad07d4571e14e15b20631f057 leaves production identical to37c2e4f. Its fresh focused Release build42.81332s passes. A real SQLite regression then fails in0.828951s (Qt185ms): successful terminal close from the history notification of a+0.75EV edit leaves saved exposure0, expected0.75, before controller destruction. Totals2 passed/1 failed/0 skipped/0 blacklisted. Original log SHA256e0fc7b265cb68871749bcfe15499b96dc08d44cefab8c4889b0b1e9c975cc48e. All22 files/14,296,487 bytes, exact Git-verified source copies, binary and original build graph/logs are retained in `failed-development-78f4b10-first`; the physical first build is never rebuilt. This is a focused CPU/offscreen durability failure, not a full/native/GPU qualification.

## Repair

- Shared edit publication and Undo/Redo register the complete dirty state/history and freeze the action trace target before notifying observers. A synchronous accepted close now sees the last edit. Recording before notifications also preserves chronological replay when a notification commits another edit.
- Publication checks guarded ownership, catalog/photo epochs and request revision after notifications. Private publication/preparation helpers return whether their context survived, allowing preset/reset/paste/XMP/brush/calibration callers to stop follow-up UI/refinement safely. Public acceptance semantics are unchanged for already committed actions.
- Apply/prepare/refinement and brush-cancellation notifications stop after close, destruction or context replacement. Shared mutable-state and Undo/Redo entry points reject a foreign owner thread and terminal close. Existing reusable persistence preflight, image math, gesture sampling and dirty-row serialization are retained.
- Reference/calibration completion callbacks protect their notification continuation; no fitting/color algorithm is changed. Identical queue headers inherit the earlier minimal queue ASAN probes only; the new controller changes are not a sanitized full application.

## Regression scope and pending gates

The original failing test remains unchanged. Forty-four notification rows cover scalar/reset/Undo/Redo/paste/actual XMP/brush removal, preparation and refinement, each with real terminal close or synchronous destruction. Independent SQLite snapshots compare complete adjustments, exact history, curation and tags; sources remain unchanged; closed owners must start no later work. Additional cases cover foreign/closed entry refusal, notification-driven photo selection, and a nested edit's chronological trace and single latest scheduling.

Expected scheduling90, Core276, performance44, Move49, session23, CLI92 and Python283. CTest remains102/all96 Qt result groups, original33 distinct validation contexts and all15 runtime guards. A fresh physical second build, mandatory GPU82/real-Sony preflight, targeted development, default-display exact PNG16/ICC comparisons, full/native/platform/installed88/strict delivery are all pending; earlier acceptance is not substituted.

No RAW precision, source-photo writes, cache/admission capacity, worker policy, GPU40/65535 threshold or deadline changes. Main and protected PR15 remain unmerged. Independent native-FP32 camera calibration, A7R VI photographic and physical-device acceptance are incomplete.
