# F1 Stage 1: discoverable keyboard help

The toolbar now has an English/Chinese **Keyboard shortcuts** button. It opens
a compact, scrollable guide for Library/Develop, adding existing photos,
Undo/Redo, brush radius, zoomed Space-drag navigation and crop Return/Escape.
Open uses the native Qt primary binding; Undo/Redo list all current platform
bindings. Text uses the existing application language and wraps within the
680-logical-pixel guide at the 1180×720 minimum window size.

The help button uses explicit `Qt.TabFocus`: opening by mouse preserves the
editor focus, while its Tab policy remains available. The guide helper records
the actual focus object, button policy and brush focus without changing input.

The modal guide owns its keyboard input. Tab/Backtab cycle between its content
and Close; Escape dismisses it from either location. G/D, native Open/Undo/Redo,
brush-radius keys, Space and Return cannot reach the background workspace while
the guide is open. Closing it preserves crop state, edits, source and history.
Opening it during a controlled held pen stroke cancels the transient stroke;
the cancelled owner remains held until primary lift, after which a fresh press
can paint. Existing brush Escape/pan behavior remains when the guide is closed.

## Direct UI coverage

The owned-source helper uses the actual App entry point, compiled QRC/AOT UI,
PhotoController, native Qt key map and controlled Qt platform events. Protocol 2
starts on **Library workspace 0**, with the actual Library and photo grid visible
and grid count zero. It opens the guide, checks native captions, blocks G/D and
standard bindings, then closes it. The remaining thirteen cases run in Develop
workspace 1, including compact bilingual layout, modal keys, content/footer
focus, resize, pan, crop and held-pressure ownership.

The crop context is explicitly prepared through internal `beginCrop`, and the
held-pen help-button focus setup is explicit. Those setups are recorded in the
report. The interactions and visible guide/canvas results use actual UI events
and original captures; they are bounded Qt validation. Physical keyboard/tablet
drivers and native OS activation remain unverified.

The new exclusive `--shortcut-help-smoke-report` mode rejects simultaneous smoke
modes and positional photographs. It uses process-owned INI settings. Its
production wrapper requires protocol, read-only context, native labels, compact
layout, modal keys, held pressure and capture SHA/dimensions/DPR gates, retaining
the existing **60,000 ms** GUI deadline. Native CI runs this owned-source guide
check, and Windows/macOS deployment adds it as a mandatory **24th mode**, keeping
all old 23 modes and all 68 CTest checks.

## Root focus-correction validation

The serial production App/Core/export-verifier dependency build passes. Its
refreshed build stamp changes the Core binaries, so a fresh **Core215/0/0** runs
and passes in28.4159s. All4 actual CLI guards pass. The accepted local matrix
contains18 distinct actual UI workflows: two guide, four canvas, eight
opaque/transparent pressure and four genuine Sony flows, using measured DPR1/2
and CPU/Metal where applicable.

| Root GUI, ms | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Shortcut guide | 3,622 | — | 8,499 | — |
| Existing canvas | 25,904 | 20,696 | 20,163 | 20,060 |
| Genuine Sony pressure | 9,865 | 9,783 | 10,011 | 10,304 |
| Sony export/reference partition | 34,492.979 | 34,307.102 | 34,789.443 | 34,256.018 |

All prior capacity/refusal/recovery, input ownership, pan, radius, locale, alpha,
live/committed RGB and full-source scope/export assertions remain. Every Sony
export retains32,947,264 pixels and exact platform-local PNG16/ICC SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.
Independent audit0 preserves250 original native PNGs, reports, binaries and all
213 tested code-source fingerprints at
`evidence/F1-shortcut-help-focus-root-local-v2/receipt.json`. Raw `4547fac` stamps
remain unchanged and require exact source matching at publication. The actual
Chinese DPR2 guide capture was visually inspected. Daily preferences keep their
original SHA256. Own committed three-platform/package acceptance remains separate.

The initial USB-artifact CPU2x attempt **fails at48,058ms against45,000ms**, at
phase43 early in capacity checking. Screenshot encoding/writing totals13,946ms;
window grabs total206ms. Its partial reports,56 PNGs, source213 and binaries
remain failed at `evidence/F1-help-focus-root-v1-failed/receipt.json`.
An isolated encoding/write comparison does not reproduce that cumulative cost;
the timeout cause remains unproven. No production source, deadline or capture
predicate is changed to make that original report pass.

A controlled continuation uses the **same frozen binary/source** with an owned
internal-SSD artifact directory. CPU2x passes at20,163ms with all7 refusal and4
recovery checks and final current histogram/plot each2,659,021 pixels. Four
already successful UI flows, fresh Core215 and4 CLI guards are reused; the
controlled case and13 previously unrun flows complete the matrix. Every original
capture byte is copied to the physical build/evidence directory after the
existing UI/wrapper timers finish, with SHA receipts. The artifact storage
change is explicit; it is not a product speedup claim or a reconstruction of
the original timeout. The original failure stays distinct from these results.

