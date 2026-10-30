# F4 M4.7K: retain full brush checks within the existing GUI budget

The documentation-only [CI 303 / 40772451380](https://github.com/snailjayxxx/JixelLight/actions/runs/40772451380)
exposes narrow Windows CPU headroom in the ordinary brush UI workflow. Its
original report fails at phase 41 after **45,126 ms**, beyond the unchanged
**45,000 ms** gate. The final histogram has **149,292 pixels**, short of the
required **2,659,021**. Earlier full-statistics checks and all pan guard records
pass, but do not establish the final full-statistics result. Windows packaging
is not reached. The original root cause is not established by that report.

Linux and macOS each pass all 68 tests with zero failures/skips, in 1,088.19 and
1,140.07 s; the actual macOS package passes its 23 modes. All three complete
runtime logs prove actual checkout `09093224a021fcea3be88dbc7f2063168d5e3173`,
whose tree `ba28535b743e03164719953d59029bfd91ac8b6d` equals documentation
head `10fc96a`. Four original validation ZIPs and the three complete logs are
retained, with SHA256, ZIP CRC, safe extraction and byte checks, at
`evidence/ci-40772451380/terminal-failed-v1/receipt.json`. That run stays failed.
The earlier `f0f86df` / CI302 acceptance keeps its separate evidence. The
committed replacement acceptance is recorded below.

## Validation change

Only the owned brush QA helper and its production wrapper change. Before the
capacity matrix, the helper still checks and captures current full-source
statistics. It then clicks the actual **Full resolution** checkbox to select
preview statistics during repeated temporary capacity documents. All seven
refusal and four recovery cases keep their original document, history,
viewport, pointer, target-specific and bilingual feedback assertions. After
restoring the original brush document and language, it clicks that same control
to restore the full request. Both clicks must preserve brush intent and history.

The final UI gate additionally requires `exactScopes`, the current full plot and
both plot/histogram counts of **2,659,021**. It retains actual displayed-frame,
plot-image URL, visible plot-ink, input-ready and control-exposure assertions.
The production wrapper requires these additional results as well as every prior
brush/pan assertion and the original **45,000 ms** limit.

All 15 original evidence captures remain lossless PNG. `QImageWriter` requests
compression ratio 1, moving compression work out of this GUI path, and records
encode/grab counters and timings. [Qt 6.8.3](https://github.com/qt/qtbase/blob/v6.8.3/src/gui/image/qpnghandler.cpp) maps requested ratios 0 and 1 to zlib
level 0; the setting is not zlib level 1. Files can grow before artifact ZIP
compression. No public source photographs are introduced by these owned fixtures.

An isolated encoder experiment rewrites all 16 original failed Windows captures
plus an owned arbitrary RGBA64/alpha/ICC probe. Every decoded RGBA64 pixel and ICC
byte is identical at the tested settings. The sum of per-image median write
times on this Mac is 523.862 ms with the default and 261.205 ms with requested
ratio 1. This measures local PNG writes, excluding window capture, rendering and
statistics. It does not establish Windows end-to-end performance or the cause
of the original failure. Original images, code, reports and corrected Qt API
metadata remain at `evidence/brush-capture-codec-v1/receipt.json`.

## Local evidence

The serial production App build passes with Qt 6.8.3 and SDK 15.4. Four actual
Cocoa CPU/Metal flows at measured DPR 1/2 pass the strengthened production
wrapper:

| GUI workflow, ms | CPU 1x | Metal 1x | CPU 2x | Metal 2x |
| --- | ---: | ---: | ---: | ---: |
| Complete flow | 19,865 | 19,788 | 21,067 | 20,573 |
| PNG encode total | 403.133 | 415.728 | 1,211.425 | 1,165.437 |
| Window grabs total | 105.465 | 87.140 | 215.273 | 161.207 |

Each flow retains 15 original captures, all 24 previous brush flags, 14 pan
guards, three additional pan geometries, two bilingual captions, seven capacity
refusals and four recoveries. The final full request and both complete
2,659,021-pixel counts pass. Requested DPR is checked against actual native DPR;
PNG dimensions retain the native raster and product minimum height.

Fresh Core validation passes **215 checks with zero failures/skips** in
**24.9804 s**. An independent audit exits 0, verifies all four native reports,
60 original PNGs, binary hashes and all 210 source fingerprints, and retains
them at `evidence/brush-capacity-K-local-v1/receipt.json`. English CPU 1x full
statistics and the Chinese Metal 2x pan caption were visually inspected.
The original local reports retain their `10fc96a` build stamp; exact tested
source fingerprints accompany publication without retagging those reports.

The two changed QA files are the only changes among the 210 code-source
fingerprints. The Sony helper suffix, actual UI/controller, brush/colour/RAW
kernels, persistence and export code remain identical. The original
32 Mi source-pixel, shared 3 GiB reservation, 60,000 ms Sony GUI and 120,000 ms
combined export/reference gates remain. Physical tablet drivers, native GPU
brushes, native FP32 RAW and the remaining F4/F0–F6 capabilities stay unfinished.
PR16 remains Draft.

## Committed acceptance

Implementation `1fbfe715a929a5181b56d2d68a093e61f11ed3dd` passes
[CI304 / 40783796319](https://github.com/snailjayxxx/JixelLight/actions/runs/40783796319).
Linux, Windows and macOS each pass **68/68 with zero failures/skips**, in
**932.19 / 1,567.46 / 1,436.47 s**. Both actual SDK-stripped Windows/macOS packages
pass all **23 modes**, including all old controlled pressure, alpha, RAW, scope,
cancellation/refund and independently checked export gates.

| Committed GUI, ms | Windows native | Windows package | macOS native | macOS package |
| --- | ---: | ---: | ---: | ---: |
| Owned canvas CPU | 43,910 | 43,920 | 37,962 | 37,027 |
| Owned canvas GPU | 26,596 | 26,511 | 38,454 | 36,735 |
| Genuine Sony pressure CPU | 49,533 | 49,612 | 17,911 | 22,068 |
| Genuine Sony pressure GPU | 25,650 | 25,717 | 18,115 | 19,692 |
| Sony combined export/reference CPU | 70,617.761 | 72,309.140 | 48,788.336 | 50,213.511 |
| Sony combined export/reference GPU | 73,323.318 | 71,236.126 | 46,188.883 | 48,288.233 |

Windows owned CPU retains about 1.08 s headroom under the original 45 s gate.
The completed histogram and plot both contain all 2,659,021 pixels. Every Sony
flow retains all 32,947,264 pixels, a separate fresh-source exact PNG16/ICC
reference and the shared 3 GiB reservation. PNG byte hashes are platform-local;
the independent reader compares every pixel and ICC byte on each platform.
This remains a QA scheduling/encoding change, with every original deadline and
pixel assertion preserved. The original CI303 report stays failed.

Five original validation ZIPs and three complete runtime logs prove actual
checkout `64dbdc9e22a418bb5d685309d3e0a12b2a923ff6` on all three jobs. Its tree
`afb02fb897dc8bcccd841e9ca7c1fd0fd5cdb085` equals the implementation tree. ZIP
SHA256/CRC, safe extraction and extracted bytes are verified. All strict platform
and package auditors, and the finalizer, exit 0. The immutable
`evidence/ci-40783796319/terminal-accepted-v1/receipt.json` retains these originals,
auditors, all 210 tested code sources, separate partial snapshots and the earlier
derived-auditor driver failures. Those driver errors concern a log filename alias
and optional owned-pressure summary fields; original traces remain and strict
assertions are unchanged. Native workflows were not replayed for those repairs.
The earlier accepted J / CI302 evidence and all original failed runs remain.
