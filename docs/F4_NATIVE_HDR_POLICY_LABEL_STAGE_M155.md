# F4 native HDR diagnostic-label fixture — M155

Status at source preparation, 2026-11-14: not compiled. Independently accepted
workspace stays fd36e48; main and protected draft PR15 remain unchanged.

## Retained original result

c5383d465886508e79f72716d958944cba476513 passes Python283/1.453731s,
Release198.258573s, recovery9, actual Metal native16/584 GPU comparisons/
132 owning CPU precision controls/56 policies with maximum26 under40.
Sony real-file checks and the repeated native16 preflight also pass.

The complete preflight remains failed: legacy GPU81/1 expects none for a
non-wide active brush while the unchanged production diagnostic correctly
reports existing-color-or-brush. Legacy original log SHA256:
204243fde69be70fc0cb11b488738e79a514ac9fa0f8d70b5869965a1bf9b73f.

All98 source/runtime/graph/helper/report files,64,753,785 bytes, plus complete
source remain frozen. Manifest SHA256:
e26267b2f792ee19c3553f7a16e1a793d66266bf7480a7d35afa0c4fab066290.
All eighteen runtimes and recorded build files remain unchanged. The original
failed preflight is not relabelled. No full/all33/index/promotion exists for
c5383d4. Own CI449 capture continues separately.

## One fixture correction

Only the non-wide brush expected diagnostic string changes from none to
existing-color-or-brush. The wide case still requires wide-rgb-span, exact
CPU fallback and all independent pixel/producer assertions. Every source
input, assertion count, legacy82/native16/Python283 count, RGB40 condition,
alpha/histogram/resource/ownership assertion remains.

All application, core, shaders, UI, tools, other tests, CMake, workflows,
triplets and dependency versions are byte-identical to c5383d4.

## Required independent acceptance, not yet executed

Fresh Python283 before compilation, separate physical Release, native16/
584 GPU/132 CPU/56 policies, complete Sony/legacy82/native16 preflight,
targeted15, default PNG16/ICC, normal-App/pan/proof/relink/Move, frozen
full106/all100/all33/all18/five graphs, current private compatibility and
own three-platform CI remain required. Both desktop installed88 and strict
delivery/matching source trees remain mandatory. No original deadline,
precision or resource budget changes.
