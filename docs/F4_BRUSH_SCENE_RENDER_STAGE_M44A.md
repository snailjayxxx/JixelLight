# F4 M4.4A — direct scene-linear brush CPU research + forced GPU fallback

Branch: `design/lightcraft-fusion-20261008`. PR #16 remains **Draft**. This is **not accepted** until exact-head Windows/macOS/Linux CI and deployed desktop packages succeed.

## Purpose and isolation

M4.3 safely persists **disabled** future brush masks. M4.4A is a separate opt-in **C++ in-memory research rendering** step. It lets numerical tests construct active brush layers and run them inside the real CPU and QRhi display pipelines, while deliberately retaining the M4.3 strict refusal of enabled masks in Project.db, named presets, XMP and `PhotoController::replaceBrushMaskDraft`. The app UI, CLI and export workflows do not expose enabling a brush. No default RAW source, MakerNote/Sony Creative Look, camera calibration, project schema, LUT engine version, shader code or personal photo is modified.

## The actual CPU path

`ImagePipeline` now takes normalized, per-pixel brush EV from the **existing** `BrushMaskTileRaster` source (no duplicate brush coverage engine), after WB/global exposure and linear/oval masks but before existing scene-linear vignette/highlight recovery/tone. Each tile is at most 128×128 pixels. A mask with no enabled layers takes the original, parallel, byte-identical CPU hot path. Enabled research masks use a serial, bounded tile coverage pass before the same legacy color pixel kernel, with origin-aware tiles for `processRegion`.

The photo canvas's `ProcessingPlan::FrameRect` is honored with or without other spatial effects: zoom/pan ROI samples the *complete* post-geometry/crop mask coordinate system. Source pixel center, short-edge-normalized round-brush radius, pressure, hardness, source-over stroke union and layer EV all follow M4.0–M4.2. Pixel alpha is carried unassociated into/out of the CPU kernel as previously; negative and above-one linear RGB reaches the same downstream tone stages without early RGBA64 clipping. Existing 16-bit integer inputs also retain their established source conversion.

### Research safeguards

- Limit an active research frame to **2,097,152** pixels, to avoid implying full 24/33 MP RAW production performance.
- Keep M4.2's tile candidate-work cap **8,388,608** dab-pixel visits and add a per-frame cap of **100,663,296**; the optional research mask is rejected atomically on exhaustion, invalid/duplicate geometry, cancellation or local brush EV magnitude greater than 8 stops. No truncated/partly edited image may be published.
- Invalid viewport rectangles and offscreen dabs are handled with explicit finite/range checks and bounds clipping *before* float-to-int conversion. `ImagePipeline::diagnoseWithPlan` refuses individual diagnostic stage hashes for active research brushes rather than reporting stale pre-brush stage hashes; `StageGraph::renderKey` includes versioned active brush documents only when enabled. Disabled drafts retain the old cache key and old image math.

## GPU and Scopes behavior

The existing QRhi `GpuEngine` numerical CPU fallback is mandatory for active in-memory research brushes on **every** backend (Direct3D11, Metal, OpenGL), using exact GPU FP32 input followed by the same CPU color pipeline and GPU presentation/1024-bin Scopes. GPU compute does **not** implement brush shader coverage yet; no claims of native GPU paint support. Log the CPU-brush provenance explicitly and reject malformed brush documents rather than silently rendering without them. Native GPU cases must demonstrate original **40/65535** CPU/GPU maximum RGB difference and exact alpha + histogram bins, for full and viewport mapping and multiple output color spaces.

## Mandatory CI

- Core: active in-memory brush changes real output pixels, disabled/empty bit-identical; correct local EV on HDR/negative FP32, tiles/region 1:1, mapped ROI matching M4.1 independent scalar oracle, stable cache provenance and source image unchanged; invalid document, invalid frame, over-8 EV and too-large research image fail closed.
- GPU: Metal/D3D11/OpenGL active brush **must** invoke CPU numeric fallback, compare output to CPU oracle at 40/65535 and 1024-bin Scopes/alpha, capture `cpu-reference-upload` fingerprint, and verify disabled brush returns to ordinary GPU compute. Never use an explicit skip to make mandatory GPU success.
- Original Core, real Sony ARW/JPEG and Stage 6C gates, 1024-bin exact 10,340,960 pixel scopes, Windows/macOS actual deployed binaries and the **unaltered 60,000 ms Windows CPU GUI smoke** remain mandatory.

**Future M4.4B:** remove small-image research-only limits only after efficient large-image performance and GPU parity/explicit fallback are validated for real camera RAW/preview/full export/Scopes; provide accurate stage diagnostics and allow an enabled saved brush with complete project/XMP/history migration before any user-visible painting tool. No Adobe/LightCraft restricted UI assets are used.