The earlier uncorrected Root guide's Core215/18-flow local record,250 PNGs and
source213 stay at `evidence/F1-shortcut-help-root-local-v1/receipt.json`, with raw
`1fbfe71` stamps. Those local passes did not establish Linux/Windows focus safety.

The previous private protocol-1 helper kept the empty dataset in Develop. Its
original reports retain that bounded scope. Protocol 2 adds the actual Library
page and visible grid, rather than treating an empty dataset as Library-page
coverage. Private Cocoa and requested offscreen/software DPR1/2 runs preserve
56 cases and 20 original PNGs at
`evidence/shortcut-help-library-v8/receipt.json`. The original full-App main,
Core and QRC objects are reused byte-exactly with only the new QA object; raw
`c078e2f` stamps and the two-file QA overlay fingerprints stay intact. That is
separate private evidence, not Root or Linux/Windows acceptance.

Earlier missing Undo/Redo captions, unreachable Close focus, held-pen continuation
behind the guide and the private compile error keep their original failed
reports and source. The first two are covered by native Qt labels and explicit
Tab/Backtab routing; disabling canvas input while the modal is visible uses the
existing held-owner cancellation path. Current Root validation and its own
committed CI supply separate results without retagging those originals.

## Scope and committed acceptance

The guide changes Main UI/context and validation wiring. RAW/colour/brush kernels,
export and persistence schemas are unchanged. All previous 45/60/120 s GUI and
export-reference gates, full genuine Sony 32,947,264 pixels, 32 Mi source-pixel
admission and shared 3 GiB reservation remain. Reservations are not RSS.
Native GPU brushes, native FP32 RAW and remaining F0–F6 work stay unfinished.
PR16 remains Draft.

Original [CI305](https://github.com/snailjayxxx/JixelLight/actions/runs/40794976750)
for `4547fac` remains **failed**. Old68 CTests pass with zero failures/skips on
Linux/Windows/macOS in1,087.73/1,207.23/1,437.74s; macOS also passes its strict
native checks and actual24-mode SDK-stripped package. Linux/Windows fail the new
guide at phase9: after dismissal, a ToolButton retains focus and Space does not
return to brush navigation. Their original guide times are1,490/2,932ms, with
unchanged source/history. Windows package validation is not produced.

All3 complete runtime logs prove actual checkout
`4f809323a745d3f5d8b63d1d17dd2376a49ad5a3`, tree
`d0882c18e3a324bad144d43684db8b6126efdadd`, equal to that original published head.
All4 produced validation ZIPs, full logs, source213 and original reports are
preserved at `evidence/ci-40794976750/terminal-failed-v1/receipt.json`; earlier
partial archives remain unchanged. The focus correction requires its own
committed68/all3 and both actual24-mode SDK-stripped packages. The prior
[K / CI304](https://github.com/snailjayxxx/JixelLight/actions/runs/40783796319)
acceptance and original evidence remain separate.

Later [CI306](https://github.com/snailjayxxx/JixelLight/actions/runs/40806921358)
on focus-corrected `531380d` remains **failed**. All68/all3 pass zero-fail/skip
in1,169.22/1,772.75/1,419.27s. Linux/macOS pass14 native guide cases in3,059/7,016ms;
macOS also passes its actual24-mode package. Windows's existing owned CPU canvas
passes7 refusals/4 recoveries before45,074ms/phase41 exceeds45,000ms waiting for
full waveform. Histogram2,659,021; plot0/is not current. Windows help/package are
not reached, so Windows guide acceptance cannot be inferred. Encoding414.021ms;
the original timeout cause remains unproven.

All4 produced validation ZIPs,3 complete actual104dcc0/treea8a7c72 logs and
source213 remain at `evidence/ci-40806921358/terminal-failed-v1/receipt.json`.
[M4.7L](F4_EXACT_SCOPES_FINAL_PREVIEW_STAGE_M47L.md) advances final-preview
statistics scheduling with separate local and committed acceptance.

Later CI307 completes Windows's original14 guide contexts,7 checks and5 PNGs in
7,140ms, with actual restored brush focus/navigation. Its wrapper correctly
fails on the separate Library virtual-copy Dialog implicitHeight QML warning.
Linux/macOS strict guides pass2,942/7,150ms and the actualMac24 package passes;
Windows Sony GUI/package are unreached. Original4 ZIPs/3 full actual8468603 logs
remain at `evidence/ci-40816071704/terminal-failed-v1/receipt.json`.
[M4.7M's dialog measurement change](F0_VIRTUAL_COPY_DIALOG_LAYOUT_STAGE_M47M.md)
keeps every guide case, capture, timer and QML error marker; Windows/all3/both24
acceptance remains required.
