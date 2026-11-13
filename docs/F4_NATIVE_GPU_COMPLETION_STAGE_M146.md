# F4 M146: completion-driven native GPU test preparation

Parent9f5c757 passes native8/336 actual GPU-reference comparisons on Metal in
19,096ms (driver19.48s), maximum RGB16 error26 under the unchanged40 bound.
The per-case helper currently polls its CPU preparation future before every
native dispatch. This checkpoint connects the actual watcher finished signal
before submission and waits for that signal, checking both the connection and
the completed future. A future already complete returns directly. This removes
repeated polling latency without reducing the work or fabricating completion.

The preparation allowance remains10,000ms, the mandatory native group120s,
and the complete registry106 CTest groups/100 Qt reports/18 runtime guards.
All336 cases,18 admission policies, owning CPU references, real coverage and
colour dispatches, exact alpha/histograms, finite output, RGB40 threshold,
resource release and finish/readback conditions remain. All six test methods,
other helpers, production sources, shaders and preflight are unchanged.

Accepted CI438 previously used118.12 of120 workflow minutes on Windows, and
its legacy GPU group110.66 of120 seconds. The legacy group was already split
in M140; this change addresses idle preparation waits in the separate native
group. It does not claim that parent9f5c757 timed out or establish a product
performance gain. Fresh timings and every existing gate must still run.

Parent9f5c757 development passes Python283, fresh Release118.90s, targeted15
in122.38s, exact default-display parity and native application checks. Its
frozen full106/all33 and own PR87/CI443 are still running. This source must not
compile until those local/native/archival processes close. Parent platform
success cannot accept this new head. Parent31e906e's first-English picker
deadline remains failed and unexplained; this separate GPU test-scheduling
change does not claim to fix it. No new runtime, full/native/platform/package
qualification or speedup is claimed at this checkpoint.
