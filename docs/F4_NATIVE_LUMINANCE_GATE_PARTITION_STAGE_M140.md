# F4 M140: independently bounded native luminance GPU qualification

The original accepted CI438 Windows log records the existing GPU suite at
110.67s in preflight and110.66s in full102, with its unchanged120s CTest limit.
This is measured at37c2e4f, not a claim that ba5804e has failed on Windows.
Adding156 native brush comparisons to that group risks exhausting its small
remaining margin. Give the added workload a separate mandatory group, while
retaining the original suite and every120s limit. No timeout is increased.

`JixelLightNativeBrushLuminanceGpuTests` now owns the three new methods and
their two private helpers. Every moved method body and the actual QRhi
initialization are byte-identical to ba5804e; all other legacy GPU methods
retain their original names, order and bodies. The legacy suite requires82
Qt results and the new suite requires5, including its separate initialization
and cleanup. Those additional lifecycle results are not new image cases.
All156 numeric comparisons, exact float alpha/actual-pixel histograms, eight
source/precision policies and operation-charge checks remain.

The new `native-brush-luminance-gpu` CTest group has its own120s timeout and
normal Windows console entry point. Mandatory preflight runs all three of
real Sony, the legacy GPU suite and the new GPU suite. It independently
requires their JUnit rows and both complete Qt reports, refuses occupied
paths, preserves both original report streams and never copies photographs
into preflight evidence. Six Python wrapper tests pass, including actual
CTest with spaces and new missing/skipped/failed/incomplete/duplicate-result
refusals. The overall Python test-method count remains283.

App/core/shader/UI source bytes remain identical to ba5804e. Its local
full104/all98/all33/seventeen-runtime run continues on its frozen source;
the first wrong-directory helper refusal remains excluded before product
execution, with a separate corrected-stage runner and unchanged runtimes.
This new source does not substitute that parent's local or platform result.

The registry becomes105 CTest groups /99 Qt result files. The separate new
GPU binary expands the frozen identity guard to18 runtimes. Fresh build,
mandatory three-group preflight, full105/all99/all33, own three-platform CI,
both installed88 desktop packages and strict delivery remain pending.
No additional local heavy build overlaps the parent's frozen qualification.
Per-packet native live brushes, calibrated float RAW, physical-device evidence
and the remaining F0–F6 scope stay unverified or unfinished.
