# F4 M4.7F: reuse the exact histogram frame for scope plots

The previous E run finishes the Windows genuine Sony CPU Paint/Erase workflow at
60880ms, beyond its unchanged60000ms gate. It passes held Paint/Erase RGB,
Esc, one Undo, deletion, reopen, full32,947,264-pixel scopes and compact English
controls, but completes final capture/hash/validation too late. The final full
waveform capture is at59000ms and English capture at60384ms. Every three-platform
65-test suite passes; the actual macOS17-mode SDK-stripped package also passes.
E remains failed and C remains the last fully accepted aggregate.

Exact histograms publish completed pre-monitor RGBA64 pixels into the shared
analysis cache. Previously the plot timer could submit while that histogram
was still scheduled or running, miss its incomplete frame and independently
render the same full source. The controller now defers plot submission with its
existing GUI timer until that producer completes. Workers do not wait on other
workers, and cancellation tokens remain independent. The ordinary streamed plot
fallback remains available when a histogram fails or cannot retain a frame;
turning off exact statistics can immediately produce a preview plot.

This changes scheduling, not rendering, histogram bins, scope ink, monitor ICC,
export pixels, cache budgets or ownership. Native reports include deferral and
completed-frame counters plus the analysis-cache snapshot. Deadline failure
during final validation now has an explicit error rather than an empty string.
The complete elapsed validation time still counts toward the same45/60s gates.

## Regression and validation

A deterministic held histogram fixture proves that plot submission defers while
scheduled and running, while a GUI turn still executes. Four data rows compare
all three plot images against independently rendered pixels: completed-frame
reuse, uncached streaming, failed histogram and cancelled exact/preview fallback.
They require unchanged edit intent/history/source. Before the fix all four fail
against the unchanged E static core because a scheduled histogram does not defer
plot submission. The original negative report is retained. The new CTest shard
retains its120s bound and joins the original65 tests, for66 total.

The complete App builds serially. All four new regression rows pass (Qt6/0/0).
All twelve actual native CPU/Metal, opaque/transparent/genuineSony,1x/2x
workflows pass every original predicate and twelve controls-only captures.
All twelve use a completed analysis frame; all four Sony runs record6-7
non-blocking GUI deferrals while the histogram finishes. Native Sony times are
CPU13436ms/Metal12662ms at1x and15547ms/15105ms at2x. Held-to-committed RGB
stays within1/255, below2/255. Full32,947,264 pixels, source identity,3GiB
admission and the original60000ms limit remain.

The affected older Sony CPU/Metal1x workflows pass at9767ms/9364ms, and older
RAW CPU/Metal1x at15136ms/15024ms with all23 screenshots,10,340,960 pixels
and60000ms. Native maximum operation reservation is1,726,695,808 bytes,
not RSS. Actual English Sony1x and transparentMetal2x controls crops have
been inspected; waveform, Erase selection and Local EV are visible.
The existing output-alias guards are unchanged and retain E's original twelve
passing copied-Sony exact/symlink/hardlink refusals; they are not rerun here.

Full local66/66 passes in710.01 seconds with zero failures/skips. Its63 exact
Qt reports and three real-Sony/CLI tests are audited from the actual CTest plan
and JUnit result. Core215, GPU36, performance39, Look21, admission17,
display-ICC4 and the four new scheduling rows (Qt6) pass. All18 admitted GPU
rows retain maximum0/40, including Paint/Erase; exact source/export/ICC,
full-source scopes and budget-refund predicates remain. All18 alpha draws retain
the independent monitor ordering/working-byte invariants. The suite runs once.
Original sources, daily preferences and four compiled source hashes stay unchanged.

Evidence is in the physical build parent's scope-scheduling-native-v1/,
scope-scheduling-legacy-gui/, scope-scheduling-local-post-audit.json and full66
log/XML. The old-core four-row negative regression remains under the private
scope-scheduling prototype. Original local4d7281b stamps are preserved and linked
to the publication's exact tested non-documentation source hashes.

## Preserved original E evidence

[CI40551927782](https://github.com/snailjayxxx/JixelLight/actions/runs/40551927782)
checks out actual1c850c27835e35ea6fc43251405c83c3a95197cd on all three jobs,
whose tree3d0c9f9fa502597232649095722b118da1cad9e4 matches4d7281b.
Linux65/65 in1022.03s, Windows65/65 in1773.20s and macOS65/65 in1420.46s
have zero skips and Core215 each. The18 analytic alpha draws retain the1/255
presentation limit and working-byte invariants. macOS's actual17-mode package
passes, including genuine Sony CPU25820ms/GPU27409ms. Windows's remaining Sony
eraserGPU, RAW-matrix and package do not run after its CPU native deadline fails.
No Windows package manifest or validation ZIP was produced. Four available
original ZIPs, three full original job logs, actual checkout/tree proof and the
failed summary are archived under the physical build parent's evidence/ci-40551927782/.

F requires its own committed66/all3/Core215/display-ICC4/new schedulingQt6
and all previous numerical/native checks, both actual17-mode SDK-stripped
packages, five original validation ZIPs, three full job logs and actual
checkout/tree proof. Local success does not establish a Windows deadline repair
or complete F4/F0-F6. C remains the last fully accepted aggregate until a newer
run passes every required gate.

Source32Mi/shared3GiB, research2Mi/tile8Mi/work96Mi, color precision and all
45/60/120s gates remain. Operation reservations are not RSS. Native GPU brush
processing, native FP32 RAW, physical pressure-device acceptance and remaining
F0-F6 work remain unfinished. PR16 stays Draft; main and PR15 are untouched.
