# F4 M144: native committed brush colour-range rendering

Test-only ddac9ff reproduces the existing active-colour-mask admission refusal
on actual Metal:2 passed/1 failed/0 skipped/0 blacklisted in142ms, before any
native dispatch. The fresh configuration/build took9.94s/24.68s. Its20 retained
files/4,020,166 bytes include exact Git-blob sources, runtime, build metadata,
helper and raw output; log SHA256 is
`f89d0f1ebff9ca3633917229ec8fa19819a909d5fcc62acee2891ef1caf768aa`.
That baseline remains failed and its physical source/build are never reused.
It records an unsupported combination, not a regression in parent dde0abc.

The existing native colour dispatch now uses slots63/64 of the unchanged
65-vec4 plan. Selection measures scene colour before white balance and every
exposure gain, using the ordinary shader's identical range shoulders, wrapped
hue distance and low-chroma confidence. Finite OKLab selection optionally
inverts its weight; the gain applies after white balance/camera baseline and
before luminance, spatial and ordered brush gains, matching the CPU path.
No coverage plane, upload, dispatch, readback, parameter or memory budget is
added. CPU/export colour math and default-plan behavior remain unchanged.

Native admission accepts broad colour ranges for sRGB, Display P3 and ProPhoto
outputs. It retains CPU fallback below45-degree hue or .003 chroma/lightness
width, and for Adobe RGB output under the existing colour-mask precision
policy. The restricted-backend bright/perceptual bound includes positive
colour-mask exposure. Existing source identity, malformed plans, Point Color,
detail, hard brush, negative linear-ProPhoto and other precision refusals stay.

The three new methods and every numeric assertion from ddac9ff remain. The
inherited luminance policy and legacy grain method now require broad colour
admission; the new policy matrix separately retains the refused cases. The
mandatory preflight requires at least8 complete native Qt results, and its
six existing tests update synthetic complete/incomplete/failure/skip cases
without reducing evidence requirements. Expected counts are legacy82,
native8/336 numeric cases,106 CTest groups/100 Qt files/18 runtime guards.

Fresh Python283, Release, actual GPU focus, mandatory Sony/GPU preflight,
targeted15, exact default-display comparisons, full106/all100/all33 and own
three-platform/installed88/strict-delivery qualification remain unexecuted at
this checkpoint. All budgets and deadlines are unchanged. Parent PR86/CI442
is independent and remains running; its source and completed local validation
are frozen. Calibrated native float RAW, per-packet native live brush, physical
device and A7R VI photographic acceptance remain incomplete.
