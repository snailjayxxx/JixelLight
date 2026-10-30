# F0 M4.7R: keep unchanged builds clean on spaced source paths

Qt6.8.3 qsb writes unescaped dependency paths. With the Gen8 source directory,
Ninja treats the space in `/Volumes/Gen 8/...` as a dependency separator and
requests seven shader generations, resource compilation and an App relink on
every otherwise unchanged build. The original diagnosis and failing control
remain preserved in the Q and private233-file archives.

The build now appends a depfile normalizer to the existing Qt shader command,
before CMake consumes its dependencies. Shader flags, resource names, shader
sources and direct include tracking remain. The workaround is restricted to
Qt6.8.3's inspected output format; unsupported input fails before replacing
the original depfile. It makes no recursive include-tracking claim.

Two existing code files change, CMakeLists and the native artifact-upload paths;
three new files provide the helper, CMake hook and regression test. The other211
tracked code sources, including processing, RAW, colour, brush and export
kernels, remain unchanged. The code-source total is216.

## Actual build and regression verification

The normal physical build uses `--parallel 1` on this8GB machine. It refreshes
the actual cached build stamp from961fb57 to32ab617 and rebuilds Core. Its first
App build takes184,559ms including configuration and recompilation. An actual
subsequent App build takes108ms, reports no work, and retains the App's exact
hash and modification time. All seven shader bundles remain byte identical.
The post-regression App dry run also reports no work.

The CTest registry contains all original68 names plus `qsb-depfile-spaces`.
Nine selected suites pass with zero failures/skips: freshCore215 in21.8217s,
five scope suites including scheduling12 and the checksum-pinned genuine Sony
case, GPU36, display-colour GPU4 and the new qsb regression in4.36053s.

The new regression runs real CMake/qsb commands in isolated scratch directories
with spaces and Unicode. It checks unchanged builds, source changes, direct
include changes with spaces/hash/dollar, and postprocessor changes, including
returning to no-work builds. Twelve malformed inputs must preserve their original
bytes. Every attempt keeps its inputs, outputs, commands and logs. Its timeout
is120s, and both native artifact uploads retain the scratch evidence.

Private actual-Mac Ninja, Unix Makefiles and Ninja Multi-Config runs pass.
Three deliberately invalid controls remain failed at the expected gate:
omitting the formatter, dropping includes or dropping the helper dependency.
The current6.8.3 test branch has a byte-identical projection of that original
control recipe. Helpers remain byte identical to the original seven-shader
experiment. Sealed private receipts contain233 and469 files respectively;
these are not native Windows or committed69-test acceptance.

## Actual App verification and evidence

Normal production App public Leica DNG CPU/Metal flows pass in20,565/20,457ms,
retaining full10,340,960 statistics, all original UI checks,23 captures per flow
and the original60,000ms bound. The scale request is `QT_SCALE_FACTOR=1`;
original capture dimensions are preserved without assuming an actual DPR.

The normal guide passes in4,726ms at measuredDPR2. It retains original14 contexts,
five captures, eight predicates and13 backend-caption contexts, with four
compact contexts/four captures/ninth check. Four CLI guards pass and daily
preferences remain unchanged. Independent audit0 and its closed collector seal
55 original PNGs, three actual App flows, source216, binary pins, test reports
and recipes at `evidence/F0-M47R-root-local-v1/receipt.json` (398 files).
Raw local32ab617 stamps remain; publication must prove all216 committed code
bytes equal these tested sources.

The future CI helper retains every original native numeric predicate by a
byte-identical projection, requires the exact old68 names plus the new test,
checks actual scratch outputs/helper hashes/command logs, and rejects16 derived
bad evidence cases. Unbound auditors/finalizers refuse execution.

## Accepted baseline and remaining acceptance

CI313 later completed all three GitHub jobs, but the final scratch qsb bundle
was absent from all three native validation ZIPs. Its preserved result is
**incomplete**, not strict acceptance; the test-only evidence correction and
actual CI313 results are documented in [M4.7S](F0_QSB_ARTIFACT_EVIDENCE_STAGE_M47S.md).

Q/32ab617 is fully accepted by
[CI312](https://github.com/snailjayxxx/JixelLight/actions/runs/40882457598):
all68/all3 zero failure/skip, both actual24-mode SDK-stripped packages, five
original ZIPs, three FULL actual3f5393f/treea830 logs, source213 and finalizer0.
Its2,458-file terminal archive is sealed. Q's original clipping failure and
earlier failures remain preserved.

M47R still requires its own all69/all3, both actual24-mode packages, five original
validation ZIPs, three complete runtime-source logs, source216 and finalizer0.
All original45/60s UI,120s export/reference, genuine Sony32Mi source ceiling,
shared3GiB reservation and colour/precision gates remain. Reservation is not RSS.
Leica RAW and owned UI fixtures do not replace public genuine Sony material.
Physical input/native OS activation, native GPU brush kernels, native FP32 RAW
and remaining F0–F6 work are unfinished. PR16 remains Draft.

Format/API references: [Qt6.8.3 qsb source](https://github.com/qt/qtshadertools/blob/v6.8.3/tools/qsb/qsb.cpp),
[CMake3.24 APPEND and DEPFILE](https://cmake.org/cmake/help/v3.24/command/add_custom_command.html).
