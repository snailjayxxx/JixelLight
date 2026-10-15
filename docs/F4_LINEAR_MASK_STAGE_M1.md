# F4 Stage M1 — first single non-AI graduated exposure mask

Branch: `design/lightcraft-fusion-20261008`, PR #16 Draft. **Exact-head CI pending.** This is only the first mask milestone, not a complete Lightroom mask stack.

- CPU oracle and QRhi shader apply **one** linear gradient EV in the scene-linear domain after WB/camera/global exposure and before vignette/highlight/tone. RGB is multiplied by `exp2(EV * smoothstep(.5 + (projection - center)/(2*feather)))`; alpha is unchanged. `projection = x*sin(angle)+y*cos(angle)`, with full-frame normalized post-geometry/crop/rotation coordinates. Down = 0 degrees; right = +90 degrees. No mask bitmap, AI, network access or extra full-frame buffer.
- Controls: enable/disable, exposure −3..+3 EV, center 0..1, feather .02..1 and angle −180..180 degrees. In the bilingual Develop panel, effect can be adjusted and reset, with History Undo/Redo, preset, virtual copy, XMP, project persistence and separate selective transfer group `masks`.
- CLI / UI replay have strict `mask.linear.enable`, `mask.linear.set` and `mask.linear.reset`; invalid extra/missing/unknown fields or out-of-range values reject without changes. Strict optional `linearMask` schema 1 rejects invalid project state. A legacy/default snapshot remains byte-identical and default/disabled pixels unchanged.
- Core tests cover endpoints, alpha, full/tiled/viewport, horizontal/vertical orientation, diagnostics, history, XMP/presets/projects, refusal. Mandatory GPU tests cover four angles, full/viewport, encoded/linear input, sRGB/ProPhoto output, exact GPU histogram/alpha and **original 40/65535** pixel tolerance. Deployed CLI checks actual PNG output and exact disabled/reset pixel identity.
- GUI CPU smoke remains **60,000 ms** and must still restore original RAW, Sony Look, full-resolution 1024-bin Scopes; Windows/macOS/Linux deployments, real Sony images and mandatory GPU cannot be skipped.

**Still not implemented:** canvas handles, brush strokes, radial/color/luminance range masks, multiple masks, per-mask WB/HSL, healing or Adobe Develop XMP conversion. No private images or Google Drive file lists uploaded.
