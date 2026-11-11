# F4 Stage M3.3 — two-sided feather guides + Shift precision

Development branch: `design/lightcraft-fusion-20261008`; PR #16 **Draft**.

## Scope and behavior

The already accepted single linear and single rotated radial local-exposure gradients each now expose **two independently hittable green handles** on opposite sides of their inner feather boundary. Both handles edit the **same** validated `feather` parameter, not two different values. This is a direct canvas usability stage, not brush painting or layered masks.

- Linear gradient: opposite handles are at `center ± feather/2` along image-normal `(sin(angle),cos(angle))`. Positive grip applies `+2*projection`; negative grip applies `-2*projection` to the frozen initial value.
- Oval: opposite handles are at `center ± radiusX*(1-feather)` along the **rotated local X axis**. Positive grip changes `feather` by `-projection/radiusX`, negative by `+projection/radiusX`.
- Holding **Shift when pressing** a handle latches 20% normalized pointer movement for fine-tuning. The fine scale also applies to center/radius/angle handles using the same frozen gesture origin. Changing Shift during a held drag does not abruptly change sensitivity.
- Clamps, strict commands, persisted project/preset/XMP schema, source-image immutability, per-gesture Undo/Redo, ROI mapping, CPU/GPU render parity and both right-side feather sliders are **unchanged**. The panel text now correctly describes implemented canvas editing instead of claiming it is missing.
- Near feather=1 the two inner oval grips may overlap the center. The independent numerical slider is retained as a fallback. No automatic painting, healing, brush, AI selection or same-type mask layers are claimed.

## Mandatory validation

The independent test-owned 1400×1000 synthetic PNG native QML smoke:
1. exercises existing oval/linear center, full-image and zoom ROI mapping;
2. drags each original positive feather handle and verifies one Undo/Redo;
3. rotates the linear guide to 30°, drags its **negative** feather boundary and validates signed feather delta and Undo/Redo;
4. repeats that grip while holding Shift and checks the actual ~20% effect with Undo/Redo;
5. rotates the oval to -40°, drags its **negative** inner contour and similarly checks normal and Shift-fine changes with Undo/Redo;
6. checks the source SHA-256 has not changed and no QML TypeError/ReferenceError.

The independent pointer-smoke deadline stays 30s (outer Python process 45s), with the original **60,000ms real full RAW GUI CPU test unchanged**. Windows x64, macOS ARM64 and Linux GPU all must pass their own accurate-HEAD CI, mandatory 40/65535 GPU parity, 1024-bin full-image scopes, public Sony RAW/JPEG / Stage 6C, Windows/macOS deployed package and original source safety. Any failure must be fixed and revalidated, never skipped.

## Next scope

The one-linear/one-oval constraint remains. Follow-on F4 work includes multi-layer mask serialization and brush/color/luma masking, which require their own versioned plans and CPU/GPU evidence. True calibrated camera FP32 RAW is a separate F3 research gate, not part of M3.3.
