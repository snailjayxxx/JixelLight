# F4 native committed brush and negative dehaze — M151

Status at source preparation, 2026-11-14: test-only baseline, not compiled.
Parent fd36e48890044d2dbf6765277987b14d81147f4e is independently accepted:
local106/1186.25s, all100 Qt groups, all33 validation contexts, eighteen runtime
and five build-graph guards, current-source private compatibility and own
CI447 all three terminal successful jobs. Both desktop packages pass88 checks.
The final local qualification index SHA256 is
410ac1b3ffee5a4d90c3d003dbbbcdfb86eb5f0ece6e3e7fbdd829a5d4ccac0d.
Main and protected draft PR15 remain unchanged and unmerged.

## Existing gap

FrameAdmission currently refuses every active dehaze plan. The ordinary GPU
negative-dehaze branch is a pointwise uniform veil before colour adjustments:
transmission=1+0.65*amount, then airlight+(linear-airlight)*transmission for
alpha above1e-20. Positive dehaze requires spatial neighbourhoods and remains
outside this stage, as do denoise, texture, clarity and sharpening.

The intended implementation may use the existing slots56/57 and existing
linear input before range-mask selection, white balance and exposure. No extra
texture, compute pass, normal-path readback or new uniform slot is proposed.
Full corrected-frame airlight must be reused for cropped/downsampled previews;
an unresolved partial frame must not estimate a new local airlight.

## Test-only contract, not yet executed

Four new mandatory methods extend the existing native GPU test group:
-72 integer/FP32, sRGB/linear/explicit-RAW, four-output, three-amount cases
 combine negative dehaze, ordered brush paint/erase/repaint, range masks and
 broad Point Color. Independent owning CPU renders with each effect disabled
 must differ, so omitting either effect cannot pass.
-48 negative/HDR/alpha cases include full input, cropped view, downsampled full
 input and downsampled crop, using complete-frame airlight where required.
-8 real Sony128x96 crop cases decode the complete source first and estimate
 airlight from the complete decoded frame. These are cropped exports, not
 full-frame export or photographic calibration evidence.
-18 admission policies require negative amounts and resolved partial contexts
 to admit, while positive amounts, malformed facts, source mismatch,
 cancellation, unresolved partial context and existing precision/spatial
 policies retain their expected refusals.

The existing488 comparisons and32 policies remain, for an expected616 numeric
comparisons and50 policies, with16 Qt results including initialization and
cleanup. Every unchanged40/65535 bound, exact alpha, exact1024-bin histograms,
source upload/coverage/colour/histogram counts, readback bounds, ownership,
retirement and zero remaining budget assertion stays in force. The helper
copies optional full-frame facts by value and retains both independent
effect-disabled CPU comparisons.

Production, UI, CPU pipeline, shaders, CMake, workflows, tools, registry and
timeouts remain unchanged for this baseline. The unsupported-feature focus is
expected to retain the existing CPUPolicy refusal before native dispatch.
Its source, binary, five graphs and original logs must be frozen before a
separate implementation source and physical build.

## Independent implementation acceptance still required

Implementation must raise native preflight minimum12 to16 and adjust wrapper
fixtures while retaining Python283, legacy82, full106/all100/all33,
eighteen-runtime/five-graph guards, all three own platform jobs, both desktop
installed88 and strict artifact delivery. The existing120-second GPU group,
120-minute Windows cap,65 uniform slots, CPU4 local and3GiB/32Mi-pixel brush
budgets remain unchanged. Heavy commands require3GiB on the actual temporary
volume. No physical-device, native float RAW, photographic calibration or
per-packet native live-brush claim is added.
