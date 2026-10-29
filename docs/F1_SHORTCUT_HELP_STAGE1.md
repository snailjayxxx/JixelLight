# F1 Stage 1: discoverable keyboard help

The toolbar now has an English/Chinese **Keyboard shortcuts** button. It opens
a compact, scrollable guide for Library/Develop, adding existing photos,
Undo/Redo, brush radius, zoomed Space-drag navigation and crop Return/Escape.
Open uses the native Qt primary binding; Undo/Redo list all current platform
bindings. Text uses the existing application language and wraps within the
680-logical-pixel guide at the 1180×720 minimum window size.

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

## Root local validation

The serial Qt6.8.3/SDK15.4 Root App/Core/export-verifier build passes. Fresh
**Core215 checks pass with zero failures/skips in24.6906s**. Four actual command
line guards pass. The complete Root matrix passes **18 actual UI workflows**:
two guide flows, four canvas flows, eight opaque/transparent pressure flows and
four genuine Sony flows, with measured DPR1/2 and CPU/Metal where applicable.

| Root GUI, ms | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Shortcut guide | 3,355 | — | 3,424 | — |
| Existing canvas | 19,477 | 19,545 | 20,829 | 20,496 |
| Genuine Sony pressure | 9,777 | 9,854 | 10,279 | 10,366 |
| Sony combined export/reference | 51,274.853 | 49,015.941 | 67,158.254 | 34,674.510 |

All previous capacity, input ownership, pan, radius, locale, alpha, live/committed
RGB and exact full-source scope/export assertions remain. Every Sony export
retains the platform-local PNG16/ICC SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.
The independent Root audit exits0 and preserves **250 original native PNGs**,
complete reports, binaries and all **213 tested code-source fingerprints** at
`evidence/F1-shortcut-help-root-local-v1/receipt.json`. Original Root `1fbfe71`
build stamps remain unchanged and require exact source matching at publication.
Actual empty Library and Chinese DPR2 guide captures were visually inspected.
Daily preferences retain their original SHA256. These local results remain
separate from own committed three-platform and both24-mode package acceptance.

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

Own committed three-platform 68-test and both actual 24-mode SDK-stripped package
acceptance is pending. The prior [K / CI304](https://github.com/snailjayxxx/JixelLight/actions/runs/40783796319)
acceptance and its original evidence remain separate.
