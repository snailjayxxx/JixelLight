# F4 native brush extreme channel-span precision — M154

Status at source preparation, 2026-11-14: not compiled. Independently accepted
workspace remains fd36e48; main and protected draft PR15 are unchanged.

## Retained second failure and decisive diagnostic

0098ccd55eb827dd0bb5663a3245e8c7967e2308 passes Python283 and fresh Release
214.382281s/recovery9, then native16 remains15 passed,1 failed: the new normal-
alpha HDR matrix still reaches53/65535. All55 files/61,418,957 bytes plus
complete source remain failed, with all eighteen runtimes and seven recorded
build files unchanged. Original log SHA256:
acc1a6e0a7b981daa1bc659a44b8a9ac921be6466d4c2df591296df44d013149.
Manifest SHA256:
463940b1c3f412f8911363ac6b4730246cdf6f0719fab62b92db2c8b087c49e0.
No full/own platform submission/accepted0098ccd index or promotion exists.

A separate test-only diagnostic ff1f38d, fresh target51.811455s, actual Metal
1.374819s, retains2/1 results and unchanged source/binary/graphs. At pixel10,7,
source RGB is[65504,0.2,-0.03], alpha0. CPU RGB16 is[32209,187,3192] while
native output is[32156,187,3182]. Turning dehaze completely off gives the exact
same values and53 error. This isolates a pre-existing native colour precision
boundary for the mixed-channel HDR input, rather than proving a dehaze-only
arithmetic failure. Diagnostic log SHA256:
287c346aabe3dee8960aa89a05f226073c992f00da7058a08cb72dab42a59f64.
It does not qualify or relabel either failed product source.

## Precision policy, without source clipping

Validated FP32 source facts now record whether any pixel's maximum-minus-
minimum RGB channel span exceeds16. This is a conservative native compute
precision region; valid scene input remains accepted up to the existing
65536 magnitude, including hidden RGB at zero alpha. The existing validation
walk collects this bit without a new frame buffer or scan.

Native frame admission retains the full CPU path for that source class with
dehaze on or off. The ordinary GPU path already uses complete CPU handling
for active brushes; its compute admission and shader remain unchanged.
Its diagnostic now identifies wide-rgb-span for these brush references.
CPU colour/render/export math, input validity, RAW decoding, shaders,
65-slot layout, caches, texture/pass/readback and resource budgets stay
unchanged. Header/fact collection changes are metadata only.

## Additive mandatory coverage, not yet executed

All prior664 render inputs remain, including both extreme-alpha modes and
their smooth-resized contexts.48 further bounded-channel HDR/context cases
use a15.03 maximum span, and4 actual compute cases exercise the exact16
boundary with dehaze enabled/disabled in sRGB and Display P3.

Expected matrix totals are716:584 actual GPU/reference comparisons and132
explicit owning CPU precision controls. Direct native policy cases rise52
to56, including the retained extreme pixel with dehaze on/off, exact16
admission and nextafter(16,+infinity) refusal. Existing RGB40, exact alpha,
1024-bin histogram and resource/ownership assertions remain. All original
488 native comparisons remain compute cases.

The ordinary numeric-source regression retains its original cases and
expected compute/signed-ProPhoto policies, adds facts/reset checks, boundary
parity and exact CPU brush-reference checks for the retained HDR reproducer.
Native Qt16, legacy82, Python283, full106/all100 and eighteen-runtime
registries remain unchanged. Independent Qt6.8.3 inspection confirms all12
full/cropped/scaled input classifications: extreme spans exceed65503 and the
bounded spans stay below15.03.

Fresh Release, complete GPU/Sony preflight, targeted15, unchanged default
PNG16/ICC, native normal-App/pan/proof/relink/Move, frozen all106/all100/all33/
all18/five graphs, own three-platform CI, both desktop installed88 and strict
source-tree/delivery originals are still required. No original precision,
budget or deadline is relaxed. Native float RAW, photographic calibration,
physical devices and native live-per-packet painting remain outside scope.
