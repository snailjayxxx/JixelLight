# F4 native committed brush and negative dehaze implementation — M152

Status at source preparation, 2026-11-14: implementation not compiled.
Main and protected draft PR15 remain unchanged and unmerged. Accepted
workspace fd36e48 keeps its own independent qualification.

## Controlled unsupported-feature baseline

Test-only38247e5aa27c2de3cf14077f327fb7167a1b537e keeps production unchanged.
Python283 passes in1.445826s; fresh NAS configure82.089774s and focused-target
build48.973162s complete. Actual Metal focus takes0.971033s and records2 passed,
1 failed,0 skipped,0 blacklisted: the existing CPUPolicy refusal before native
dispatch. This is the unsupported-feature baseline, not a product regression.

All22 source/binary/five-graph/helper/metadata/log files, totaling4,320,235 bytes,
remain frozen with verified copies. Original focus log SHA256:
577bc0b2d716b7af78f82192a29e9fde2599110d5b82074e46d8495cefaf0f3f.
Failure manifest SHA256:
17844a574046145da9a25c4799a1bd5c5cc079b5e67eb04a46dc9cc0ecc822d4.
The original failure is not relabelled. Implementation uses a separate source
and a fresh physical second build.

## Change

The native colour shader uses the existing slots56/57 and copies the ordinary
negative-dehaze transmission and veil arithmetic, with the same alpha guard.
It runs after input linearization and before luminance/colour selection,
white balance, base/user exposure and brush gains. Inactive input remains
unchanged; no texture, compute pass, normal-path readback or uniform slot is
added.

Frame admission retains positive dehaze and all other spatial/precision/source
policies. A validated negative plan resolves absent airlight from a complete
canonical input on the worker, after work-budget reservation. Existing
full-frame facts are reused for partial/downsampled input; unresolved partial
contexts refuse. Cancellation and allocation failure retain explicit outcomes.
The resolved plan moves into the immutable frame, avoiding an extra retained
plan copy. The existing resolver uses bounded airlight sampling.

The inherited Grain regression retains both signed amounts and now explicitly
requires negative admission and positive CPUPolicy refusal. All its other
assertions and legacy82 count remain. New native numerical/policy methods
remain byte-identical to the retained test-only baseline. Mandatory native
preflight minimum changes12 to16 with corresponding wrapper fixtures.

## Required independent acceptance, not yet executed

Python283 before compilation, fresh Release, recovery9, legacy82/native16/
Sony preflight, targeted15, exact default PNG16/ICC comparisons, actual normal
application CPU/Metal DPR1/2, pan/proof/relink/Move, frozen full106/all100/all33,
all eighteen runtimes/five graphs and own three-platform CI remain required.
Both desktop installed88 and strict delivery/source-tree checks remain.
The616 numeric comparisons and50 policies retain40/65535, exact alpha,
1024-bin histogram and all resource/readback/ownership assertions.

All existing CPU4,3GiB/32Mi-pixel,65-slot,120-second native group and platform
deadlines remain. Heavy local commands require3GiB on the actual temporary
volume. Positive spatial dehaze, other spatial effects, native per-packet live
painting, photographic calibration, native float RAW and physical-device
acceptance remain outside this stage.
