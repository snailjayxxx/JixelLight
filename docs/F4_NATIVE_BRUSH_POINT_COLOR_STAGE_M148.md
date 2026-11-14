# M148: Point Color with committed native GPU brushes, controlled baseline

Status at source preparation, 2026-11-14: test-only extension, not compiled.
Parent02166d7 completes local106/all100/all33/all18 and private-camera checks;
its own Linux/macOS jobs succeed, while Windows is still validating its package.
These results do not accept this new source. Main and protected PR15 keep their
separate review path.

## Existing gap

The ordinary compute pipeline implements broad Point Color ranges in working
Oklab after grading and before output conversion. The committed native brush
frame still rejects every active Point Color plan. Its65-slot uniform already
contains Point Color adjustment/center/range slots58–60, but the brush shader
does not evaluate them. The current explicit CPU fallback is safe; this stage
defines the missing admitted combination before changing production.

## Added test contract, not yet executed

Four methods extend the dedicated native GPU group from8 to12 Qt results:

-72 combinations of U16/FP32 source, sRGB/linear/explicit RAW semantics, four
 output spaces and three nontrivial Point Color adjustments with ordered
 Paint/Erase/Paint, two brush layers and existing range/geometry masks. The
 existing working-color picker chooses a real nontransparent sample. Separate
 owning CPU requests must prove that turning Point Color off changes pixels.
-72 hue-seam/near-neutral/range-boundary cases, including45/.003/.003 admitted
 shoulders, signed adjustments, tiny/zero alpha, viewport mapping and grain
 where its existing output policy permits it.
-8 actual Sony128×96 cropped exports after full-source RAW decode, with exact
 normalized source-frame coordinates. These are not full-frame export tests.
-14 admission policies covering broad ranges, source mismatch, malformed plans,
 three narrow precision refusals, exact boundaries, Adobe output, coexisting
 color masks, inactive selection, retained grain and sharpening refusals.

There are152 new numeric cases, for488 with the parent's336. Every actual
dispatch must match the owning CPU reference within40/65535 RGB16, preserve
alpha exactly and produce exact1024-bin histograms from its actual output.
The existing single upload/coverage/color/histogram dispatch,8-byte control and
16,400-byte histogram readbacks, no normal finish and zero final budget charge
remain required. No new texture, readback plane or relaxed limit is proposed.

The initial focused execution must preserve the existing production/shader
bytes and demonstrate the expected CPUPolicy refusal before native dispatch.
It is an unsupported-feature baseline rather than a product regression. Its
original source, binary, graph, logs and failure remain frozen before a separate
implementation source/build. The existing broad-Point-Color refusal fixtures
will need explicit narrow selections when the supported combination is added;
their refusal assertions will remain and new broad cases provide coverage.

## Independent implementation acceptance still required

After the controlled baseline: a fresh Release, Python checks, complete
legacy82/native12/Sony preflight, targeted groups, all106/all100, all33 local
contexts and all18 runtime identities, then this source's own Linux/macOS/Windows
terminal jobs, installed88 on both desktops and strict artifact delivery. The
existing120-second GPU group and every job/precision/resource deadline remain.
No native live-per-packet, calibrated float RAW or photographic camera-color
acceptance follows from these tests. Private user photos stay out of CI/repo.
