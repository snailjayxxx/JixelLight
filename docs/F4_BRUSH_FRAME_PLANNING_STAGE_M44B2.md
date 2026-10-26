# F4 M4.4B.2 — frame work preflight and representative brush measurements

Branch `design/lightcraft-fusion-20261008`; PR #16 stays Draft. Accepted at `7a27d7f11ab629bafae846cc76e0887f559e42b0`, [CI #40262658009](https://github.com/snailjayxxx/JixelLight/actions/runs/40262658009) Success on all three platforms and deployed desktop packages. Baseline `5d624d3` passed run #39350778492.

## Change and boundaries

Previously the 96 Mi candidate-visit frame budget was checked only after rasterizing each tile. A doomed frame could spend most of its budget computing discarded pixels. `BrushMaskTileRaster::planFrame` now validates the entire requested region and computes tile and frame candidate counts **before source conversion/output allocation** in `processColorFrame`. It stores only tile rectangles and counts, not full-frame masks or RGBA scratch.

Planning and rasterization share the same clipped double-to-integer candidate rectangle helper. Candidate counts deliberately include bounding-box pixels outside each circular dab; disabled/zero-EV layers, zero opacity and zero pressure incur no candidate visits, matching the existing renderer. Raster execution checks exact planned/actual counts per tile and whole frame. Plans are request-local and never reused across mutable documents or cached.

Cancellation is checked at every tile, layer, stroke and dab in planning, and continues per scanline/pixel during raster and color processing. A cancelled/refused request publishes neither a partial plan nor a partial image. Existing 2,097,152-pixel research cap, 8,388,608 candidates per tile, and 100,663,296 per requested frame are unchanged. The source frame itself may be larger when rendering a bounded region, as before.

No enabled project/XMP/preset/Controller/GUI/CLI brush activation; no large camera RAW brush editor, native GPU brush compute, export or persistence gate change. Sony Look/RAW/ICC/GPU/Scopes algorithms remain unchanged.

## Verification and measurement protocol

CoreTests compare preflight versus actual raster counts on uneven partial tiles, portrait/landscape ROI and a tiny offscreen viewport; verify requested pixel coverage, invalid/duplicate/oversized/pre-cancel atomic refusal, disabled identity, separate tile exhaustion and frame exhaustion with individually legal tiles. Existing independent scalar/original RGBA oracle and CPU/GPU brush tests remain mandatory.

PerformanceTests add deterministic synthetic inputs (no private photo data): sparse landscape 512×384, overlapping portrait 384×512 and dense viewport 1024×768; each uses two opposite-EV layers. Three repetitions report median planning, exposure-only and legacy RGBA-oracle milliseconds, pixels, tile counts and candidate visits. Every measured tile also checks exact mask-plane equality and work counts. Timing excludes constructing the synthetic RGBA oracle input, and the first of three samples is not discarded. These are bounded research CPU kernel measurements, not physical GPU benchmarks, interactive latency promises, or a comparison to a previous binary. Each workload also runs the real CPU pipeline and pre-cancel refusal. A separate asynchronous 1024×1024 overlapping request is cancelled while pending and must return no partial image; a later independent request must still succeed.

## Accepted measurements

Three-sample median milliseconds from run #40262658009; plan / mask-only / original RGBA-oracle kernel, excluding synthetic RGBA input construction. First sample retained. All mask planes/work counts matched exactly.

| Platform | Sparse landscape | Overlap portrait | Dense viewport |
|---|---:|---:|---:|
| Linux | 0.0035 / 0.6828 / 2.1252 | 0.0306 / 13.8149 / 15.2525 | 0.0657 / 25.7289 / 32.3194 |
| macOS | 0.0094 / 1.3987 / 4.5078 | 0.0160 / 12.7583 / 18.8790 | 0.0376 / 16.6815 / 23.2932 |
| Windows | 0.0055 / 2.7804 / 8.7724 | 0.0414 / 10.8772 / 20.3057 | 0.0937 / 24.6303 / 69.1416 |

Each platform passed 175 core / 33 mandatory GPU / 31 performance checks, CTest 10/10, original real Sony RAW/JPEG, strict 60,000ms RAW GUI and 40/65535 GPU limits, 1024-bin Scopes and Windows/macOS deployed package checks. Tests used merge commit `92339a055fcaaae13dd329b2fbd3de6ade17aa07`; its tree `31de28aa87875c4a7d63cf40cd5edae3a645aed4` equals the accepted branch HEAD tree. No local Qt SDK was available; acceptance is from CI. These results do not establish production large-RAW brush latency or physical GPU speed.

## Next gate

Use accepted zero-candidate counts to remove empty-tile mask allocation/rasterization, keeping the independent pixel oracle, work limits and cancellation refusal. F0–F6 is not complete.
