# F4 M6 independent sharpening

Develop now provides bilingual Amount (0–100), Radius (.5–6 source pixels)
and Reset controls. Sharpening works independently of the Sony Look's own
sharpness/clarity settings. It participates in gesture-coalesced Undo/Redo,
per-version project/history storage, virtual copies, named/portable presets,
explicit XMP snapshots, all exporters and the new selective `detail` transfer
group. The CLI adds `sharpenAmount`, `sharpenRadius` and `sharpen.reset`; the
shared scalar registry has19 parameters. Reset preserves every other edit.

The effect adds up to1.5 times the difference between encoded luma and its
separable fractional box blur. Fractional edge weights avoid integer-radius
steps. This is an encoded-luma unsharp mask, not deconvolution or RAW capture
sharpening. Existing Sony detail and the independent term use the same
post-color/grain image and are added before final quantization; the Sony
parameters and profile identities are unchanged. With Amount0 the original
Sony CPU path, shader arithmetic and default canonical snapshot are retained.
An inactive nondefault radius is preserved as editing intent.

The independent blur weights luma by alpha and normalizes by filtered alpha.
Fully transparent centers receive no independent delta, hidden RGB cannot
contaminate visible neighbors, and output alpha is unchanged. GPU filtering
uses the same16-bit reference alpha grid, including tiny FP32 alpha values.
The original RGB40/65535 bound and exact-alpha checks remain unchanged.

Radius is measured on the complete post-geometry image. Prepared previews
retain that frame's dimensions and scale radius by actual pixel density;
100% viewports preserve source-pixel radius. Full exports and exact scopes
use the full-image density. Dynamic metadata participates in plan/cache keys.
Prepared brush admission validates and preserves it before calculating the
halo reservation and freezing the render. Processing radius is bounded at12.
The packed plan grows from52 to53 vec4 slots (848 bytes). No source decoder,
engine identifier, monitor-ICC policy, source-pixel limit or time gate changes.

Normal QRhi rendering reuses the existing two detail compute passes and
RGBA32F intermediate. Active independent sharpening with Paint/Erase uses
the complete admitted CPU reference and GPU display/statistics upload;
native brush FrameAdmission explicitly refuses it with `CPUPolicy`.
Existing FL/HSL numeric policies and grain's Adobe RGB fallback still apply.
There is no new sharpening-specific Adobe fallback in the tested Metal path.
CPU ownership adds8 bytes per expanded pixel to the existing conservative
reservation for the extra two horizontal planes. The shared3GiB budget and
32Mi-pixel source cap remain; retained outputs keep their charge.

The new `independent-sharpen` CTest is appended as78. Its14 targeted Qt
observations pass locally with no failures/skips. They include an independent
double-precision two-dimensional convolution oracle across two encodings,
four spaces and Sony on/off; default identity and transparent-color safety;
fractional density and geometry; exact uneven regions, all histograms and
three scope plots; strict snapshots and failed-open writer preservation;
commands, presets, XMP, selective detail transfer, projects/virtual copies,
and actual controller full-resolution scopes while the preview is downscaled.

Twelve raster cases cover PNG16, TIFF16 and WebP8 in four ICC spaces. PNG/TIFF
match all RGBA16 pixels exactly. WebP matches every alpha8 and every visible
RGB8 channel exactly; the codec rewrites hidden RGB at alpha8=0. The original
all-channel WebP comparison failure is retained rather than presented as a
successful lossless hidden-color round trip. CLI safety verifies deterministic
sharpened PNG16, a real pixel change, isolated reset, invalid-command refusal
and unchanged source bytes.

The24,000,000-pixel owned Paint/Erase test completes in4,488ms locally,
including fixture creation, hashing and three independent regional renders.
The full output and regional pixels match exactly; source SHA-256 is unchanged.
Peak accounted ownership is1,424,225,792 bytes, below the existing3GiB limit,
and returns to0. This is the owning budget, not a process-RSS measurement.
The original60-second large-operation deadline is not relaxed.

The expanded GPU method passes64 numerical/statistics cases locally with
maximum error7/65535 and exact alpha. Its56 ST/HDR cases require actual
compute; four separate FL cases assert the existing backend policy and four
Paint/Erase cases explicitly assert CPU upload. Native admission refusal is
checked on integer and FP32 source contexts. No fallback counts as compute.

Six fresh panel PNGs cover English/Chinese at300,320 and350 pixels, height780,
with actual Amount/Radius/Reset pointer input and Undo. Every new control,
value and Reset is bounded by the real window. The focused component host
uses Main.qml's Fusion palette; it does not establish normal-App, packaged
GUI, tablet-device or whole-panel subminimum-width acceptance. Original
fixture-path, preview-versus-full-scope and missing gesture-merge action
failures remain in local logs; the gesture action is included in coalescing.

Implementation was isolated while grain CI364 remained running. Targeted
validation is not an exact-commit78-test, three-platform or shipping-package
acceptance claim; the published commit must complete those existing gates.
Noise reduction, Texture/Clarity/Dehaze, healing and a native floating-point
RAW decoder remain outside this change and unfinished in the master plan.
