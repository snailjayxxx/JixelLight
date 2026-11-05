# F4 M4.8O — steady-pen help for keyboard focus and accessibility metadata

## Behavior

The existing Steady pen ½px / 稳笔 ½像素 checkbox keeps its compact label, position, default-off and session-only state. Its existing complete explanation also appears when the control has keyboard visual focus. Mouse hover keeps showing the same text; mouse focus alone does not keep the explanation open. Space retains the ordinary checked/unchecked behavior and updates the editor choice.

The checkbox explicitly binds its accessible name to the visible translated label and its accessible description to that same translated explanation. The ordinary CheckBox role and checked state remain. This exposes metadata to Qt; it does not certify a screen reader or change system accessibility settings. Qt documents [visual focus](https://doc.qt.io/qt-6.8/qml-qtquick-controls-control.html#visualFocus-prop) and the [accessible description](https://doc.qt.io/qt-6.8/qml-qtquick-accessible.html#description-attached-prop).

Only three QML property bindings change in one source file. Layout, all labels/tooltips, checked/enabled/onClicked behavior, persistence, controller, input, sampler, renderer, document schema, QA and workflow remain. Half-pixel stabilization still changes selected raw-position intent, preserves every pressure packet and leaves mouse input unchanged. Full unfiltered unique-position jitter and physical-tablet support remain open.

## Private evidence and limits

The private source-snapshot component harness passed paired English baseline/candidate and Chinese candidate observations on Cocoa at actual DPR1. Both candidate languages show the complete explanation for controlled Tab-reason focus and hover, then hide it after focus transfers through the existing Mode selector and returns with mouse reason. Four candidate captures pass independent CRC, SHA-256, decoded-row-size and bounds checks and the two keyboard captures were visually checked. Qt interface queries return the translated name/description, CheckBox role44, checkable and checked state. Two Space events select the editor option and restore it off.

This harness uses controlled QML brush activation and synthetic focus/key events, with owned image/preferences. Qt global accessibility activation actually remains false; direct interface reads do not establish VoiceOver, OS activation or physical Tab navigation. Baseline interface details depend on that harness setup and are not a claim of an observed normal-app VoiceOver failure. The reused formal objects, five existing binaries, Root and user preferences remained unchanged.

Private receipt `evidence/F1-steady-pen-description-private-v1/verified-v1/receipt.json`, SHA-256 `cd43a0a3f34e0a3022875516d1ee0f11a1db30157b8d87aadac41d1ae7b83544`, seals 175 files. Original probe timeouts, diagnostic compile failure, incomplete-name candidate and focus-reason setup observation remain preserved; the final candidate is `qml-candidate-v2/DevelopRightPanel.qml`. These component checks do not count as a normal-app binary run, formal Root, cross-platform or physical assistive-technology acceptance. Completed private probes are adopted without rerunning them.

## Formal validation

The formal Root app target built serially in 120,609 ms after 58,852 ms configuration. Source232 stayed fixed: exactly the three bindings in DevelopRightPanel change and the other 231 files retain their accepted CI335 bytes. Four unchanged test/verifier binaries also retain their prior hashes; no fresh Core, pressure or Sony run is claimed.

A custom controlled main compiled and linked the actual new formal QML/Core objects, with the formal link arguments differing only in main and output. English and Chinese checks completed in 3,086/2,874 ms under the probe's 15,000-ms deadline, including exact translated name/description, role/check state, full keyboard/hover explanation, no mouse-focus residue, and two Space events synchronizing the editor option then restoring it off. Four help screenshots are CRC/hash/size checked; both keyboard frames were visually inspected. Actual Qt global accessibility remains false and physical VoiceOver/OS activation/Tab navigation remain unverified. These are compiled-component checks, not normal-app binary runs.

Two existing normal-app regressions separately passed: generic canvas CPU/offscreen DPR1 in 15,224 ms, and ordered Paint/Erase Metal/Cocoa DPR2 in 8,772 ms, each below the original 45,000-ms gate. Existing automatic/manual scope reflow, compact English exposure controls, ordered operation, history/reopen and full owned-source scopes pass their unchanged validators; the English compact controls frame was visually checked. These two runs retain 27 PNG captures. The two component-owned fixtures and four help captures bring this local bundle to 33 actual PNGs. All app/object/source/preference checks remain stable.

Formal local evidence is prepared at `evidence/F4-steady-pen-help-root-local-v1/verified-v1/receipt.json`, binding source232/docs3, the new app build, the two compiled-component checks, two normal-app regressions, private cd43 provenance, accepted CI335 b36 and preserved CI333/CI331 failures. The initial private harness failures remain in their independent archive.

The unchanged Core/input/sampling/rendering evidence is accurately adopted from accepted CI335. New-commit cross-platform acceptance still requires three native69/Core217-zero-skip/Qt20/live8 jobs and both actual45-mode packages with every original timing, numeric, RAW/color, pixel, memory, export and full-scope gate. Existing cross-platform regression coverage is separate from the local controlled keyboard-help path; no physical assistive-technology acceptance is implied.
