# F0 edit-notification close durability — M128 regression

Status: new regression, before compilation; no product change yet. Parent37c2e4f independently passes local102/980.27s, all33 contexts/fifteen runtime guards and all three CI438 jobs, both installed88 desktop packages and strict delivery. The clean root is that accepted commit; main/protected PR15 remain unmerged.

`persistAndApply`, Undo and Redo currently notify observers before marking the updated state/history dirty. A synchronous successful `beginClose()` from a notification may therefore finish persistence before the last edit reaches the writer. Continuing after such a notification can also restart work or access a destroyed controller. This is a source finding, not yet a reproduced failure.

The new unchanged-production regression creates a real project, flushes its initial state, requests terminal close directly from the history notification of a +0.75EV edit, then reads an independent saved SQLite snapshot before controller destruction. Both the final edit and its exact history must already be durable when close is accepted. A separate focused run will establish the result before any repair; original output/build/source will be retained if it fails.

No render math, RAW precision, source-photo writes, cache/admission capacity, worker policy, GPU tolerance or deadline changes. Native-FP32 RAW calibration, A7R VI photographic and physical-device acceptance remain incomplete.
