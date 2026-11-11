# F0 M4.7M: measure virtual-copy confirmation text at its allocated width

**Actual committed result:** CI308 remains failed. All68/all3 and the actual
Mac24 package pass, but Windows still reports the same Library delete Dialog
implicitHeight warning; Linux also fails its later basicGPU completion check.
All produced validation originals and runtime source logs remain at
`evidence/ci-40823773110/terminal-failed-v1/receipt.json`. The content wrapper
alone did not resolve Windows. [M47N](F0_F4_GUI_READINESS_STAGE_M47N.md) adds paired
actual-App reproductions, window Overlay ownership and required final GPU waiting;
its own Linux/Windows and whole-stage acceptance remain pending.

The virtual-copy delete confirmation now puts its wrapped Label inside an Item
whose implicit height comes from that Label. The Label uses the allocated content
width; its implicit width no longer participates in the popup's size calculation.
The existing modal confirmation, title, footer, actions and460px width remain.
Only `ui/LibraryWorkspace.qml` changes in production code. Controller, Core, RAW,
colour, brush arithmetic and export kernels are unchanged.

## Original committed failure

[CI307](https://github.com/snailjayxxx/JixelLight/actions/runs/40816071704) on
`68fff98` stays **failed**. All68/all3 pass zero-fail/skip in
Linux/Windows/macOS713.19/1,768.10/1,360.28s, including Core215 and scheduling12.
Linux/macOS native flows and the actual macOS24-mode SDK-stripped package pass.

Windows owned CPU/GPU canvas now passes the unchanged45,000ms gate in
**41,985/24,073ms**, retaining all24 flags, pan guards,7 refusals/4 recoveries and
current histogram/plot each2,659,021. Its guide completes all14 native contexts,
all7 wrapper checks and5 verified PNGs in7,140ms. The wrapper correctly fails on
`LibraryWorkspace.qml:232` / delete-copy Dialog / `implicitHeight` binding loop.
Windows Sony GUI and package are unreached. This differs from CI306's original
45,074ms waveform timeout, which remains preserved with its cause unproven.

All4 produced validation ZIPs,3 complete actual-checkout
`8468603db487132f538b1690b57b5e4b30e56e13` logs, matching tree
`f2f487b2bc47fcbfd44f709cc3d30566c376d296` and source213 remain at
`evidence/ci-40816071704/terminal-failed-v1/receipt.json` (1,823 files).
The failed Windows guide is not retagged; no error marker or timer is relaxed.

## Actual local validation

The serial normal App build and regression-target build pass. CMake updates the
base build stamp from531380d to68fff98 and rebuilds Core, so fresh regressions run:
**Core215/0/0 in20.7682s**, plus all five scope suites, including scheduling12/0/0.
The genuine Sony fixture remains checksum pinned; the native scope suite runs
with real-source-required enabled. Existing120s scope partitions stay unchanged.

The normal production App passes its original14-context guide on actual
CPU DPR1/2 in **3,203/3,477ms**, retaining5 captures each, modal input, focus,
held pressure, source/history and60,000ms checks. Four original CLI guards pass.
A separately linked private QA entry reuses that normal App's actual main, Core
and every compiled UI object. It drives create/open/cancel/accept and header locale
controls for an80-character copy name, verifies English/Chinese wrapped text and
preserves the original photo, intent and history after deleting only the copy.
These actual Root-UI probes pass DPR1/2 in **2,386/2,723ms**.

Independent audit0 retains **34 actual UI records,16 original PNGs**, component
boundaries, binaries, commands and213 tested source fingerprints at
`evidence/F0-M47M-root-local-v1/receipt.json`. The actual DPR2 Chinese dialog and
private DPR2 English confirmation captures were visually inspected. Daily
preferences are unchanged. Raw68fff98 report stamps remain unchanged and require
exact committed-source matching; they do not identify the future publication.

Private baseline/candidate evidence is separate at
`evidence/library-dialog-height-private-v2/receipt.json` (40 records/22 PNGs).
The original Cocoa baseline also passes: the Windows binding loop is not
reproduced locally. The first private QA matched the background's identically
labelled delete action; its phase9 timeout and controlled stop stay archived.
The corrected QA scopes clicks to the popup footer. No product deletion failure
is inferred from that QA selector error.

## Remaining acceptance

The Windows correction and whole-stage acceptance require this code's own
committed all68/all3, both actual24-mode packages,5 original ZIPs and3 complete
runtime source logs. The existing strict auditor/helper gates remain unchanged.
Latest fully accepted implementation stays K/1fbfe71/CI304. Original45/60/120s,
32 Mi source pixels, shared3 GiB and precision gates remain; reservation is not
RSS. Owned dialog PNGs do not replace genuine Sony RAW acceptance. Physical input,
native OS activation, native GPU brush kernels, native FP32 RAW and remaining
F0–F6 stay unfinished. PR16 remains Draft.
