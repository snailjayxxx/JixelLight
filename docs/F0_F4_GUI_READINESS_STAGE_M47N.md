# F0/F4 M4.7N: stable popup ownership and final GPU presentation

The virtual-copy delete confirmation now belongs to the window's Overlay. Its
centering and width use that stable parent, with the existing460px cap and40px
window margin. A Library Loader can change size while its wrapped text is being
measured; using that intermediate Library size could re-enter the Dialog's
implicit-height calculation. Modal behavior, wrapped80-character names,
Cancel/Delete actions and original-photo/history preservation remain.

The original full-App smoke now waits for actual GPU presentation when GPU is
required. CPU image and full statistics readiness can precede the queued
`gpuPresented` notification. Previously, that condition stopped the timer before
the final GPU predicate ran. The completion policy now includes GPU readiness
before stopping, while retaining the strict final GPU predicate and60,000ms
deadline. CPU-only completion remains independent of GPU readiness. Reports add
required/enabled state, wait ticks and the first final-statistics-ready time with
GPU still pending. These are observations, not substitutes for presentation.

Changed code is limited to `ui/LibraryWorkspace.qml`, `app/SmokeRun.cpp`,
`app/SmokePhasePolicy.h` and the existing policy regression in
`tests/CoreTests.cpp`. Controller, Core processing, RAW, colour, brush arithmetic
and export kernels are unchanged.

## Original committed failures remain failed

[CI308](https://github.com/snailjayxxx/JixelLight/actions/runs/40823773110) on
M47M/f8119ca is **failed**. All68/all3 pass zero-fail/skip in
Linux/Windows/macOS933.88/1,725.53/1,512.54s; macOS native flows and its actual
24-mode SDK-stripped package pass the unchanged strict auditors.

Linux passes the preceding owned canvas, real Sony, pressure, exact export,
RAW matrix and precision flows, then fails the original basic GPU smoke at
phase50/19,243ms: all UI flags,23 captures and full10,340,960-pixel statistics
are ready, but `gpu_active` is false. Its later diagnostic/basic CPU flows are
unreached. Windows owned CPU/GPU passes in40,862/23,436ms, and its guide completes
14 native contexts,7 checks and5 captures in7,097ms; the wrapper correctly fails
on the same `LibraryWorkspace.qml:232` implicitHeight binding loop. M47M's content
wrapper alone did not resolve that Windows warning. Windows Sony GUI/package
are unreached.

All4 produced validation ZIPs,3 complete actual-checkout
`f4f51dc90b83e52001e3df951df46bf4fe3b84e6` logs, matching tree
`bae0dd2617c5dd6a68a244275046221d4dacabb6` and source213 are preserved at
`evidence/ci-40823773110/terminal-failed-v1/receipt.json` (1,815files).
Neither failed report is rewritten or promoted to acceptance.

## Paired actual-application evidence

A private350ms delay of the real queued GPU-present notification reproduces the
old early finish in the actual full App: GPU enabled, two notifications pending,
all UI/full statistics ready, exit2 at14,561ms. The candidate waits8 ticks and
passes actual Metal presentation at14,727ms. Stock baseline and normal candidate
CPU/Metal DPR1/2 flows also run. All7 runs/161 original PNGs, including the failed
controlled baseline, remain at
`evidence/final-GPU-present-private-v1/receipt.json` (434files).
The renderer delay is private QA and is absent from production sources.

The actual App's original compiled Library UI also reproduces the same line232
warning when controlled transient Library sizes and locales change: two warnings
at each of CPU DPR1/2. Its native copy actions complete, while the strict QML
observer correctly remains failed. With the same private QA and the Overlay UI,
both size/long-name/English-Chinese Cancel/Delete flows pass in2,328/2,869ms;
the original14-context guides pass in3,139/3,462ms. Source/history are preserved.
The40 records/22 original PNGs and standalone Qt6.8.3 experiments are at
`evidence/library-dialog-zeroextent-private-v1/receipt.json` (307files).
Failed width-clamp, direct-width and explicit-height attempts remain archived.

These controlled reproductions support the changes. They do not establish that
the actual Linux/Windows CI failures are resolved; the new committed CI must
confirm that independently.

## Normal Root validation

Serial normal App/CoreTests/ScopeTests builds pass. CMake advances the compiled
base stamp from68fff98 tof8119ca and rebuilds Core, so fresh **Core215/0/0** runs
in20.8196s alongside all five scope suites, including **scheduling12/0/0** and the
checksum-pinned genuine Sony native scope test. Six suites pass in55.11s.
The subsequent UI-only App build reuses those exact Core/regression binary bytes.

The normal production App passes the original full-App public Leica DNG smoke:

| Flow | Elapsed ms | Final statistics pixels | Original captures |
| --- | ---: | ---: | ---: |
| CPU DPR1 | 13,531 | 10,340,960 | 23 |
| Metal DPR1 | 14,370 | 10,340,960 | 23 |
| CPU DPR2 | 18,595 | 10,340,960 | 23 |
| Metal DPR2 | 18,600 | 10,340,960 | 23 |

Every original UI/statistics/GPU predicate and60,000ms gate remains. This Leica
fixture is not Sony acceptance. The normal App's original14-context guide passes
CPU DPR1/2 in3,379/3,484ms, keeping all7 checks and5 captures each. Private QA
linked against that normal App's actual main/Core/all compiled UI passes the
controlled Library-size/80-character copy-name/locale/Cancel/Delete flows in
2,350/2,922ms, with3 captures each. Four CLI guards pass.

Independent audit0 archives **8 actual App flows,34 guide/dialog records and108
original PNGs**, commands, binaries, source213 and original failure receipts at
`evidence/F0-F4-M47N-root-local-v1/receipt.json` (392files). Actual Root DPR2
Chinese confirmation and private DPR2 English confirmation captures were visually
inspected. Daily preferences are unchanged. The first archive observer's missing
sibling-helper import is preserved; correcting that observer required no native
rerun or report/gate/source modification. Original local f8119ca stamps remain,
requiring exact committed-source fingerprint proof rather than retagging.

## Remaining acceptance

Own committed all68/all3, both actual24-mode packages,5 original validation ZIPs,
3 complete runtime source logs and source213 publication matching remain required.
The original native auditor and7-check guide helper are unchanged. Latest fully
accepted implementation remains K/1fbfe71/CI304.

Original45s owned GUI,60s Sony/help/basic GUI,120s export/reference partitions,
32 Mi source pixels, shared3 GiB and colour/precision gates remain; reservation is
not RSS. Owned modal PNGs and Leica RAW do not replace genuine Sony validation.
Physical input/native OS activation, native GPU brush kernels, native FP32 RAW and
remaining F0–F6 work remain unfinished. PR16 stays Draft.
