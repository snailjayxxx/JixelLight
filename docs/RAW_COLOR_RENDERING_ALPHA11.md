# RAW Color Rendering — alpha.11

## Purpose

alpha.11 repairs the large color/brightness discontinuity that could occur when a RAW file changed from its embedded camera JPEG placeholder to JixelLight's full RAW render. The goal is not to force the RAW to equal the camera JPEG. The camera JPEG is used as a regression/reference signal while RAW processing remains explicit and inspectable.

## Corrected RAW decode policy

LibRaw is configured as follows:

- camera white balance enabled
- camera color matrix enabled
- `no_auto_bright = 1`
- `adjust_maximum_thr = 0.75`
- `highlight = 1` (unclip; no LibRaw highlight blend)
- `output_color = 4` (ProPhoto RGB)
- `gamm = 1, 1` (linear output)
- 16-bit output

JixelLight therefore keeps clipped-channel reconstruction out of LibRaw and owns visible highlight/tone rendering downstream. The current JixelLight `highlightRecovery` implementation scales RGB together and the display shoulder is a tonal transform; neither is treated as a reason to silently change the RAW decoder policy.

A previous alpha.11 probe compared LibRaw highlight modes 1 and 2 on the pinned Sony set. Before any fitted look, held-out RAW-to-camera-JPEG RMSE changed only from approximately `0.0507661` (mode 1) to `0.0507528` (mode 2), about a 0.026% relative difference. That is not sufficient evidence to change the production decoder to mode 2. Decoder code, `RawMetadata`, cache metadata and diagnostics therefore remain consistently on mode 1.

## Exposure semantics

Three concepts must remain separate:

1. **Camera/model baseline exposure** — metadata-derived zero-point offset. DNG `BaselineExposure` is currently supported. Proprietary RAW without a supported equivalent defaults to 0 EV; JixelLight does not invent a camera-specific offset.
2. **Neutral base tone** — scene-to-display tone placement and highlight roll-off. This is rendering, not the Exposure slider.
3. **User Exposure** — the value visible to the user. A new RAW still starts at `0.00`.

alpha.10's universal `+2.5 EV / ×5.656854` RAW multiplier is no longer used. `RawBaseExposureStops` and `RawBaseGain` remain source-compatibility constants at 0 EV / 1× only.

The current Neutral v2 anchor is `scene gray 0.03 -> display gray 0.18`. It is not treated as a universal camera constant. The checksum-pinned Sony A7 IV/ST render matrix swept an additional camera-base offset from -1.5 to +1.5 EV on three independent scenes. Per-scene minima occurred at `+0.5 / 0 / -0.5 EV`, while the three-scene mean RMSE was lowest at **0 EV** (`0.07073`; +0.5 EV was `0.08050`). Therefore no additional universal exposure offset is added. The anchor remains a provisional neutral rendering center for the current evidence set and must be re-evaluated as more camera/scene fixtures are added.

## RAW identity is not pixel encoding

`LinearProPhoto` describes the pixel representation. It is not proof that the source was RAW. `ProcessingPlan` therefore carries explicit `rawSource` and `baseExposureStops` fields. Preview and export pass RAW identity explicitly.

The JPEG export API defaults `rawSource` to `false`; callers must opt into RAW base rendering deliberately.

## Display color management

Image processing ends in encoded sRGB for the interactive preview. Monitor calibration is a separate, final presentation transform:

```
Linear RAW
  -> JixelLight processing
  -> encoded sRGB preview pixels
       |-> histogram / regression / CPU-GPU parity
       |-> export path (with requested output profile)
       |-> CPU/placeholder/reference provider display copy
       |      -> shared sRGB-to-monitor ICC 33^3 LUT
       `-> GPU display pass
              -> same sRGB-to-monitor ICC 33^3 LUT
              -> framebuffer
```

On Windows, JixelLight uses the current `QScreen` display-device name to create a monitor-specific Windows display DC, obtains its ICC profile through `GetICMProfileW`, validates it with LittleCMS, and builds a 33^3 float LUT. `NOMINMAX` is defined before including `windows.h` so Windows SDK `min/max` macros cannot corrupt standard-library calls.

The same LUT is used by the CPU/image-provider presentation copy and by `display.frag`. Embedded JPEG placeholders, reference JPEGs, CPU fallback previews and GPU RAW previews therefore share one monitor transform instead of switching between unrelated Windows/Qt color paths.

When no usable monitor profile is available, the display layer uses an identity sRGB LUT. A bad ICC profile fails closed to identity; it must never alter the processing output.

The monitor LUT is **display-only**. It must not affect:

- RAW/Creative Look fitting
- histograms/scopes
- exported pixels
- CPU/GPU processing parity
- cached linear RAW data

## Automated validation

alpha.11 adds:

- `monitor-color` — LittleCMS LUT generation, identity behavior, P3 transform, invalid-profile rejection, and CPU/provider LUT application.
- `display-color-gpu` — applies a synthetic inverse monitor LUT and proves that presentation changes while the processed GPU output remains byte-identical.
- existing `gpu-correctness` — CPU/GPU processing parity across D3D11, Metal and OpenGL.
- `JixelLightRawRenderMatrix` — real checksum-pinned Sony RAW/JPEG matrix.

The render matrix produces, for every pinned scene:

1. paired camera JPEG
2. embedded camera JPEG
3. CPU RAW Neutral
4. GPU RAW Neutral
5. CPU RAW As-shot
6. GPU RAW As-shot
7. CPU RAW FL3 diagnostic
8. GPU RAW FL3 diagnostic

The pinned Sony A7 IV dataset is ST, not FL3. FL3 rows therefore validate CPU/GPU consistency only and are never presented as a camera-authentic FL3 comparison.

For each comparable route the matrix records mean RGB, mean/median/p95/p99 linear luminance, mean CIE Lab (D65), highlight hue, p99 white chromaticity, shadow/highlight clipping and image deltas. It also records CPU/GPU deltas and a neutral exposure sweep from -1.5 to +1.5 EV.

Current pinned-set CPU/GPU parity is substantially tighter than the normal acceptance bound: Neutral is at most 1 code value in 16-bit RGB, As-shot at most 2 code values, and the current FL3 diagnostic is byte-identical on the Linux matrix run used for the alpha.11 review.

## Merge gate

Do not merge alpha.11 merely because it builds. The merge gate is:

- Windows, macOS and Linux build success
- core and real RAW tests pass
- D3D11/WARP, Metal and OpenGL compute tests pass
- monitor ICC LUT tests pass
- display-only GPU LUT test passes
- real RAW render matrix is generated without decode/reference errors
- CPU/GPU matrix deltas remain within the established numerical parity bounds
- Neutral v2 exposure sweep is reviewed and does not justify another universal base exposure

The current pinned Sony review satisfies the final Neutral-tone criterion. Merge still requires the complete three-platform CI run to be green at the final branch HEAD.