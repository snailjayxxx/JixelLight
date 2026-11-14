# M149: Broad Point Color in the committed native brush color dispatch

Status at source preparation, 2026-11-14: implementation not yet compiled.
The independently accepted parent02166d7 has local106/all100/all33/all18, own
CI445 success on all three platforms and installed88/strict delivery on both
desktops. Those results cannot qualify this new source. Main and protected PR15
remain unchanged and retain their review path.

## Retained controlled baseline

Test-only e3ff8b8 configures in79.55s and builds the focused native target in50.10s
after Python283 passes. The new first method fails2/1/0/0 in0.90s because existing
production refuses active Point Color with CPUPolicy before native dispatch.
All20 source/graph/runtime/log files,4,243,188 bytes, are retained alongside the
unchanged originals. Focused original log SHA256:
`9a2b96a8e161fdbed2c3ffc9b16083bb97a5caf09b25bfaa508d3c2c5196cb27`.
This unsupported-feature baseline stays failed and is not a product regression.
A separate physical second build is required for this implementation.

## Change

The native brush shader copies the ordinary shader's Point Color function
byte-for-byte, consuming existing slots58–60 after perceptual/Sony/B&W/grading
and before output conversion. The existing shared selection shoulder helper is
reused. Admission now uses the ordinary plan's narrow-range precision predicate
instead of refusing all active selections. Hue below45 degrees or chroma/luma
ranges below.003 retain the complete owning CPU reference on every backend.
All other source, HDR/gamut, grain, spatial, budget and thread policies remain.
There is no new uniform slot, texture, pass or readback.

Three inherited Point Color refusal fixtures now select an explicit five-degree
range: two native admission methods and the legacy grain/native policy loop.
Their refusal assertions and counts remain. The new numeric/admission methods
cover the newly admitted broad behavior. The mandatory preflight requires all12
native Qt results, with six existing wrapper tests updated for that complete
count and their incomplete/failure/skip evidence. Legacy82 and CTest106/all100/
eighteen-runtime registries remain; all deadlines and precision limits remain.

## Required independent acceptance, not yet run

Fresh Release and Python283; mandatory real-Sony/legacy82/native12 preflight;
all488 actual GPU/CPU-reference cases and32 admission policies; unchanged
40/65535 RGB16, exact alpha and1024-bin histograms; targeted15, actual Cocoa
CPU/Metal DPR1/2 normal App/pan/proof/relink/Move and exact default PNG16/ICC;
full106/all100/all33/all18 and verified retention; then this exact source's own
Linux/macOS/Windows terminal originals, matching trees, installed88 on both
desktops and strict artifact delivery. New Sony cases are128×96 crops after
full-source decode, not full-frame exports or photographic calibration.

Private A7R VI evidence remains separately linked to accepted02166d7 and does
not substitute for these gates. Calibrated float RAW, physical input/printer
validation and native live-per-packet painting remain outside this checkpoint.
