# CPU/GPU numerical correction — 2026-09-07

## Investigation evidence, not final package acceptance

Base `9edf69f289fa7fc5ff3d85e46b2ac81d1819c9af`; [read-only experiment](https://github.com/snailjayxxx/JixelLight/actions/runs/34086153404). Seven candidates were independently built/tested in temporary runner worktrees and restored afterward. No candidate source was automatically pushed. Experiment completion is not equivalent to all candidates passing.

| Candidate | Metal passed / failed | D3D11/WARP passed / failed |
|---|---:|---:|
| Shared-matrix baseline | 13 / 3 | 12 / 4 |
| Ordered CPU arithmetic | 13 / 3 | 12 / 4 |
| Ordered + Cartesian pure chroma | 13 / 3 | 13 / 3 |
| Ordered + relative gamut width 0.001 | 16 / 0 | 12 / 4 |
| Both + relative width 0.001 | 16 / 0 | 14 / 2 |
| Ordered + relative width 0.005 | 16 / 0 | 14 / 2 |
| **Both + relative width 0.005 (selected)** | **16 / 0** | **16 / 0** |

Selected maximum channel differences: **9/65535 on Metal, 10/65535 on D3D11/WARP**, versus the unchanged limit **40/65535**. No skips. The expanded high-exposure Windows baseline reached 582/65535. These are fixture-specific correctness results, not universal error guarantees or performance measurements.

Devices were Apple Paravirtual device and Microsoft Basic Render Driver (WARP), not physical-machine acceptance. [Mac experiment artifact](https://github.com/snailjayxxx/JixelLight/actions/runs/34086153404/artifacts/10005406374); [Windows experiment artifact](https://github.com/snailjayxxx/JixelLight/actions/runs/34086153404/artifacts/10005493129).

## Correction and compatibility

CPU and GPU now share the exact precomposed input and working-space rows; the std140 payload is **496 bytes**. Pure saturation/vibrance edits scale Oklab a/b directly instead of unnecessarily reconstructing it through atan2/sin/cos. Only the CPU image kernel uses ordered floating-point options (`/fp:strict` or `-ffp-contract=off`); the shader retains precise arithmetic.

The gamut-inset transition uses `width = 0.005 * max(1, luminance)` and a smooth interpolation of inset 1 to 0.995. The old fixed narrow interval amplified small FP32 differences at high exposure. Both the chroma and boundary fixes are needed for the tested cross-backend result.

**The boundary change is an intentional rendering-policy correction, not a bit-identical optimization.** Some near-out-of-gamut pixels can differ visibly from older alpha.7/alpha.6 output. Positive in-gamut RGB is not compressed by this step. Historical `legacyProcess` retains alpha.6's fixed inset. The separate independent `correctedReferenceProcess` implements only the new boundary policy and continues to use original unoptimized calculations. Neither reference calls the optimized engine. Serial/parallel equality and reference bounds remain mandatory tests.

Engine/cache identity is `jixellight-linear-v2-perf3`, invalidating old previews. Original RAW files and adjustment JSON are unchanged.

## Expanded and deployment tests

Retained historical parameter cases plus two deterministic 1024x1024 dense fixtures, fixed neighborhoods of failed pixels, and eight input/output combinations use the same maximum error bound on every backend. Non-finite GPU values are checked before integer conversion. Histogram bins and clipping percentages must exactly match the actual GPU pixels. Direction and source-upload reuse remain tests.

Normal CI must still pass all four CTest suites and native GUI interaction on the committed source. Metal and Direct3D tests are required; skips are not passes. After deployment, `ValidatePackage.py` launches the actual packaged executable from a clean directory with development Qt lookup paths removed, once on native GPU and once on CPU fallback, checks pixels/scopes/screenshots, and writes commit/backend/hash manifests before package upload. Windows GUI processes are explicitly waited for. The investigation workflow/script are removed after selection but remain reproducible at the base commit.

No stable release, code signing, notarization, physical-device coverage or whole-program speedup is asserted by this document. Consult the final normal build run and package manifests for delivery status.
