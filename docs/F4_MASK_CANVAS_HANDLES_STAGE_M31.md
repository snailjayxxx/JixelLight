# F4 Stage M3.1: interact with existing local masks on the canvas

**Development:** `design/lightcraft-fusion-20261008` / PR #16 Draft. **Accepted at exact HEAD `a41ac604344b74265b496ae0beb221050bf580c0`**, [CI #38985204485](https://github.com/snailjayxxx/JixelLight/actions/runs/38985204485): Windows x64, macOS ARM64, Linux GPU all Success, with the independent real Qt mouse smoke, Core 167 / GPU 32 / performance 27 tests, CTest 10/10, real Sony RAW/JPEG, Stage 6C, shipped desktop packages and original 60,000 ms RAW CPU smoke.

The photo preview now offers explicit **Linear handles** and **Oval handles** modes. It draws manually adjustable geometric guides without any additional full-frame masks/pixel passes. User interaction is opt-in; entering the tool does not enable a previously disabled effect or change photo intent until the user drags a handle. Escape exits; Crop mode and library photo switching exit automatically. Existing zoom pan is suspended while selecting a handle and resumes on exit.

All guides/pointer coordinates use the immutable normalized **complete post-geometry/crop/orientation photo**, and the actual prepared `previewFrameRect` for Fit / viewport zoom / pan. The C++ `MaskCanvasGeometry` inversion refuses nonfinite or zero-size planes; normalized pointer coordinates remain *unclamped* across a viewport edge so users can reach valid limits in zoom mode. Stored parameters still pass their existing strict command range checks.

Controls implemented:

- **Linear:** drag the center handle to move gradient center along its normal; drag the blue handle to set angle.
- **Oval:** drag the center handle to move center X/Y atomically (one Undo entry per pointer gesture); drag axis handles to change radii; drag the blue handle to rotate the oval. Effects can be disabled/adjusted using the existing right-side panel; reset, history, presets and XMP do not change.
- The mouse gesture snapshots its initial values rather than feeding its own mutations back into incremental deltas. `PhotoController::finishInteraction()` closes the history merge key on release/cancellation.
- The owning original photograph is never written.

The new independent CLI flag `--mask-canvas-smoke-report` uses a generated synthetic PNG to send **native QMouseEvent pointer drags** to real QML radial and linear handles, test single-gesture Undo/Redo and zoom ROI mapping, then check the original source SHA. `ValidateMaskCanvas.py` enforces the report and rejects QML runtime errors on each OS. It is deliberately separate from the original **60,000 ms** real RAW/Sony/GPU/1024-bin Scopes desktop smoke. Mandatory Windows/macOS/Linux and deployed package CI gates remain unchanged.

Still out of scope at M3.1: brush, masks stacking, drawn raster masks, edge/subject recognition, per-mask HSL/WB, canvas feather dragging and full F4 completion. M3.2 adds the first canvas feather handles as an independently tested next increment.
