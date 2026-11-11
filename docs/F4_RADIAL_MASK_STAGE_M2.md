# F4 Stage M2 — one independent elliptic/radial local exposure mask

**Branch:** `design/lightcraft-fusion-20261008` · **PR #16 stays Draft** · **Exact-head three-platform CI pending**. M1's separate linear mask was accepted at `34b72522` by [CI #38827554163](https://github.com/snailjayxxx/JixelLight/actions/runs/38827554163). M2 is not accepted simply because M1 passed.

## Geometry and pixel contract

M2 adds one editable oval to each photo version, independent of the already accepted linear gradient. Coordinates refer to the entire post-geometry/committed-crop/oriented picture, normalized to [-1,1]. Full-frame, 128-row tile, panned 1:1 ROI and fast previews use the same `ProcessingPlan::pixelMap` world coordinates. With user controls `centerX,centerY, radiusX,radiusY`, the center is `2*center-1`, radii `2*radius`, and `distance=sqrt(((x-cx)/rx)^2 + ((y-cy)/ry)^2)`. The inside weight is `1-smoothstep((distance-(1-feather))/feather)`; `inverted=true` uses `1-inside`. The scene-linear unassociated RGB gain is `exp2(exposureEV*weight)`, applied after camera WB/global exposure **and after the linear gradient**, before the existing vignette and highlight/tone stages. Alpha is unchanged. Neither mask allocates a raster mask or full-frame temporary image. The existing diagnostic Vignette boundary currently includes the contributions of both gradient stages and vignette; StageGraph additionally records each mask's separate explicit input parameters.

Controls: enable, invert, exposure [-3,3] EV, normalized centers [0,1], fractional axis radii [.05,1], feather [.02,1]. Ellipse rotation, direct canvas drag and multiple oval layers remain unimplemented. Zero exposure and disabled states retain original pixels even when inactive control values are stored. All-default settings omit the new `radialMask` optional data. Non-default settings roundtrip via strict `schema=1` that rejects extra, missing, wrong-type, nonfinite, future-version, out-of-range or all-default redundant blocks. Old camera rendering engine/Look identity and project migration contract remain unchanged.

The existing shared `masks` transfer group now carries the single linear and single radial masks while retaining independent `mask.linear.reset` and `mask.radial.reset`. New strict `mask.radial.enable`, `mask.radial.invert`, `mask.radial.set`, `mask.radial.reset` commands are shared by CLI/UI replay. A bilingual QML Develop section exposes all controls; project/history, virtual copies, presets, and JixelLight XMP snapshots retain only opt-in editing intent. No source photo modification, AI model or network calls.

## Mandatory acceptance

- Core: calibrated center/edge/ellipse/invert scene-linear pixel changes, alpha preservation, full/ROI/stats/diagnostic equality, combined linear/radial/vignette, legacy and disabled identity, strict commands and malformed project refusal, Undo/Redo, presets/XMP, independent masks transfer.
- GPU: 4 combinations of centers/radii/feather/invert/combined effects × 2 viewport geometries × 2 input encodings × 4 output spaces; exact alpha/scopes and original **40/65535** CPU–QRhi maximum channel discrepancy.
- Windows/macOS/Linux: 10/10 CTest and no mandatory GPU skip, real Sony RAW/JPEG, Stage 6C research refusal, Qt GUI, Windows/macOS deployed packages, deployed CLI new-image/disable/invert/reset pixel proofs.
- The deployed GUI **60,000 ms** CPU deadline, complete source RAW restoration, Sony look/reference and **10,340,960** full-resolution scope pixels must not be relaxed.

M2 does **not** imply production float-native RAW, automatic AI selections, heal/brush, complex layer masks, radial rotation, or Adobe Camera Raw Develop conversion. Private RAW, Drive paths and file lists remain off public GitHub/CI.
