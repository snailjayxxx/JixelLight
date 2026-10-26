# F4 M4.6C — exact-layer stroke removal and display readiness

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Candidate based on `4ff05b9` / M4.6B. Initial B CI failed its macOS native painted-window capture; this follow-up adds layer editing and actual display/readiness checks. Final local regression passed below; own committed cross-platform/package acceptance remains pending.

## User behavior

**Remove last stroke** removes the most recently stored stroke from the selected brush layer, including after reopening a project. It creates one ordinary Undo step without walking backwards through unrelated adjustments. Removing its only stroke removes that exact layer, preserving other layers and global edits; Undo restores its stable identity and intent. A removed selection returns to the explicit New layer choice, so another click cannot silently remove a different layer. Disabled layers can be trimmed or deleted without enabling their effect.

The existing **Delete layer** action and the new action share the strict command registry with the offline CLI:

| Command | Required fields | Behavior |
| --- | --- | --- |
| `mask.brush.remove-last-stroke` | `layer` string | Remove one stored final stroke; prune its layer when empty. |
| `mask.brush.remove` | `layer` string | Remove only that existing stable-ID layer. |

Both reject missing/wrong-type/empty/unknown IDs, unknown fields and malformed typed documents before mutation. Command arrays retain whole-batch staging. The project schema remains1; the reduced document is readable by earlier active-brush builds. These actions reduce stored intent, perform no pixel work and do not expand admission or processing budgets.

For example, `[{"command":"mask.brush.remove-last-stroke","layer":"brush_existing_id"}]` may be used as an override while exporting a saved catalog version. The caller must use its exact existing ID; this is not a wildcard or an instruction to operate on the first layer.

## Verification scope

The new120-second `brush-layer` shard brings CTest to42. Actual public-controller gestures create two distinct layers and multiple strokes; removal is checked against previous immutable intent, one Undo, preserved other layers/global exposure, saved-project reopen and restored history. The shared registry rejects malformed edits and an invalid command after a valid removal rolls back the whole batch. An actual external CLI applies both removal commands to a2,659,021-pixel owned image; PNG16 pixels and ICC are compared to an independent legal-strip reference. A later invalid command refuses without publishing a destination, and source bytes remain unchanged. The older real Sony/four-format/large-image/precision tests remain required.

The existing native pointer smoke retains all prior steps and adds actual ComboBox/key selection, appending to that selected saved layer, a multi-move EV drag with one Undo/Redo, checkbox disable/re-enable, last-stroke removal, automatic empty-layer removal/Undo and layer deletion/Undo while preserving the other layer. It additionally checks the English layer controls are visible at1180×800 and saves a fifth screenshot. No layer selection or these operations are injected through direct editor-property changes. Public-controller project operations and language selection remain separate from OS file-picker automation.

CPU and required-GPU smoke keep the same45s internal/65s wrapper deadlines. The actual SDK-stripped Windows/macOS packages still require all **nine modes**, including both expanded brush modes; the original RAW60,000ms, GPU40/65535 and120-second performance/large CTest gates are unchanged. All old four screenshots/flags and complete2,659,021-pixel statistics remain required.

Admitted source32Mi pixels/shared3GiB and the old2Mi research API/8Mi per tile/96Mi operation work limits remain. Mouse pressure1, CPU-reference processing applied on release, and existing GPU presentation remain accurately labeled. This does not add an eraser, pressure-device support, live color during a drag, native GPU brush computation or native FP32 RAW decoding; F4/F0–F6 remain incomplete.

## Results and retained findings

The first layer build and focused `brush-layer` shard passed3.72s. A first full42 passed440.17s and old RAW GUI passed CPU14,138/Metal14,770ms (23 screenshots/10,340,960 scopes/all UI/selected-export flags/QML0/60,000ms); mask914/recovery459ms passed. These precede the final readiness fix and are retained, not represented as final-source acceptance.

Visual inspection of the first native Metal report found an empty displayed waveform despite completed model/control flags. The smoke now waits for the actual Qt Quick plot image to be Ready at the current URL, rechecks state after capture and requires64 sampled green waveform pixels in the actual window. The native unpainted photo must also be visibly loaded before measuring the unchanged minimum3/255 painted-window delta. A pending async frame is retried inside the same45s deadline; it is not accepted as a successful display. Failure screenshots are retained. Standalone GUI logs are now included in CI artifacts.

A second visual check exposed a real production notification gap: a fast GPU frame could finish while interaction was still active, then the refinement timer cleared `m_interacting` without changing the busy flag. QML could retain a disabled canvas and a “processing” hint even though the controller getter was ready. The native Metal RED run (`brush-layer-gpu-ready-red.json`,5,204ms) requires actual enabled pointer input and a hint matching the current controller status; it failed with the stale hint while all model/visible-waveform checks passed. `finishInteraction` now notifies when ending interaction even if GPU rendering is already idle; language changes also notify the localized brush message. The CPU canvas additionally waits for its displayed image to be Ready at the current URL before accepting paint.

Final CPU/actual M2 Metal native smoke passed5,093/5,020ms, all20 required boolean flags, five screenshots each, full2,659,021 statistics,902 sampled waveform ink pixels and zero QML errors. Actual displayed color deltas remain62/63. The restored English hint and enabled canvas were visually checked. Reports/screenshots: physical build `brush-layer-{cpu,gpu}-verified.json*`, parent wrapper logs `brush-layer-{cpu,gpu}-verified.log`. All initial, blank-frame and RED reports/screenshots remain in place. Native reports identify precommit base4ff05b9, not committed-HEAD acceptance.

Final fixed source **42/42 CTest passed448.40s**, actual M2 Metal/public Sony/DNG, zero failures/skips:212 Core/36 mandatory GPU/39 performance/21 Sony Look/9 DB/9 missing-source/17 admission plus all prior large/routing/persistence/export/statistics shards and the new layer shard. Final XML/log: `local-brush-layer-green-full-tests.xml` / parent `brush-layer-green-full-tests.log`. Original RAW GUI passed CPU13,854/Metal14,836ms, each23 screenshots/10,340,960 full scope pixels/all UI and selected-export flags/QML0 within60,000ms; mask913/recovery476ms passed (OS picker unautomated). GUI reports: `local-brush-layer-green-gui/`, parent `brush-layer-green-gui-validation.log`. Complete local summaries: `/tmp/jixellight-brush-layer-local-summary.json` and physical build parent `brush-layer-local-summary.json`. No compiled source changed after these checks. No old120s/60,000ms/45s or numerical precision threshold was relaxed. Own committed CI must still pass all42 on three platforms and both actual nine-mode SDK-stripped packages before acceptance.

Public real Sony32,947,264-pixel gesture/statistics and four-format exports remain required through the actual controller/CLI suites. The native pointer scenario here uses its explicitly owned2,659,021-pixel fixture; a native real-Sony large-image pointer scenario remains a next production acceptance slice. This stage is not a claim of complete large-image UI or F0–F6 completion.
