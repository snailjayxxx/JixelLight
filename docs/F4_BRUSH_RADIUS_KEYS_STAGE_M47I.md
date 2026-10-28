# F4 M4.7I: change brush radius from the focused canvas

Changing radius previously required returning to the side panel. The focused
brush canvas now accepts unmodified `[` and `]`: shrink by0.8 or grow by1.25,
clamped to the existing0.002–0.5 range. The default0.05 radius is unchanged.
The slider and cursor ring follow the same editor value. A separate bilingual
canvas caption shows the current radius and keys, including in compact windows.

Entering the brush tool requests canvas focus. The request is retained when
preview readiness temporarily disables the overlay; input still requires the
existing display/readiness guards. Keys remain local to the focused canvas.
Typing `[]` into the actual preset-name field leaves radius unchanged, and
Shift/Ctrl/Alt/Meta combinations are ignored.

A held mouse or pen keeps its frozen stroke style. Radius keys cannot cancel or
resize it, including an Esc-cancelled pen whose primary tip has not lifted.
Esc cancels a held stroke; from an idle canvas it exits the brush tool, after
which brackets do nothing. These editor choices do not change Develop intent,
render revision, project data or history. The next completed stroke uses the
selected radius and still has one Undo step.

The original ready, disabled-layer and capacity messages retain their exact
text. The radius caption is a separate `brushRadiusHint`; the existing
`maskToolHint` block only gains a stable id. The sampler, rasterizer, rendering,
exports, masks schema and request admission are unchanged.

## Original evidence and repairs

Private tests use an exact H source archive and keep their original `unknown`
build stamp. The first actual-key probe fails because canvas activation does
not support the new radius workflow. Later private wide-window cases pass.
An additional compact-window probe catches idle Esc leaving the tool active;
its failed native report, image, binary and source remain separate originals.
The repair exits the idle tool while preserving a held/cancelled pen owner.

A compatibility review identifies exact-string contracts in the older ready,
disabled-layer and capacity checks. Moving the new caption into its own label
preserves those contracts. The revised private version passes8 actual wide/
compact CPU/Metal1x/2x workflows, plus2 original pointer workflows and their
exact hint strings. The private results are not published-wrapper acceptance.

The first Root matrix has three successful original cases, then an actual
GPU2x activation-focus failure before painting. All original reports, captures,
source and binaries are preserved at
`evidence/brush-radius-I-root-v1-original-failed/receipt.json`. Root now requests
focus through temporary preview disabling; its keyboard QA waits for an active
window before the initial click and records actual window/item focus and enabled
state. The next complete matrix uses fresh paths and retains the failed run.

## Root local validation

A serial App/export-verifier build and all16 production-wrapper cases pass:
wide opaque/transparent/Sony CPU/Metal1x/2x, plus compact opaque CPU/Metal1x/2x.
The driver measures DPR with one neutral wide CPU run and retains that successful
actual2x case once. Remaining cases assert measured1x/2x; no source-only bypass
or report retagging is used.

Twelve owned-image cases verify120 actual key cases,72 held/cancelled pen guards,
36 bilingual radius captions and72 existing statistics-language records. They
retain17 captures each and all prior pressure/RGB/barrel/Esc/single-Undo/reopen/
exact PNG16/ICC assertions. The four genuine Sony cases retain11 controls-only
crops and their separate fresh-source export verifier. No photograph is added
to the public Sony evidence.

| Genuine Sony Root validation | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Complete GUI, ms | 14108 | 11645 | 13265 | 13503 |
| Combined export/exit/reference, ms | 38479.397 | 41899.596 | 36046.680 | 35777.814 |

Each Sony case verifies full32,947,264-pixel histogram/plots and exact16-bit output
pixels/ICC. All four PNGs retain SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.
Peak observed request reservation is1,770,329,984 bytes under the shared3GiB;
this is not an RSS measurement. The original45/60/combined120-second gates,
source32Mi cap and reservation refunds remain mandatory. Native Sony reports
remain partial until the separate verifier supplies aggregate acceptance.

Four additional actual2x production-wrapper regressions pass: original pointer
CPU/Metal14869/14373ms and compact Erase CPU/Metal9316/9412ms. The original seven
capacity cases, four recovery cases, exact bilingual ready/disabled/capacity
messages, manual scroll, selected stroke, language/scroll/reentry, RGB, history
and project reopen remain checked. The Erase flows keep all12 controls captures.

All210 tested source fingerprints, both binary fingerprints, genuine ARW and
daily preferences remain fixed through the runs. Original local reports retain
the parent `de2cfd9` stamp. Publication links them through exact source hashes;
it does not rewrite their stamps. Unchanged heavy numerical tests are not
repeated locally for this UI/QA-only slice; its own full committed CI remains
required.

An independent audit rechecks all20 reports, original wrapper/capture/input/
export predicates and the new key evidence, then archives the untouched originals
at `evidence/brush-radius-I-root-local-v2/receipt.json`. The earlier Root v1
failure stays separate. Maximum live/commit display difference is1/255 within
the original2/255 limit.

## Original committed CI failed

The committed I head `e0405de` runs in
[CI40704878923](https://github.com/snailjayxxx/JixelLight/actions/runs/40704878923).
Linux and macOS pass the strict68-test/native/numerical audits in1041.51s and
1499.28s; the actual macOS23-mode SDK-stripped package also passes. Windows
passes68 tests, the original pointer/Erase flows and all four owned pressure/
radius modes, then genuine Sony CPU pressure GUI validation takes60371ms,
exceeding the unchanged60000ms limit. The final full waveform is present;
Sony CPU export, Sony GPU pressure and the Windows package do not complete.
I is not accepted.

Four original validation ZIPs and three full original job logs prove every
actual checkout `d862db4f4d546eda15eddf109fe1593c4bce305b`, tree
`3dabcb880e777b1ac20a8a74516fa831139dd840`, equal to I. SHA/CRC/extracted-file
checks and the unchanged failed report are retained at
`evidence/ci-40704878923/terminal-failed-v1/receipt.json`. Linux/macOS successes
remain platform evidence. A [completed-frame counting follow-up](F4_SCOPE_PLOT_FUSED_COUNT_STAGE_M47I1.md)
keeps this failure and the original45/60/120-second gates.

The latest fully accepted checkpoint is H `de2cfd9` /
[CI40688164562](https://github.com/snailjayxxx/JixelLight/actions/runs/40688164562):
all68/all3/bothactual23, five original ZIPs and three full job logs proving
actual0ae1139/tree17e18 equal H. Its immutable terminal receipt remains in
`evidence/ci-40688164562/terminal-accepted-v1/receipt.json`.

I requires its own unchanged68-test suites on all three platforms and both
actual23-mode SDK-stripped packages, all older numeric/input/alpha/source/budget/
export gates and actual job-checkout evidence. The strict wrapper now requires
all10 key cases,6 held/cancelled pen guards and3 actual bilingual radius captions
for each owned pressure flow. A geometry-only
`JIXELLIGHT_BRUSH_PRESSURE_COMPACT_QA` switch selects1180×720 for local QA; it
cannot bypass source identity or any assertion/deadline.

Physical tablet validation, native GPU brushes, native FP32 RAW and remaining
F0–F6 are unfinished. PR16 stays Draft; main and PR15 are unchanged.
