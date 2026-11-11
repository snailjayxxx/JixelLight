# F0 M4.7O: stable delete-copy measurement during Overlay initialization

The delete-copy confirmation retains its existing 460px desktop width while the
window Overlay initializes. Its measured wrapped text no longer depends on the
Overlay's transient width. The application's minimum logical window width is
1180px, so this keeps the normal confirmation size at both supported test widths
1180 and 1540. Window Overlay ownership, centering, the measured content wrapper,
modal behavior, bilingual text and Cancel/Delete roles remain.

Only `ui/LibraryWorkspace.qml` changes code in this slice. All other 212 code
sources, including M47N's required final GPU-presentation wait and its strict
60,000ms completion predicate, are unchanged. Controller, processing, RAW,
colour, brush arithmetic and export kernels are unchanged.

## CI309 remains failed

[CI309](https://github.com/snailjayxxx/JixelLight/actions/runs/40834405751) tests
M47N/d2b123f. All three platforms pass 68/68 with zero failures or skips:
Linux 1,097.29s, Windows 1,767.11s and macOS 1,562.09s. Windows and macOS native
flows and both actual 24-mode SDK-stripped packages pass the original strict
auditors. Full genuine Sony flows retain 32,947,264 source/statistics pixels,
exact platform-local PNG16/ICC export references, shared 3GiB and original
60s GUI /120s export-reference partitions.

Linux owned CPU/GPU canvas passes in 15,544/18,786ms, retaining full current
2,659,021 histogram/plot pixels and all original pan/capacity predicates. Its
guide completes native 14 contexts, seven checks and five captures in 3,032ms,
but the wrapper correctly fails on two `LibraryWorkspace.qml:232` Dialog
`implicitHeight` binding-loop warnings. Moving the popup to Overlay ownership
and retaining an Overlay-dependent width in M47N did not resolve this actual
Linux failure. Later Linux genuine Sony GUI and basic GPU smoke are unreached;
the final-presentation wait is still unproven in that actual Linux flow.

All five original validation ZIPs, three complete runtime logs, source213 and
failed reports are preserved at
`evidence/ci-40834405751/terminal-failed-v1/receipt.json` (2,151 files).
The full logs prove actual checkout
`e23df6aeacaf46bf80fb2107f1d84d86e417400c`, whose tree
`8641e0f542f7482186295e63c7bf93246a2f1fcd` equals d2b123f. Linux's separate
failed-only archive has 458 files. Its wrapper stays false; no success finalizer
runs for CI309. Earlier CI308/307/306/305 and local failures remain separate.

## Controlled actual-application reproduction

Earlier Library-size probes did not cover transient geometry of the Overlay
itself. Private QA now verifies the actual dialog parent is `QQuickOverlay` and
controls its width through 0,16 and restored geometry before the copy actions.
With the same actual M47N main/Core/all compiled UI, the committed UI reproduces
two original line232 warnings at each DPR. Native copy actions pass, while the
unchanged strict QML observer correctly fails. These baseline failures remain.

The paired fixed460 candidate passes six positive App flows: long80-name
English Cancel, Chinese Cancel and English Delete at window1180/1540 and DPR1/2,
plus both original14-context guides. Forty positive guide/dialog records,
28 original PNGs including the two failed baselines, commands, exact component
hashes and source213 remain at
`evidence/library-dialog-overlay-width-private-v1/receipt.json` (305 files).
Only the QA controls transient geometry; production contains no injected delay,
warning filter or test-only width mutation. This controlled reproduction does
not establish actual Linux resolution.

## Normal Root validation

The serial normal App/CoreTests/ScopeTests build passes. CMake advances the actual
compiled base stamp from f8119ca to d2b123f and rebuilds Core, so fresh
Core215/0/0 runs in 21.3086s with all five scope suites,
including scheduling12/0/0 and the checksum-pinned genuine Sony native scope case.
The six suites pass in 55.56s; prior binaries are not reused as fresh results.

The normal production App passes the original public Leica DNG full-App smoke:

| Flow | Elapsed ms | Final statistics pixels | Original captures |
| --- | ---: | ---: | ---: |
| CPU DPR1 | 13,415 | 10,340,960 | 23 |
| Metal DPR1 | 14,621 | 10,340,960 | 23 |
| CPU DPR2 | 18,253 | 10,340,960 | 23 |
| Metal DPR2 | 20,082 | 10,340,960 | 23 |

All original UI/statistics/GPU predicates and the 60,000ms gate remain. The normal
original guide retains all14 contexts, seven checks and five captures. Additional
private QA links only its driver override against this normal App's exact
main/Core/all compiled UI, checking actual Window Overlay transient widths,
80-character names, bilingual Cancel/Delete, wrapped-text fit, bounds and original
photo/history preservation at both desktop widths. It uses an owned 384x256
RGBA64 PNG, which is not genuine Sony evidence.

| Flow | Elapsed ms | Guide/dialog records | Captures |
| --- | ---: | ---: | ---: |
| guide-cpu1x | 3,233 | 14 | 5 |
| guide-cpu2x | 3,441 | 14 | 5 |
| copy-minimum-cpu1x | 2,315 | 3 | 3 |
| copy-minimum-cpu2x | 2,861 | 3 | 3 |
| copy-wide-cpu1x | 2,490 | 3 | 3 |
| copy-wide-cpu2x | 3,088 | 3 | 3 |

Four CLI guards pass. Independent audit0 archives ten actual App GUI flows,
40 guide/dialog records, 114 original PNGs, commands, binaries, all213 tested
source files and original failed receipts at
`evidence/F0-M47O-root-local-v1/receipt.json` (408 files). The normal Root DPR2
minimum-window Chinese and wide-window English confirmation captures were
visually inspected. Daily preferences remain unchanged. Raw local d2b123f stamps
are retained; exact213 committed-source fingerprint matching is required for the
publication, rather than retagging local reports.

## CI310 committed acceptance

M47O/644942a is fully accepted by
[CI310](https://github.com/snailjayxxx/JixelLight/actions/runs/40845703559).
All68/all3 pass zero-fail/skip in Linux703.52s, Windows1,762.98s and macOS1,398.51s.
Both actual24-mode SDK-stripped packages and all native strict auditors pass.
Guides14/checks7/captures5 pass2,940/7,194/7,414ms without QML errors. Linux's
previous Library232 warning is absent; its later basicGPU flow passes15,910ms,
actualGPUactive/full10,340,960/all original UI predicates under60s. Windows/Mac
basicGPU pass34,325/27,915ms. All three report zero finalGPU wait ticks; the
controlled notification-gap experiment remains separate evidence.

Genuine Sony pressure GUI CPU/GPU is Linux10,224/11,442ms, Windows54,448/30,048ms
and Mac19,072/18,943ms under60,000ms. Export/reference CPU/GPU partitions are
41,887.385/41,833.430ms,80,349.798/79,981.497ms and52,835.203/52,553.966ms under
120,000ms. Full32,947,264 source/histogram/plot, exact platform-local PNG16/ICC,
shared3GiB and all original numeric/input/source/refund predicates pass.

Five original ZIPs and three full logs prove all actual checkouts
469329f74a6177ce21dde7bb91902d1bf3a9a9e7/tree4bbd2ad8658ab1f13ca850e7686d690ae42fed6a
equal644942a, with213 publication fingerprints and strict finalizer0. Evidence is
`evidence/ci-40845703559/terminal-accepted-v2/receipt.json` (2,415files).
The original native auditor and seven-check guide helper are unchanged. The
initial derived reporting label used GUI-plus-export-write elapsed as GUI; v2
corrects only that metadata from original gui_elapsed_ms and combined export
reference wall time. The first receipt, Mac CTest-summary reader mismatch and
expired reference attempt remain preserved; original reports/gates/source are
unchanged, with no native rerun. CI309 remains failed. The following
[M47P caption slice](F0_BACKEND_CAPTION_LOCALE_STAGE_M47P.md) adds new caption
coverage and needs its own committed acceptance.

Original45s owned GUI,60s Sony/help/basic GUI,120s export-reference partitions,
32Mi source pixels, shared3GiB and colour/precision gates remain; reservation is
not RSS. Owned modal images and Leica RAW do not replace genuine Sony evidence.
Physical input/native OS activation, native GPU brush kernels, native FP32 RAW
and remaining F0–F6 work remain unfinished. PR16 stays Draft.
