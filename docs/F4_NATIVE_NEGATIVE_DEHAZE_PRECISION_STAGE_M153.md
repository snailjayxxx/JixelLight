# F4 native negative-dehaze precision policies — M153

Status at source preparation, 2026-11-14: not compiled. Accepted workspace
remains fd36e48; main and protected draft PR15 remain unchanged and unmerged.

## Preserved genuine first implementation failure

206774cfd0beb392b28e8ef3eedef647c46389f7 builds Release in201.022572s and
passes recovery9, then actual Metal native16 records14 passed,2 failed,
0 skipped,0 blacklisted. The HDR/alpha row reaches53/65535; a real Sony
Adobe RGB row reaches77/65535, both above the unchanged40 limit.

All50 source/runtime/graph/helper/metadata/log files,61,280,150 bytes, and a
complete source archive stay frozen. Original log SHA256:
f948e3777d601003a080c7b31ee1221399dec144e6393167cb6490f3f1c11804.
Manifest SHA256:
9145b306c3f449d8f4f2c072275680f4b64993cb23aed1f04b085070d82a2f29.
All eighteen runtimes and seven recorded build files remain unchanged.
No full/all33/accepted206774c index or promotion exists. Own CI448 original
capture continues independently. Neither failure is relabelled.

## Narrowed native admission and additive validation

The ordinary GPU route already retains CPU precision for spatial input with
positive alpha below1e-18. Native negative-dehaze admission now retains that
same source fact, and retains negative-dehaze Adobe RGB output on every
backend after the measured77 error. Shader and CPU image math stay identical
to206774c. These policies preserve40 rather than asserting unsupported
compute precision. The policy audit does not establish an exclusive cause
for each original numerical discrepancy.

Every original616 render-matrix input remains: affected cases now explicitly
require CPUPolicy refusal and a complete owning CPU reference with exact
quantized alpha, full pixel count, independent effect-disabled differences,
zero native upload/dispatch/finish activity and complete budget release.
All admitted native cases retain every original RGB40, exact-alpha,
1024-bin histogram, resource/readback and ownership assertion.

48 additional normal-alpha HDR/context cases complement the original tiny-
alpha inputs. Independent Qt6.8.3 inspection finds117/35 tiny-alpha pixels in
full/cropped original inputs and0/0 after the two smooth resizes; normal-alpha
inputs have none. Each test verifies its input classification explicitly.
Expected totals are664 render-matrix cases:596 actual compute comparisons
and68 explicit CPU precision controls. Two additional direct admission rows
bring policy cases50 to52; mandatory native Qt16 and legacy82 remain.

## Independent fresh acceptance still required

A separate source and physical build must pass Python283 before compilation,
Release, actual native16/596/68/52, Sony/legacy82 preflight, targeted15,
unchanged default PNG16/ICC, normal-App/pan/proof/relink/Move, frozen
full106/all100/all33/all18/five graphs and its own three-platform CI.
Both desktop installed88, matching compiled tree and strict delivery remain
required. No budget, precision, time limit or test registry is relaxed.
Native float RAW, photographic calibration, physical devices, positive
spatial dehaze and per-packet native live brush remain unaccepted.
