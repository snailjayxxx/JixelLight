# F4 Stage M2.1: rotated manual oval mask

Implementation candidate on `design/lightcraft-fusion-20261008`, PR #16 Draft; exact-head three-platform CI **pending**.

The existing one-ellipse non-AI radial EV mask accepts `angle` in [-180,180] degrees. Elliptic coordinates are rotated before applying the existing per-axis radii and unchanged smooth feather. Zero angle retains the original CPU and GPU coordinate formula. The optional `angle` key is stored only when nonzero, preserving schema-1 old saved project, preset, XMP and history snapshots. The parser rejects out-of-range/non-finite and redundant zero-angle properties. No RAW/Look/ICC/Scopes defaults change.

The bilingual Develop panel exposes ellipse rotation. Existing `mask.radial.set` and reset commands, `masks` transfer and project persistence carry the angle. New Core tests check old serialization, strict rejection, and rotated pixel positions. Mandatory GPU matrix uses four ellipse angles without relaxing 40/65535 per-channel parity or exact alpha/1024-bin Scopes. No canvas handles, multiple masks or automatic subject selection are claimed. Windows/macOS/Linux real Sony RAW/JPEG, package CLI and strict 60,000 ms deployed GUI CPU smoke remain mandatory.
