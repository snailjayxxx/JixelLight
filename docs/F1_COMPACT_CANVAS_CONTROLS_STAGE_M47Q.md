# F1 M4.7Q: keep controls inside a narrow comparison canvas

At the minimum 1180 × 720 window size, enabling the camera/reference view and
widening the sidebars can leave a 176-pixel-wide editing canvas. The old fixed
rows put the 200% button and part of the Oval handles button outside its clipped
hit-test area. The actual App baseline reproduces this in 2,089ms; its failed
report and original captures remain preserved.

Zoom, mask and brush groups now wrap within the available canvas width. The
tool hint also wraps, and crop controls wrap while retaining their centered
placement when the full group fits. Button labels, actions, keyboard behavior,
brush arithmetic, processing, RAW, colour and export kernels remain unchanged.
Only PhotoCanvas, ShortcutHelpSmokeRun and ValidateShortcutHelp change code;
the other 210 tracked code sources are unchanged.

## Actual App verification

The paired private comparison reuses the accepted M47P App main, Core and all
other compiled UI. CPU and Metal at actual DPR1/2 pass eight poses each, including
176-pixel English/Chinese controls, linear/radial/brush hints and crop Cancel.
All 98,304 source/statistics/plot pixels, histogram bins, source SHA and edit
history remain unchanged. The original baseline, early private QA readiness and
publication-timing failures, and scale-request versus actual-DPR reporting
failure are preserved at `evidence/canvas-compact-controls-private-v1/receipt.json`
(352 files). Those QA corrections retain the original 60,000ms limit.

The serial normal build refreshes its actual cached stamp from644942a to961fb57
and rebuilds Core. Fresh Core215/0/0 passes in22.013s; all five scope suites pass,
including scheduling12/0/0 and the checksum-pinned genuine Sony scope case.

Normal production App public Leica DNG flows retain all original UI/GPU checks,
full10,340,960 statistics, 23 captures per flow and the original60,000ms bound:

| Mode | Requested QT_SCALE_FACTOR | Elapsed ms |
| --- | ---: | ---: |
| CPU | 1 | 20,668 |
| Metal | 1 | 20,956 |
| CPU | 2 | 42,546 |
| Metal | 2 | 49,769 |

These rows name the requested scale factor; original capture dimensions are
retained rather than assuming that the request equals the actual window DPR.
Separate compact probes reuse the normal App's exact main/Core/all compiled UI
and assert actual DPR1/2:

| Mode | Actual DPR | Elapsed ms | Poses / captures |
| --- | ---: | ---: | ---: |
| CPU | 1 | 6,188 | 8 / 8 |
| CPU | 2 | 28,119 | 8 / 8 |
| Metal | 1 | 8,423 | 8 / 8 |
| Metal | 2 | 32,738 | 8 / 8 |

The normal guide passes in8,125ms at actualDPR2. Its original14 contexts, five
captures, eight checks and13 backend-caption contexts remain. Four additional
contexts and captures check narrow English brush controls, actual Oval pointer
selection with Chinese hints, narrow crop controls with actual Cancel, and the
restored wide canvas. A ninth wrapper check independently checks control bounds,
expected controls, wrapped hint dimensions, literal backend captions, pointer
results and PNG hashes/dimensions.

Four CLI guards pass. Twenty-four derived Root negatives and24 future-helper
negatives reject missing or reordered cases, false pointer results, overflow
despite a true reported flag, fake wide layouts, missing controls, matching stale
captions, bad hints/PNGs and original source/modal-key/deadline violations.
Removing only the new wrapper predicates gives a byte-identical projection of
M47P's eight-check validator; its immutable CI helper accepts the projected
original14-context evidence. Derived Windows/POSIX extraction-path checks pass.
Unbound full auditors/finalizers refuse use.

Independent audit0 preserves nine actual App GUI flows,50 guide/compact records,
133 original PNGs, source213, normal binaries and commands at
`evidence/F1-M47Q-root-local-v1/receipt.json` (420 files). The normal English
compact and crop captures were visually inspected. Daily preferences remain
unchanged. Raw local961fb57 stamps remain; publication requires exact213
committed-source fingerprint matching.

The local QSB dependency files also expose a pre-existing space-splitting issue
in the Gen8 source path, which makes Ninja request redundant shader generation.
The quoted shader commands build successfully; source fingerprints and the
actual CPU/Metal checks above remain authoritative. Its read-only diagnosis is
preserved for a separate F0 repair.

## Accepted baseline and remaining acceptance

M47P/961fb57 is fully accepted by
[CI311](https://github.com/snailjayxxx/JixelLight/actions/runs/40864413485):
all68/all3 zero failures/skips, both actual24-mode SDK-stripped packages, five
original ZIPs, three full actual29dd8be/treee97a608 logs, source213 and strict
finalizer0. Its2,426-file terminal archive remains immutable. Its guide has eight
checks; the new ninth check is only for this M47Q publication.

M47Q is now fully accepted by
[CI312](https://github.com/snailjayxxx/JixelLight/actions/runs/40882457598).
All68/all3 pass with zero failures/skips in Linux933.51s, Windows1,763.08s and
macOS1,462.40s. All three native strict audits, both actual24-mode SDK-stripped
packages and strict finalizer0 pass. Guide14/five original captures/eight old
checks/13 captions plus four compact contexts/four captures/ninthcheck passes
in3,591/12,231/9,076ms without QML errors.

Genuine Sony pressure GUI CPU/GPU is13,071/14,693ms,53,462/29,967ms and
19,941/20,229ms. Combined export/reference is57,548.081/57,471.623ms,
80,150.321/79,509.542ms and52,816.463/52,906.210ms. These retain full32,947,264
source/histogram/plot pixels, shared3GiB and exact platform PNG16/ICC. Native
partial reports remain partial; use GUI time separately from export/reference.

Five original validation ZIPs, three FULL actual3f5393f/treea830 runtime logs,
all213 matching Git/FS source hashes, original reports/captures and closed
collector/finalizer evidence are sealed at
`evidence/ci-40882457598/terminal-accepted-v1/receipt.json` (2,458 files).
The original failed clipping baseline and all earlier failed records remain.

Original45s owned GUI,60s Sony/help/basic GUI,120s export/reference,32Mi source
ceiling, shared3GiB reservation and colour/precision gates remain. Reservation
is not RSS. Owned UI PNGs and Leica RAW do not replace public genuine Sony
material. Physical input/native OS activation, native GPU brush kernels, native
FP32 RAW and remaining F0–F6 work remain unfinished. PR16 stays Draft.
