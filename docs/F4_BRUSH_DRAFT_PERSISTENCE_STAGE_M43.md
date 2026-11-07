# F4 Stage M4.3 — versioned brush draft, safe history and project persistence

Development: `design/lightcraft-fusion-20261008`, PR #16 Draft. **Code is not accepted until the exact HEAD passes Windows x64/macOS ARM64/Linux GPU CI and desktop package checks.**

## Product safety contract

M4.1 already defined a bounded `BrushMaskDocument`, and M4.2 already supplied a separately tested CPU tile color oracle. The **production** renderer, QRhi shader, saved-photo export, user-facing brush canvas and brush edit toolbar do **not** yet render those strokes. To prevent users from silently exporting an image with a selected brush that has no effect:

- This stage allows **disabled brush-layer drafts only**. An enabled layer, even with zero EV, is *unsupported* in stored JixelLight editing intent. All attempts to insert an active layer through this stage's C++ draft setter or read it from a project/preset/XMP are refused, not ignored.
- It is a **C++-only draft entrypoint**, `PhotoController::replaceBrushMaskDraft(QJsonObject)`; not exposed as `Q_INVOKABLE`, QML UI, CLI command or a fake tool. A canonical schema-1 document replaces the complete draft as one non-merged undo/redo edit; empty input means clear. Unknown fields, duplicate IDs, future versions, invalid numeric/pressure/dab limits and enabled layers are rejected before changing editor state.
- Preserve the existing `AdjustmentState` JSON: no `brushMasks` field for legacy/default photos. When a valid disabled draft exists, persist its canonical JSON in the existing adjustment snapshot. Use the **existing** `EditHistory`/virtual-copy/project DB and preset/XMP pipelines, not a new database, file family or second command/history framework.
- `AdjustmentTransfer` masks group carries the same disabled draft to existing paste/sync flows. Existing groups not including `masks` must not overwrite brush intent. StageGraph diagnostics report the layer count and `active_rendering=false` so no downstream tool mislabels drafts as visible corrections.
- Project opening checks the optional `brushMasks` field explicitly **before switching the active database writer**; invalid/active/redundant-empty/unknown versions fail closed. Project update and virtual-copy writes reject invalid or enabled drafts prior to database operations, and validate brush-bearing history entries rather than silently dropping them. Preset and XMP exports also reject unsupported active drafts.

## Required Core and end-to-end checks

1. Exact old default adjustment JSON without a `brushMasks` key; disabled draft canonical round-trip and serialized layer order.
2. Scene-neutral RAW/JPEG pixel path remains unchanged by disabled drafts (no fake brush gain); input PNG source hash/bytes remain untouched.
3. Existing EditHistory records draft replacement as one undo entry; Undo/Redo after project reopen restores the complete stroke array; a named virtual copy retains the draft with independent history.
4. Existing masks group transfers disabled drafts without touching exposure; other groups preserve the target's mask draft. Named preset and explicitly requested JixelLight XMP round-trip the same canonical intent.
5. Reject active layers and unknown/corrupt future versions; failed Controller edits do not mutate state or history. A forged project's invalid/active `brushMasks` is refused without replacing the current healthy writer or modifying any bytes in the malicious source project.
6. All prior mandatory Linux/Windows/macOS Core and QRhi GPU tests, real Sony ARW/JPEG/Stage 6C, 1024-bin exact full-source Scopes, native Canvas pointer smoke, and Windows/macOS deployed package smoke **under the original 60,000ms RAW CPU deadline** remain required. The CPU/GPU RGB tolerance stays **40/65535**, without skips of mandatory GPU.

## Compatibility and future stages

Do not declare brush painting, GPU rendering, local effects, AI selection, actual exposure/export integration or final F4/F0–F6 completion at M4.3. Older released builds may not preserve unknown new optional fields; keep M4.3 in this Draft development branch until the integrated renderer, migration safeguards and application UI are validated.

M4.4 must connect the *same* documented brush mask semantics to the real scene-linear pipeline with explicit GPU parity/fallback, byte-exact Alpha, full-frame vs ROI/tile/Scopes/exports, undo/project snapshots and original-photo SHA. Then M4.5 can implement photo-canvas pointer painting and the user-visible brush toolbar after all acceptance gates succeed. Never add a duplicate mask/raster/history system.
