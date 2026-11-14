# F0 M156: sign and validate the actual macOS shipping archive

The downloaded CI450 macOS package cannot start on the local Apple M2/macOS26.5
host. Its source, all local 106/100 Qt groups/33 contexts/private compatibility checks and all three original
CI450 jobs pass, including both 88 installed-package checks. That evidence does
not replace the additional failed delivery check. Candidate 5fb739f remains held;
the accepted workspace remains fd36e48. Main and protected draft PR15 are untouched.

## Observed failure

The provider archive digest and every extracted regular-file byte, executable
permission and symlink target match the original shipping ZIP. The actual App
exits by SIGKILL (-9) before producing its report. The system kernel explicitly
reports an invalid signed page in bundled `libqcocoa.dylib`. Strict bundle and
plugin signature verification both fail. This is a packaging defect, not an
established regression in negative-dehaze arithmetic.

All 66 retained source/runtime/graph/helper/delivery/log files total 263,338,770
bytes, plus a complete source archive. Manifest SHA256:
`90e972959fa869a62c4562940b3a7ae58fe9867ad32d9de34f6e9ddd63347154`.
The original package, failed extraction/launch evidence and all 18 runtimes/five
graphs remain unchanged. The collector and promoter stopped before creating an
accepted 5fb739f index or advancing the workspace.

On a separate complete copy, Qt 6.8.3 deployment with explicit `-codesign=-`
restores strict nested signature verification and startup `--version` success.
This is a causal startup diagnostic; the original failure remains failed, and
it does not establish whole-App or new-source qualification.

## Change

- Explicitly request ad-hoc signing after Qt dependency deployment, then require
  strict recursive verification of the whole bundle. No system security setting
  or quarantine policy is changed. This is development-bundle integrity, not a
  Developer ID/notarization claim.
- Create the shipping ZIP before package qualification, extract it into a fresh
  runner directory, verify its complete nested signatures and compare every
  original/archived/extracted entry. CRC, file bytes, executable bits, directories
  and symlink targets must agree; unsafe, duplicate and unsupported entries fail.
- Run the existing 88 installed-package checks once against that extracted App
  and its extracted helpers. Verify its signature again afterward. Original
  product gates, deadlines, artifact IDs/digests and strict delivery remain.
- Run eight portable archive-corruption/refusal checks on all three CI platforms.
  The new verifier also compares the actual held archive: 1696 regular files and
  166 symlinks, with exact bytes. Byte integrity alone does not imply a valid
  signature; the independent signature gates remain mandatory.

Application/core/shaders/UI/CMake, existing tests and dependencies are unchanged
from 5fb739f. Native 16/legacy 82, 584 GPU comparisons, 132 complete CPU precision
controls, 56 policies, 40/65535, exact alpha/1024-bin histograms and the existing
resource budgets remain. No image math, RAW defaults or Sony evidence changes.

## Qualification still required

The new source needs all existing Python methods plus the eight new methods,
a fresh Release build, original full106/all100/all33/all 18/five-graph closure,
current-source compatibility and its own three-platform terminal delivery.
The actual new shipping ZIP must also pass strict signatures and normal-App
validation on the distinct local host before acceptance. The diagnostic copy or
any older passing source cannot substitute for these gates. Overall F0–F6,
photographic calibration, native-float RAW and physical-device acceptance remain
incomplete.

References: [Qt 6.8 deployment](https://doc.qt.io/qt-6.8/macos-deployment.html),
[Apple nested signatures](https://developer.apple.com/library/archive/technotes/tn2206/).
