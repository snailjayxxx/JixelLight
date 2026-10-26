# F4 M4.4B.3 — prune provably empty brush tiles

Branch `design/lightcraft-fusion-20261008`; PR #16 stays Draft. Candidate pending its own exact-HEAD three-platform CI.

## Implementation

M4.4B.2 established exact candidate counts before image staging. Sparse brushes still allocated double stop/layer/stroke planes plus float EV output for every tile, including tiles with no candidate work. The real CPU `processColorFrame` now rasterizes only request-local planned tiles whose count is nonzero. Zero-candidate tiles supply scalar zero EV to the same color kernel, without coverage or mask output allocations. All pixels still receive global WB/exposure, other masks, vignette/tone, color transforms and alpha processing.

A completely zero-candidate frame reuses the historical serial/parallel row renderer. This happens **after** document/viewport/research size/work validation and FP32 source validation, never as an early return of unprocessed source pixels. Mixed-frame nonempty tiles retain exact planned/actual visit-count checks. New cumulative `brush_research_rasterized_tiles` and `brush_research_pruned_tiles` counters distinguish actual mask work from total planned tiles; existing total tile and candidate-visit counters retain their meanings. No stale plans/cache reuse or new mask algorithms.

These are research-only paths. The 2,097,152 requested-pixel, 8,388,608 per-tile and 100,663,296 per-frame candidate limits and enabled project/XMP/preset/Controller/GUI/CLI refusal remain. Even a zero-EV active research brush on an oversized frame is refused. QRhi continues reporting active brush processing as explicit CPU reference upload, including empty viewports. Sony Creative Look/RAW/ICC/1024-bin Scopes behavior is preserved.

## Verification

- Core: single active tile among nine must increment rasterized by one and pruned by eight; signed/HDR RGB compared with an independent per-pixel scalar EV oracle (<=2 codes only for oracle FP32 reassociation), exact alpha and empty-tile baseline pixels. Uneven regions, wholly empty panned viewport, serial/parallel identity, zero-EV/opacity/pressure, immutable source, duplicate document/pre-cancel/oversized-zero refusal are checked. Existing 40/65535 GPU tolerance is unchanged.
- Mandatory GPU: sparse mixed frame and completely empty viewport retain CPU/GPU pixel + 1024-bin scopes parity, alpha and explicit CPU fallback provenance.
- Performance: retain earlier unpruned kernel/oracle measurements; also report three-sample median **whole CPU frame** time, check deterministic image output and exact pruned/rasterized counters in all workloads, add a fourth empty-viewport row. No comparison to an older binary or portable speedup claim.

Local environment has no Qt SDK; `git diff --check` passed. Build, numeric tests and performance values for this candidate remain pending its exact-HEAD Windows/macOS/Linux CI, including real public Sony RAW/JPEG, strict original Windows RAW CPU GUI 60,000ms, GPU 40/65535, full 1024-bin Scopes and actual Windows/macOS deployed package verification. Do not skip failures or relax limits.

## Next gate

Record exact-HEAD acceptance and whole-frame timing evidence. Further dab/stroke indexing and enabled large-frame/render/export/editor integration remain separate future work; F0–F6 is not complete.
