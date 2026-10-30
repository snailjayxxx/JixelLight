# F4 M4.4B.3 — prune provably empty brush tiles

Branch `design/lightcraft-fusion-20261008`; PR #16 stays Draft. Accepted at exact HEAD `a2520654e2914184b6d77635b75562f0a7f81f8e` by [CI #40267148010](https://github.com/snailjayxxx/JixelLight/actions/runs/40267148010).

## Implementation

M4.4B.2 established exact candidate counts before image staging. Sparse brushes still allocated double stop/layer/stroke planes plus float EV output for every tile, including tiles with no candidate work. The real CPU `processColorFrame` now rasterizes only request-local planned tiles whose count is nonzero. Zero-candidate tiles supply scalar zero EV to the same color kernel, without coverage or mask output allocations. All pixels still receive global WB/exposure, other masks, vignette/tone, color transforms and alpha processing.

A completely zero-candidate frame reuses the historical serial/parallel row renderer. This happens **after** document/viewport/research size/work validation and FP32 source validation, never as an early return of unprocessed source pixels. Mixed-frame nonempty tiles retain exact planned/actual visit-count checks. New cumulative `brush_research_rasterized_tiles` and `brush_research_pruned_tiles` counters distinguish actual mask work from total planned tiles; existing total tile and candidate-visit counters retain their meanings. No stale plans/cache reuse or new mask algorithms.

These are research-only paths. The 2,097,152 requested-pixel, 8,388,608 per-tile and 100,663,296 per-frame candidate limits and enabled project/XMP/preset/Controller/GUI/CLI refusal remain. Even a zero-EV active research brush on an oversized frame is refused. QRhi continues reporting active brush processing as explicit CPU reference upload, including empty viewports. Sony Creative Look/RAW/ICC/1024-bin Scopes behavior is preserved.

## Verification

- Core: single active tile among nine must increment rasterized by one and pruned by eight; signed/HDR RGB compared with an independent per-pixel scalar EV oracle (<=2 codes only for oracle FP32 reassociation), exact alpha and empty-tile baseline pixels. Uneven regions, wholly empty panned viewport, serial/parallel identity, zero-EV/opacity/pressure, immutable source, duplicate document/pre-cancel/oversized-zero refusal are checked. Existing 40/65535 GPU tolerance is unchanged.
- Mandatory GPU: sparse mixed frame and completely empty viewport retain CPU/GPU pixel + 1024-bin scopes parity, alpha and explicit CPU fallback provenance.
- Performance: retain earlier unpruned kernel/oracle measurements; also report three-sample median **whole CPU frame** time, check deterministic image output and exact pruned/rasterized counters in all workloads, add a fourth empty-viewport row. No comparison to an older binary or portable speedup claim.

## Exact-HEAD acceptance evidence

[CI #40267148010](https://github.com/snailjayxxx/JixelLight/actions/runs/40267148010) completed **Success** on Windows x64, macOS ARM64 and Linux GPU. The run records head `a2520654e2914184b6d77635b75562f0a7f81f8e`; checkout tested merge commit `17e3f4eccc42063bdeb5aeb94ac7401f37e58ca8`, whose tree `88f7634cf55f767cbe1ff26ee11fbaa7aaeeb52b` is identical to that branch HEAD's tree. Each platform passed CTest **10/10**, **176 Core / 34 mandatory GPU / 32 performance checks**, with zero failed or skipped checks.

The sparse/empty-tile GPU regression retained explicit CPU-reference-upload provenance and full 1024-bin Scopes parity; both new cases reported maximum RGB delta **0/65535** on every platform under the unchanged **40/65535** limit. Real checksum-pinned Sony RAW/JPEG, the native canvas pointer smoke, display ICC and existing sensor/readiness/refusal gates passed.

Three-sample median **whole CPU frame** timings from the same run (milliseconds):

| Platform | Sparse landscape | Overlap portrait | Dense viewport | Empty viewport |
| --- | ---: | ---: | ---: | ---: |
| Linux GPU job / CPU frame | 23.8475 | 34.5555 | 119.058 | 22.7446 |
| macOS ARM64 | 25.7785 | 34.2982 | 140.616 | 24.338 |
| Windows x64 | 50.1565 | 62.5191 | 243.596 | 49.4312 |

Sparse/overlap/empty fixtures contain 196,608 pixels and 12 planned tiles; dense contains 786,432 pixels and 48 tiles. All platforms verified exact pruned/rasterized counts: sparse **8/4**, overlap **0/12**, dense **0/48**, empty **12/0**. These are synthetic CPU elapsed-work measurements, not presentation latency, physical GPU performance or a comparison against an older binary. The earlier unpruned mask-kernel/oracle rows remain in the performance report.

Both **actual deployed packages** passed GPU, CPU, diagnostic, Sony native CLI and develop CLI checks with SDK paths removed. Packaged real-RAW CPU GUI elapsed time was **49,235ms Windows / 39,533ms macOS**, below the unchanged **60,000ms** deadline; packaged GPU GUI was **32,401ms / 37,706ms** respectively. Each full GUI run counted **10,340,960 Scopes pixels**, and both package manifests report **0 QML runtime errors**. Evidence is in the run's `package-validation-Windows` and `package-validation-macOS` artifacts. Local `git diff --check` passed; compilation/numeric acceptance above comes from this exact-code-tree CI, not a local Qt build.

## Next gate

M4.4B.3 is accepted only for the code tree above; later implementation commits require their own unchanged three-platform gates. Further dab/stroke indexing and enabled large-frame/render/export/editor integration remain separate future work; F0–F6 is not complete. Research limits and enabled persistence refusal remain in force.
