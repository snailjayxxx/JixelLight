# F4 M4.4B.5 — whole-operation brush budgets for streamed rendering

Branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Accepted at exact HEAD `4e2fd588dcc9bdee270b0d57c4f3a25ea132a867` by [CI #40277268441](https://github.com/snailjayxxx/JixelLight/actions/runs/40277268441).

## Problem

`processRegion` intentionally supports a bounded region of a larger source. JPEG export, full-resolution histograms and scope plots call it once per 128-row strip. Each strip previously received an independent brush research preflight, so a complete operation could exceed the 2,097,152 logical-pixel or 100,663,296 candidate-visit limits even though each individual strip was legal. The whole-frame raster-export route refused the same request. Enabled saved brushes remain gated, so this inconsistency concerns the opt-in in-memory research path.

A deterministic example needs only a 2048×1024 source and 97 identical radius-0.5 dabs at the center. Their bounding boxes require 101,711,872 candidate visits across the image, exceeding the 96 Mi limit. Every 128-row strip independently remains below that limit. Repeated dabs keep the same maximum-alpha visual contribution while still charging their real candidate work.

## Change

A shared `ImagePipeline::preflightTiledResearchRequest` checks an entire operation before color conversion, streamed color processing or output-file creation. Callers provide the logical post-geometry frame size and their actual stripe height. The logical active-brush frame must remain within the existing research size cap. Candidate accounting follows each strip's real expanded region and its 128×128 tile grid; repeated detail halos count again because they are actual repeated raster work. Tile and complete-operation work caps retain their existing values.

The preflight and `processRegion` share the region-expansion rule. Candidate bounds are prepared only for counting; this check does not copy brush documents, create a cross-request cache or allocate full-frame EV/color scratch. Cancellation or any invalid document, viewport, detail geometry or exceeded budget refuses the operation before rendered strips are published. Ordinary no-brush and valid disabled-brush large-frame rendering retains its existing behavior.

The check covers tiled JPEG export, exact full-image histograms, individual/shared scope plots and whole-frame raster export. Standalone `processRegion` keeps its accepted bounded-ROI behavior; it is not redefined as a full-source operation.

This stage does not enable persistent brush layers, add a drawing UI, enlarge research limits or implement native GPU brush compute. Reusing prepared plans across strips and parallel tile execution remain separate work.

## Verification

Core regressions must demonstrate:

- a legal isolated strip can belong to an over-budget complete operation, which is refused before successful-render counters change;
- detail-halo overlap is charged across strips, including a request whose unique full-frame count fits while repeated strip work does not;
- legal complete operations retain histogram/plot/export behavior and ordinary no-brush large images are not subjected to brush research caps;
- JPEG/raster refusal leaves existing destinations unchanged and does not publish new output; scope refusal does not return partial counts or plots;
- cancellation, malformed inputs and invalid stripe/detail geometry return an explicit failure without partial output.

Local Qt 6.8.3 Release compilation and the four targeted budget/cancellation regressions passed. Complete local CTest passed **10/10 in 73.37 s**, including **182 Core / 35 mandatory Metal GPU / 36 performance** checks, checksum-pinned public Sony RAW/JPEG and zero skips. This is working-tree verification before publication, not three-platform acceptance.

## Exact-HEAD CI acceptance

[CI #40277268441](https://github.com/snailjayxxx/JixelLight/actions/runs/40277268441) completed **Success** on Windows x64, macOS ARM64 and Linux GPU. The run records HEAD `4e2fd588dcc9bdee270b0d57c4f3a25ea132a867`; tested merge `9d6366a533996df140dcc226abee9a90e17ea708` has tree `aa50219acba0686fe6e09d4e11fa2181e4082c41`, identical to that HEAD's tree. Every platform passed **CTest 10/10**, **182 Core / 35 mandatory GPU / 36 performance** checks with zero failed or skipped checks.

All three platforms explicitly passed the four B5 regressions: cancellation while planning without poisoning later requests; complete-frame and repeated-halo accounting; scope/export refusal before publication; and legal tiled scopes/export full-frame pixel fidelity. These checks retain the 96/97-dab boundary, unchanged destinations on refusal, valid independent ROI rendering and original size/work limits.

The retained 16,384-dab GPU viewport passed **sRGB, Display P3 and ProPhoto RGB** on each platform at maximum CPU/GPU RGB error **0/65535**, exact alpha and complete **1024-bin Scopes**, while the original bound stays **40/65535** and active-brush display remains an explicit CPU-reference-upload. Public checksum-pinned Sony RAW/JPEG, ICC, native canvas pointer smoke and existing sensor/refusal/readiness gates passed.

Both actual deployed packages passed all six GPU/CPU/diagnostic/Sony CLI/develop CLI checks with SDK paths removed and **0 QML runtime errors**. Packaged real-RAW CPU GUI elapsed time was **59,799ms Windows / 43,759ms macOS**, under the unchanged **60,000ms** deadline; packaged GPU GUI was **43,302ms / 40,145ms**, respectively. Each full GUI run counted **10,340,960 Scopes pixels**. Exact smoke reports and manifests are in the run's `package-validation-Windows` and `package-validation-macOS` artifacts.

## Retained performance and index evidence

All eight existing brush workload rows passed on this exact code tree. The performance suites completed in **4.174s Linux / 4.834s macOS / 8.721s Windows** under the unchanged **120-second** timeout. Three-sample median elapsed-work times below are milliseconds; they do not establish a new B5 speedup, presentation latency or physical GPU performance.

| Platform | Workload | Plan | Indexed mask | Unindexed mask | Whole CPU frame |
| --- | --- | ---: | ---: | ---: | ---: |
| Linux | Sparse landscape | 0.002795 | 0.64417 | 0.81054 | 27.0868 |
| Linux | Overlap portrait | 0.017713 | 14.4115 | 13.9877 | 40.8372 |
| Linux | Dense viewport | 0.027642 | 27.3201 | 25.5047 | 134.901 |
| Linux | Empty viewport | 0.001352 | 0.268906 | 0.647817 | 25.0972 |
| Linux | Distributed sparse 4K | 0.168166 | 3.07211 | 3.95933 | 28.5011 |
| Linux | Distributed sparse 16K | 0.662092 | 9.28376 | 14.3392 | 35.3976 |
| Linux | Mostly offscreen 4K | 0.051497 | 0.516576 | 3.80566 | 25.5699 |
| Linux | Mostly offscreen 16K | 0.198933 | 0.537845 | 13.6259 | 25.7312 |
| macOS | Sparse landscape | 0.005083 | 1.18046 | 0.775166 | 20.638 |
| macOS | Overlap portrait | 0.013958 | 6.13921 | 5.83584 | 26.5225 |
| macOS | Dense viewport | 0.018 | 12.8761 | 11.8967 | 107.117 |
| macOS | Empty viewport | 0.002333 | 0.479001 | 0.802624 | 18.6839 |
| macOS | Distributed sparse 4K | 0.109208 | 2.0705 | 3.50829 | 37.8624 |
| macOS | Distributed sparse 16K | 0.329292 | 4.66509 | 11.7721 | 24.7778 |
| macOS | Mostly offscreen 4K | 0.030083 | 0.80254 | 4.23267 | 26.5295 |
| macOS | Mostly offscreen 16K | 0.116959 | 0.762625 | 11.5984 | 20.2046 |
| Windows | Sparse landscape | 0.0092 | 2.5415 | 3.6274 | 69.5734 |
| Windows | Overlap portrait | 0.032 | 15.7804 | 15.597 | 89.9786 |
| Windows | Dense viewport | 0.0424 | 34.8372 | 34.7912 | 343.8 |
| Windows | Empty viewport | 0.0047 | 1.6759 | 2.6321 | 67.6727 |
| Windows | Distributed sparse 4K | 0.2762 | 3.9185 | 6.5511 | 72.75 |
| Windows | Distributed sparse 16K | 1.1431 | 6.4895 | 19.1449 | 76.4095 |
| Windows | Mostly offscreen 4K | 0.1214 | 1.9821 | 5.9692 | 69.2749 |
| Windows | Mostly offscreen 16K | 0.5353 | 1.8005 | 18.1025 | 69.6476 |

All platforms retained bit-exact indexed/unindexed/RGBA EV results and exact visit/pruned/rasterized counters. References (sparse / overlap / dense / empty / distributed 4K / distributed 16K / offscreen 4K / offscreen 16K) were **20 / 482 / 432 / 0 / 3,413 / 13,653 / 18 / 18**. Corresponding index capacity bytes were **408 / 4,608 / 4,380 / 288 / 37,152 / 147,744 / 408 / 408** on Linux/macOS and **420 / 3,660 / 4,098 / 288 / 23,040 / 115,416 / 402 / 402** on Windows, reflecting standard-library vector growth. Forced zero-budget plans retained identical pixels/counts with zero index capacity. Indexed masks are not uniformly faster in small or overlap/dense fixtures; all timing rows are retained.

Acceptance applies only to the exact code tree above; later implementation commits require their own unchanged three-platform validation. Enabled persistence refusal, research limits and the original strict GUI/GPU/Scopes/package gates remain. This completes B5's streamed-operation budget consistency gate, not production large-frame brush editing or F0–F6.
