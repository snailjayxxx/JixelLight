# F4 M4.7J: pan a zoomed canvas while using the brush

Brush mode previously owned the canvas pointer, so a zoomed view required leaving
the tool to pan. A focused brush canvas now accepts unmodified **Space + left
drag** at 100% and 200%. Releasing Space during the drag retains navigation until
the mouse lifts. Navigation changes only the viewport, with no brush stroke,
edit-history entry or persisted intent change. Escape cancels navigation while
keeping brush mode. The existing Escape behavior for a held stroke is retained.

The cursor becomes a hand and the brush ring is hidden during navigation. A
bounded English/Chinese caption adds the gesture before the existing radius
caption. Fitted axes keep their center. Panning uses exact post-crop/orientation
dimensions, correcting the old metadata-based normalization for rotated crops.
Ordinary browsing retains its metadata fallback outside the brush geometry
dimension domain; brush admission and its 131,072 dimension cap are unchanged.

Text fields retain Space input. Modified keys, orphan autorepeats and Space
pressed during a held or cancelled stroke cannot take over navigation. Radius
keys remain unchanged during a pan. Pen packets arriving during navigation are
ignored until that pen's primary lift; a fresh press can then paint. Focus loss,
Qt window deactivation, tool exit, crop entry and source changes cancel navigation.
Fit mode does not start it. These are controlled Qt platform input checks;
physical tablet drivers and native OS activation policy remain unverified.

## Local production validation

The serial Qt 6.8.3/SDK 15.4 production App/Core/export-verifier build passes. Fresh
Core validation passes **215 checks, zero failed/skipped**, in 25.80 s. A later
pressure-QA-only rebuild preserves the Core library, Core test binary, export
verifier and brush-smoke object byte for byte; those numeric checks are retained.

Four actual Cocoa CPU/Metal cases at measured DPR 1/2 pass the strict production
canvas wrapper. Each checks 14 input/focus/source guards, three additional
100%/rotated-crop/ordinary-crop navigation records and two visible bilingual
captions. Expected centers are independently recomputed within 1e-9, including
the 846×1653 cropped/oriented grid. Nine original captures and six additional
captures are retained per case. English CPU 1x and Chinese Metal 2x layouts were
visually inspected. All original 24 brush flags, seven capacity-refusal cases,
four recovery cases and full 2,659,021-pixel scopes remain mandatory.

The original DPR 2 pressure PNGs are 3080×1440 and canvas PNGs 2360×1440.
Requested nominal window height is not a claim about native raster height.
Checks retain exact declared pixels/DPR, the expected width and the product's
720-logical-pixel minimum height. An initial independent auditor incorrectly
required the requested 920 height; its failed script/log are preserved at
`evidence/brush-pan-J-root-audit-v2-original-failed/receipt.json`. Only that
auditor assumption changes; passing native workflows and their PNGs are reused
without replay, resizing or retagging.

| Root owned canvas GUI, ms | CPU 1x | Metal 1x | CPU 2x | Metal 2x |
| --- | ---: | ---: | ---: | ---: |
| Complete workflow | 20433 | 20387 | 22099 | 21673 |

The complete fresh matrix of 16 workflows also passes eight opaque/transparent
pressure-photo cases and four genuine Sony CPU/Metal DPR 1/2 cases. Owned pressure
retains 80 radius-key cases, 48 held-owner guards, 24 radius captions and 48 scope
language records. Sony retains full **32,947,264 pixels**, 11 controls-only crops
per case and a separate fresh-source exact PNG16/ICC comparison.

| Root genuine Sony | CPU 1x | Metal 1x | CPU 2x | Metal 2x |
| --- | ---: | ---: | ---: | ---: |
| GUI, ms | 9843 | 9660 | 10164 | 10036 |
| Combined export/reference, ms | 36975.976 | 35350.006 | 34025.271 | 32957.215 |

All four Sony PNGs retain SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.
Peak request reservation is 1,742,914,816 bytes under 3 GiB;
this is not RSS. Maximum live/committed display difference is
1/255 under the original 2/255 bound.
The 45,000 ms owned GUI, 60,000 ms Sony GUI, 120,000 ms combined export/reference,
32 Mi source pixels and shared 3 GiB limits are unchanged. RAW decoding, color kernels,
brush sampling and document persistence are unchanged.

The independent local audit exits 0 and preserves 240 original captures, complete
reports, tested binaries and all 210 code-source fingerprints at
`evidence/brush-pan-J-root-local-v2/receipt.json`. Reports retain their original
`6f2ac31` build stamps; publication uses exact source fingerprints, without
retagging. This is bounded local UI evidence; its own committed three-platform
and both actual 23-mode package acceptance is pending.

## Preserved failures and separate boundaries

The first Root matrix passes the four canvas cases, then owned CPU 1x pressure
times out at phase 0 in 45,266 ms under the original 45,000 ms gate. Its original
report, failure capture, private output, source and binary remain at
`evidence/brush-pan-J-root-v1-original-failed/receipt.json`. The original report
did not record the window-active value. The pressure helper previously waited
for `requestActivate()`/`isActive()`. It now explicitly supplies Qt platform focus
for the controlled key/tablet test and records actual initial state, bootstrap
count and readiness, with the original deadline and key predicates retained.
All eight revised owned/transparent cases observe initial Qt window-active=false,
one controlled focus bootstrap and actual readiness=true.
This does not verify or fix native OS activation policy. The original failure
stays failed; the complete revised matrix supplies separate passing evidence.

A separate actual-Main/real-PhotoController probe uses an owned 131073×2 PNG to
exercise ordinary navigation beyond the brush dimension cap. Before the
metadata fallback, the actual pointer is owned but the viewport/RGB stay fixed.
Afterward, CPU/Metal DPR 1/2 match independent center arithmetic and visible RGB
changes while the source and document stay unchanged and brush input remains
unavailable. Its original unknown stamps, failed controls, source/binary and
original pixels stay at `evidence/wide-pan-boundary-v3-verified/receipt.json`.
This owned fixture is separate from genuine Sony/RAW validation.

The previous documentation-only [CI 301 / 40734277346](https://github.com/snailjayxxx/JixelLight/actions/runs/40734277346)
passes all 68 native tests on all three platforms and the Windows actual 23-mode
package. The initial Mac job cannot acquire a hosted arm64 runner; its single
retry 123017277664 passes native validation, then is cancelled during packaging.
GitHub explicitly reports the aggregate 1h15m limit; a complete Mac 23 manifest is
absent. That run remains unaccepted. Five original ZIPs and three complete
source-proven runtime logs are retained at
`evidence/ci-40734277346/terminal-failed-v1/receipt.json`. Only the macOS aggregate
job budget changes 75→90 minutes. All individual assertions/deadlines remain.

The latest fully accepted implementation remains
[6bdf94e /CI40721028761](https://github.com/snailjayxxx/JixelLight/actions/runs/40721028761)
until this slice completes its own CI. The original I Windows 60,371 ms failure,
unexplained I.1 local phase 12 Erase failure and earlier private pan failures stay
preserved. This work does not claim native GPU brushes, native FP32 RAW, physical
tablet validation or completion of all F4/F0–F6. PR16 stays Draft.
