# F4 M141: queued brush recovery after accepted close

CI441 at da2395c is genuinely failed on macOS: full104 has103 passing groups
and a gesture-owner failure,43 pass/1 fail/0 skip/0 blacklist. In
`brush-end-recovery-close`, the terminal snapshot's recovery-present flag
changes from true to false while the event loop drains. Core277 and the
separate catalog138 group pass. The original1,257,990-byte Mac log remains
failed with SHA256
`d60b0f656a8ba424b0e5b7826216f45630759f65b9c2808d724ce5e13c334ede`.

Source inspection finds an unguarded queued recovery-expiration callback for
backend/activity/preview-geometry changes. After close, availability correctly
returns false, but the callback can then call unguarded recovery discard and
mutate the closed owner. This is a causal hypothesis until the new controlled
baseline runs, not an invented observation from the CI trace.

Six new data rows create actual capacity-prefix recovery through public brush
operations, queue each production signal before or after accepted close, and
deliver its MetaCall events. They require the recovery object, state/history,
revision/serial, terminal status, notification count, independent saved project
and source bytes to remain unchanged. A seventh case requires foreign-thread
and post-close direct discard to refuse mutation. All original gesture44 and
scheduling91 method bodies remain unchanged. The dedicated group expects9
Qt results and retains120s; the graph becomes106/all100 with the existing18
runtime identity guards. No production method changes in this checkpoint.

Parent79547f7 passes Python283, a fresh112.45s build, separate native5/legacy82
mandatory GPU/Sony preflight, targeted14/119.75s, exact default PNG16/ICC and
all development desktop checks. Its full105/native-final/platform acceptance
has not run; the actual CI failure takes priority before that submission.
Parent ba5804e has independently closed local104/1011.44s/all98/all33/all17
and archive30, with no own platform acceptance. Neither result accepts this
new source. The controlled failing baseline, repair, complete local106/all100,
native33 and own three-platform/installed88/strict delivery remain pending.
