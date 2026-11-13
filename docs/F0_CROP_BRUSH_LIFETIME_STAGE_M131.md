# F0 crop/brush transition lifetime — M131

Status: implementation and regressions, before fresh compilation/full/platform qualification. Parent585a56b independently completes local102/966.21s, all96 Qt groups/all33 contexts/fifteen unchanged runtimes and is draftPR83/CI439. Stable root37c2e4f remains all-three CI438/installed88/strict accepted and clean.

## Retained baseline

Test-only2c49658ce8175f3e9916486c88cd56dd047dedcb leaves production identical to585a56b. Its fresh focused Release build41.913193s passes. The real-project crop-entry regression fails in0.817871s (Qt165ms): refinement's library notification accepts `beginClose()`, yet crop entry then sets `m_cropEditing=true`, expectedfalse. Totals2 passed/1 failed/0 skipped/0 blacklisted. Original log SHA256fa924ee4d8375a44d56fe1a25730feacdd60bba447855715203cfd348ce6a042. All22 files/14,398,576 bytes, exact Git-verified source copies, binary and original build graph/logs remain failed in `failed-development-2c49658-first`. The first physical build is never rebuilt. This focused CPU/offscreen regression is not native/physical-device acceptance.

## Repair

- A private guarded refinement result lets crop entry stop after close/context replacement while retaining the public `finishInteraction()` API. An in-progress refinement refuses recursive crop entry. Begin/cancel/apply crop protect their draft notification and use the existing geometry command/dirty-history transaction for an accepted commit.
- Brush begin/append/end/recovery protect owner, photo/catalog, preparation/revision and current gesture identity after drawing, capacity, preview and recovery notifications. A newly started gesture cannot be committed or cancelled by the older ending gesture. Foreign-thread and terminal-close entry refuse new work. Cleanup semantics, every accepted input sample, stabilization, warning thresholds and capacity limits are unchanged.
- Error/status follow-ups stop when a notification destroys/closes the owner or switches its context. The camera/pixel algorithms, source/cache metadata, export formats and numeric GPU tolerance are unchanged.

## Added validation

The original failing test stays unchanged in the existing scheduling group (expected91). A separate `gesture-owner-lifetime` CTest group adds34 close/destruction rows and three foreign/closed/reentrant cases (expected39 Qt results). Independent saved SQLite state/history and source hashes must match the accepted commit boundary; stopped owners must not restart work or alter their frozen model/status. Capacity/recovery cases use the existing production16384-dab limit, not a lowered test limit.

All original102 CTest groups remain, plus the new group: expected103 tests/all97 Qt result groups. All original deadlines remain, including120s for both scheduling and gesture lifetime; the new group receives the same120s limit. Core276/performance44/Move49/session23/CLI92/Python283, original33 distinct native/validation contexts and all15 runtime guards remain required. New Release, mandatory GPU82/real-Sony preflight, targeted13, exact default PNG16/ICC, full103/native/platform/installed88/strict delivery are pending.

Identical queue headers retain focused parent ASAN evidence only, not a sanitized controller/application. No source-photo writes, native-FP32 RAW adoption, worker/admission/cache budget, GPU40/65535 threshold or deadline changes. Main/protected PR15 remain unmerged; independent camera calibration/A7R VI photographs and physical-device acceptance are incomplete.
