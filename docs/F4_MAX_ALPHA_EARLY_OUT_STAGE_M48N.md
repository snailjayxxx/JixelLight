# F4 M4.8N — maximum-alpha early-out for dense brush coverage

## Behavior

The CPU coverage kernel skips a pixel’s distance/feather calculation only when the current within-stroke maximum is already at least opacity times the current pressure. For valid finite inputs, the unchanged feather cannot exceed one. The existing maximum therefore cannot increase at that pixel. Every other pixel keeps the original radial, feather, alpha and maximum expressions. Ordered Paint/Erase and layer exposure composition stay unchanged.

All dabs, coordinates and pressure values remain stored in the same order. Candidate rectangles, charged work, per-row cancellation, tile index/fallback, geometry mapping, source limits, the 4,096/16,384 dab boundaries and shared 3 GiB are unchanged. This CPU optimization does not create native GPU brush computation, alter input stabilization, or accept previously refused full raw jitter.

## Independent private evidence

The reviewed candidate header matches private receipt `evidence/F4-max-coverage-earlyout-private-v1/verified-v1/receipt.json`, SHA-256 `5e157b29c41298ca8d8c5edca1a6adf8a2bf1fd5dc7a1885059b977dcc2e435b`: 55 files, 32,966 checks, 26,214 per-dab double-alpha comparisons, 264 public cases, 288 multi-tile executions and two deliberately broken candidates rejected. Coverage doubles, float exposure, signed HDR RGBA and work counts match the old kernel bit for bit. Invalid/capacity/work/cancel/forged-plan refusals retain atomic publication and canonical intent. Original harness compile and over-budget fixture errors remain archived.

On the local Apple M2 / Qt6.8.3 / Apple clang Release toolchain, one correctness warmup and seven alternating samples (three calls per sample) measured dense-hard overlap medians 8.9248 to 2.2519 ms, dense-soft pressure jitter 8.7355 to 3.8496 ms, and sparse-small-dab 0.00447 to 0.00457 ms. These synthetic CPU coverage calls include validation/allocation and exclude decode, UI, color processing, RAW, actual export and GPU. They do not establish whole-app throughput.

The existing Core exposure-oracle method gains an analytic concentric Paint/Erase/Paint fixture with 4,096 paint dabs, 128 eraser dabs and one repaint dab. It checks all pixel rings, exactly 202,081 charged visits, indexed/scanned/fallback plans, unchanged JSON and refusal at 4,097 dabs. The fixture passed 46 standalone checks against both old and candidate kernels. No new Qt method or CTest registration is added; original core217, pressure20/live8 and 69 registry names remain.

## Formal verification

The five formal Root app/brush/core/export-verifier targets built serially in 144,660 ms after configuration. All 232 source files stayed fixed; exactly the raster header and Core regression change from accepted 7c213df, with the other 230 byte-identical. Seven registered brush cases pass exact original9+recovery5+stabilization4 Qt20 and live8. Four existing Core methods, including the strengthened exposure oracle, passed with six Qt passes.

The added complete Core invocation initially omitted RAW/Sony environment, producing 213 passes and four skips. The strict driver rejected that report, which remains unchanged. Only those four methods were then run with the existing real Leica DNG and all six checksum-pinned Sony originals; six Qt passes including setup/cleanup resolved every skipped method. The compiled binary’s function list confirms 217 unique checks covered across the two reports. Their combined 19,551 ms retains the original Core 120,000 ms limit. The 213 completed checks were adopted without rerunning them. This is not a claim of one 217-pass/zero-skip invocation. Twelve new ledger controls (one positive, eleven negative) reject missing/duplicate methods, new skips, false totals/claims and budget overrun.

Eight selected-rule canvases passed in 1,150–2,214 ms and eight existing recovery canvases in 1,325–2,333 ms, preserving 144 CRC/hash-checked PNGs under the original 60,000 ms gate. CPU used offscreen 1× and Metal Cocoa 2×. These are controlled normal-app checks, not physical tablet input.

The original eight CPU/Metal × 1×/2× × opaque/transparent pressure cases passed in 5,449–6,066 ms under 45,000 ms with 176 PNGs and exact owned export/ICC. Public Sony pressure CPU/Metal GUI took 9,847/10,115 ms; export plus independent reference took 34,664.188/32,913.943 ms. Both retain all 32,947,264 source/histogram/waveform pixels, precise PNG16/ICC, shared 3 GiB, and the original 60,000/120,000 ms gates. Saved native Sony reports remain external-verification-pending/false; their wrappers and independent references passed. No stabilization-specific Sony or physical-tablet acceptance is implied.

Formal local evidence target is `evidence/F4-max-alpha-earlyout-root-local-v1/verified-v1/receipt.json`: 232 source pins, three documents and 342 actual PNGs (selected/recovery144 + pressure176 + Sony pressure controls22), with private kernel provenance, ten integration-guard controls and the original Core fixture-environment refusal retained. The unchanged 18 validator controls are adopted by sealed M48L log/source hashes, without rerunning them. The previous M48M focused/pointer checks remain historical CI334 evidence and are not claimed as freshly run on this source.

The previous commit 7c213df is completely accepted by CI334 receipt `7d0590c7acafdd91fc9de98c5216af7d6ebd25df0718c024ae1e024ee1abb0f7`. Its original raw artifacts and CI333/CI331 failures remain preserved. This new commit still requires all three native 69-test jobs with original Core217 zero-failure/zero-skip reporting and exact Qt20/live8, both actual 45-mode packages and every original timing/numeric/color/RAW/export/32 Mi-pixel/3 GiB/full-scope gate. Complete unfiltered unique-position jitter, physical pen, stabilization-specific Sony, native GPU brush computation and native FP32 RAW remain unverified.


## Cross-platform acceptance

CI335 run41960105878 completely accepts published `9119596`, tree `5339d68a96573e4b11944c3061b76a55eab1a083`. All three actual checkouts are `acbbc7eaced910daae449bac2e57a3e56ee596d5` with the identical tree. Each original native job passes all 69 registrations and Core217 in one zero-failure/zero-skip report, exact pressure Qt20 and live8. Both actual SDK-stripped packages pass all 45 modes. The five native/package contexts retain 40 stabilization cases/320 PNGs and 40 recovery cases/400 PNGs, all original validator-method, source, byte, behavior, backend, timing, numeric, real-RAW, color, export, pixel, memory and full-scope gates.

Three original FULL logs and five original ZIPs are independently sealed at `evidence/ci-41960105878/terminal-accepted-v1/receipt.json`, SHA-256 `b36ccc6b549d54f8b9721b7920dc6cfcfce1d783d2f0f89a004a2f1102417b12` (563 files). The local split Core coverage remains a separate observation with its original skips preserved. CI333 and CI331 failures and all original audit refusals remain retained; this acceptance does not establish full raw jitter, physical-tablet, stabilization-specific Sony or native GPU-brush support.
