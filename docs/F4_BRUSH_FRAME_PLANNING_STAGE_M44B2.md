# F4 M4.4B.2 — frame work preflight and representative brush measurements

Branch `design/lightcraft-fusion-20261008`; PR #16 stays Draft. Candidate pending its own exact-HEAD CI. Baseline `5d624d3` passed run #39350778492.

## Change and boundaries

Previously the 96 Mi candidate-visit frame budget was checked only after rasterizing each tile. A doomed frame could spend most of its budget computing discarded pixels. `BrushMaskTileRaster::planFrame` now validates the entire requested region and computes tile and frame candidate counts **before source conversion/output allocation** in `processColorFrame`. It stores only tile rectangles and counts, not full-frame masks or RGBA scratch.

Planning and rasterization share the same clipped double-to-integer candidate rectangle helper. Candidate counts deliberately include bounding-box pixels outside each circular dab; disabled/zero-EV layers, zero opacity and zero pressure incur no candidate visits, matching the existing renderer. Raster execution checks exact planned/actual counts per tile and whole frame. Plans are request-local and never reused across mutable documents or cached.

Cancellation is checked at every tile, layer, stroke and dab in planning, and continues per scanline/pixel during raster and color processing. A cancelled/refused request publishes neither a partial plan nor a partial image. Existing 2,097,152-pixel research cap, 8,388,608 candidates per tile, and 100,663,296 per requested frame are unchanged. The source frame itself may be larger when rendering a bounded region, as before.

No enabled project/XMP/preset/Controller/GUI/CLI brush activation; no large camera RAW brush editor, native GPU brush compute, export or persistence gate change. Sony Look/RAW/ICC/GPU/Scopes algorithms remain unchanged.

## Verification and measurement protocol

CoreTests compare preflight versus actual raster counts on uneven partial tiles, portrait/landscape ROI and a tiny offscreen viewport; verify requested pixel coverage, invalid/duplicate/oversized/pre-cancel atomic refusal, disabled identity, separate tile exhaustion and frame exhaustion with individually legal tiles. Existing independent scalar/original RGBA oracle and CPU/GPU brush tests remain mandatory.

PerformanceTests add deterministic synthetic inputs (no private photo data): sparse landscape 512×384, overlapping portrait 384×512 and dense viewport 1024×768; each uses two opposite-EV layers. Three repetitions report median planning, exposure-only and legacy RGBA-oracle milliseconds, pixels, tile counts and candidate visits. Every measured tile also checks exact mask-plane equality and work counts. Timing excludes constructing the synthetic RGBA oracle input, and the first of three samples is not discarded. These are bounded research CPU kernel measurements, not physical GPU benchmarks, interactive latency promises, or a comparison to a previous binary. Each workload also runs the real CPU pipeline and pre-cancel refusal. A separate asynchronous 1024×1024 overlapping request is cancelled while pending and must return no partial image; a later independent request must still succeed.

Local environment has no Qt 6.8.3 SDK/CMake. `git diff --check` passed; full compilation, timing values and functional acceptance remain pending CI. The unchanged workflow must pass Windows x64/macOS ARM64/Linux GPU, real public Sony RAW/JPEG, 1024-bin full Scopes, 40/65535 GPU tolerance, original Windows CPU RAW GUI **60,000ms**, and actual Windows/macOS deployed packages. Any failure must be inspected and fixed; do not skip, retry quota failures, or widen limits.

## Next gate

Collect exact-HEAD three-platform timing/acceptance evidence and inspect cancellation under representative load before considering further indexing or active large-frame work. F0–F6 is not complete.
