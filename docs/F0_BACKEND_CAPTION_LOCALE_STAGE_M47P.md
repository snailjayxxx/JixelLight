# F0 M4.7P: refresh the visible backend caption after a language change

Choosing English in the header could leave the photo canvas's CPU caption as
“CPU 并行”. The getter already returned “CPU parallel”, but the QML binding only
depended on `processingBackend`/`backendChanged`. The language setter emits its
separate language notification, so the visible caption did not reevaluate.

The caption now explicitly depends on `controller.language` and has a stable
object name for inspection. Language changes refresh its text without emitting
a new backend notification or requesting GPU processing. Its placement, size
and existing backend getter remain. Only `ui/PhotoCanvas.qml`, the existing
`app/ShortcutHelpSmokeRun.cpp` and `tools/ValidateShortcutHelp.py` change code;
the other210 code sources are unchanged. Controller, processing, RAW, colour,
brush arithmetic and export kernels are unchanged.

## Current-component baseline and candidate

The private comparison reuses accepted M47O/644942a's actual main/Core/all other
compiled UI, including the fixed460 copy dialog. Its actual cached build stamp
is d2b123f. The original visible UI reproduces the mismatch at1,583ms: English
header, English getter, Chinese visible CPU caption. Source, pixels, full98,304
statistics/plot, revision and history remain stable. The original failed JSON,
log and three captures stay failed.

With the same private driver, the caption binding passes CPU DPR1/2 in
1,165/1,670ms and actual Metal DPR1/2 in1,228/1,834ms. Each checks initial Chinese,
pointer-selected English and pointer-selected Chinese with unchanged image,
statistics and history. The proposed original14-context guide CPU DPR1/2 passes
3,010/3,484ms, adding13 actual caption contexts per guide. All40 positive UI
records,26 guide caption contexts and25 original PNGs including the failed
baseline are at `evidence/backend-caption-on-M47O-private-v5/receipt.json`
(304files). The actual CPU DPR2 English caption was visually inspected. Root
sources, link inputs, original binaries and daily preferences stay unchanged
through that comparison. These owned PNGs are UI fixtures.

## Existing guide checks are extended

All14 original guide contexts, five captures, modal/focus/held-input/source/history
predicates, CLI routing and the original60,000ms deadline remain. Each of the13
image-bearing Develop contexts now records the actual label, getter, language,
visibility and current-caption result. The empty Library context remains
inapplicable. A new eighth wrapper check independently requires literal
`CPU parallel`/`CPU 并行` for the selected language; two equally wrong strings
cannot satisfy it.

The future CI helper adds only these caption requirements. Removing them projects
byte-for-byte to CI310's original seven-check helper. Twenty-four derived temporary
negative controls reject stale English labels, a matching wrong getter, missing
context, invalid language/count/applicability/current state and the original
source/modal-key/PNG/deadline failures. All original JSON and PNGs remain intact.
Unbound full auditors/finalizers refuse use. The template at
`backend-caption-locale-ci-template-v4/receipt.json` is for the next publication;
CI310's helper and reports remain immutable. No new package mode is added.

## Normal Root validation

The serial normal App/CoreTests/ScopeTests build passes. CMake advances the actual
compiled base stamp from d2b123f to644942a and rebuilds Core, so fresh
Core215/0/0 runs in20.9544s with all five scope suites,
including scheduling12/0/0 and the checksum-pinned genuine Sony native scope case.
The selected six suites pass with zero failures or skips.

The normal production App passes the original public Leica DNG full-App smoke:

| Flow | Elapsed ms | Final statistics pixels | Original captures |
| --- | ---: | ---: | ---: |
| CPU DPR1 | 13,454 | 10,340,960 | 23 |
| Metal DPR1 | 14,505 | 10,340,960 | 23 |
| CPU DPR2 | 18,291 | 10,340,960 | 23 |
| Metal DPR2 | 19,320 | 10,340,960 | 23 |

All original UI/statistics/GPU predicates and60,000ms remain. Additional private
QA reuses the normal Root main/Core/all compiled UI and exercises actual header
locale pointers while preserving the owned source, exact statistics and history:

| Flow | Elapsed ms | Guide/caption records | Captures |
| --- | ---: | ---: | ---: |
| guide-cpu1x | 2,956 | 14 | 5 |
| guide-cpu2x | 3,440 | 14 | 5 |
| caption-cpu1x | 1,303 | 3 | 3 |
| caption-cpu2x | 1,554 | 3 | 3 |
| caption-gpu1x | 1,275 | 3 | 3 |
| caption-gpu2x | 1,815 | 3 | 3 |

The two normal guides retain all14 contexts, five captures and the old seven
checks, plus13 actual caption checks each. Four CLI guards pass. Eight derived
Root-wrapper caption negatives and24 future-CI negatives are rejected.
Independent audit0 archives ten actual App GUI flows,40 guide/caption records,
26 guide caption contexts,114 original PNGs, commands, binaries and source213 at
`evidence/F0-M47P-root-local-v1/receipt.json` (402files). Actual normal Root CPU
DPR2 English caption and English compact help captures were visually inspected.
Daily preferences remain unchanged. Raw local644942a stamps are retained;
publication requires exact213 committed-source fingerprint matching.

## Accepted baseline and remaining acceptance

M47O/644942a is fully accepted by
[CI310](https://github.com/snailjayxxx/JixelLight/actions/runs/40845703559):
all68/all3 zero-fail/skip, both actual24-mode SDK-stripped packages, five original
validation ZIPs, three full actual469329f/tree4bbd2ad logs, source213 and strict
finalizer0. `evidence/ci-40845703559/terminal-accepted-v2/receipt.json` has2,415files.
Its original seven-check guide did not check this newly reproduced caption
mismatch. That acceptance remains historical under its actual predicates.
Original CI309/308/307/306/305 and private baseline failures remain preserved.

This M47P caption publication still requires its own all68/all3, both actual24
packages, five original ZIPs, three complete runtime source logs, exact213 source
proof and strict finalizer0 with the additive eighth check. Local passes do not
establish current committed cross-platform/package acceptance.

Original45s owned GUI,60s Sony/help/basic GUI,120s export-reference partitions,
32Mi source pixels, shared3GiB and colour/precision gates remain. Reservation is
not RSS. Owned caption images and Leica RAW do not replace public genuine Sony
material. Physical input/native OS activation, native GPU brush kernels, native
FP32 RAW and remaining F0–F6 work remain unfinished. PR16 stays Draft.
