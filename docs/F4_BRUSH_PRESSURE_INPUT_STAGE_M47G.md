# F4 M4.7G: tablet pressure and retained stroke ownership

The brush canvas now routes Qt pen/eraser pressure into the existing brush sampler.
Initial pressure is retained, including exact zero; a device without pressure
capability and the existing mouse path use pressure one. The existing sampler
continues to apply pressure to radius and opacity. The cursor follows that radius.
The serialized brush document, render arithmetic, RAW decoding and shaders do not
change.

A gesture retains one input device until its primary tip lifts. Releasing a barrel
button while the tip remains down does not finish the stroke. Esc, deactivation,
visibility/enable changes and invalid input cancel the transient state; subsequent
barrel edges or duplicate presses from the held tip cannot restart it. Foreign
input cannot take over the active gesture. The next fresh gesture works after lift.

## Controlled input and actual photo UI evidence

The separate Qt input fixture checks 21 independently specified packet traces at
actual measured 1x and 2x, for 42 rows. It covers exact-zero pressure, missing
pressure capability, mouse compatibility, clipped input, foreign input, cancellation,
owner destruction, duplicate presses and barrel-button transitions. These fixture
results are separate from actual photo-application UI validation and physical
hardware validation.

A controller regression rejects seven non-finite/out-of-range initial pressures
without history changes, retains exact zero, rejects a second start without losing
the current stroke, compares the sampler output, verifies one Undo/Redo and cancels
an invalid append without a commit. Together the two new CTest shards bring the
suite from 66 to 68.

The actual compiled application passes twelve controlled pen runs: CPU/Metal,
1x/2x, owned opaque/transparent RGBA64 PNG and pinned public Sony ARW. Each checks
Paint and Erase, eight barrel-button transitions, cancellation while the tip stays
down, subsequent fresh input, unchanged held intent/history/revision, single Undo,
Redo, project reopen, live/committed RGB and eleven original captures. Owned PNG
exports compare exact 16-bit pixels and ICC. Transparent source alpha is 42000/65535.
No QML errors are accepted.

| Genuine Sony | CPU 1x | Metal 1x | CPU 2x | Metal 2x |
| --- | ---: | ---: | ---: | ---: |
| Complete GUI validation, ms | 13978 | 12341 | 13538 | 13144 |
| Combined export/exit/independent reference wall time, ms | 45464.136 | 51867.523 | 49573.377 | 51950.848 |

The native GUI deadline remains 60000ms. Export and a fresh-process reference
comparison share one 120000ms slice after the measured GUI work; there is no second
120-second allowance. The independent verifier reloads the original source,
compiles the frozen pre-effective edit state, verifies the exact 816-byte compiled
plan hash and compares every output pixel and its ICC. All four local exports
contain 32,947,264 pixels and share PNG SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.
The 32Mi source limit, shared 3GiB request budget and end-of-operation refunds
remain. Reservation counters do not measure RSS.

Original Sony native reports intentionally retain `passed=false` and
`external_pixel_verification_pending=true` after the GUI/export-writing stage.
Only a separate aggregate passes after the fresh reference process succeeds.
Neither the original report nor its source stamp is rewritten. Full Sony images
and PNG exports stay outside the public report/package tree; only controls-only
crops, logs, metadata and frozen edit state are retained there. Successful private
scratch exports are removed; failed scratch outputs are preserved. The normal PNG
reader allocation limit remains 256MiB.

The macOS validation activity covers only this controlled Sony smoke through normal
process teardown, allows idle system sleep and does not change user preferences.
It is not evidence that App Nap caused the earlier private timeouts.

## Regressions and retained failures

All twenty-two affected older photo workflows pass: owned brush and genuine Sony
mouse painting, opaque/transparent/Sony Paint/Erase at actual1x/2x CPU/Metal,
and the original RAW CPU/Metal GUI with all23 screenshots and10,340,960 scope
pixels. Fifteen copied-Sony exact/symlink/hardlink report, guarded-cancel capture,
export, frozen-state, checkpoint and failure-capture aliases refuse before writing;
the copied source, pinned originals and daily preferences stay unchanged.

The original serial full run finishes67/68 in941.03s. Only the new input fixture
fails: inherited Qt offscreen produces no mouse fallback for clipped input.
Cocoa's original42-row fixture had passed. The repaired helper explicitly uses
the actual Cocoa/Windows/XCB platform and retains every expected trace; its
isolated Cocoa rerun passes42/42 in21.12s (CTest1/1). It also writes a fresh
pending pointer before starting, so an older passing result cannot remain as
apparent evidence after a later failure. The original failed run, stale primary
pointer and partial traces remain archived and are not retagged as a full68 pass.

All67 unchanged tests retain their own original passing reports: Core215,
display-ICC4, schedulingQt6, numerical/export/ICC/full-scope/refund gates and
18 analytic alpha draws. A strict read-only audit reconciles these original67
with the repaired input shard; the67 heavy tests are not repeated. The compiled
App, input probe, core/QML and these67 test sources remain unchanged.

The export verifier additionally uses QCoreApplication Unicode arguments. The final
twelve native-helper regressions pass through Chinese, spaces and supplementary
Unicode characters in private report/export paths; the Sony table above records
this final matrix. Maximum live-to-commit difference is1/255 against the unchanged
2/255 limit; maximum native request reservation is1,742,914,816 bytes. The initial
twelve ASCII-path passes remain separate originals. Their App and input-probe
binaries are byte-identical to the final matrix; only the input-platform helper,
Unicode validation path and verifier argument decoder changed after the original
full run. None of those changes affects the other67 test inputs. A strict audit
checks all64 Qt reports, the real-file/CLI results, corrected42 input rows, final12
photo runs, older22 UI flows,15 aliases and source/preference hashes.

New controlled captures, original primary/targeted reports, binaries and all
nineteen final tested non-documentation source hashes are archived under the
physical build parent's evidence/pressure-input-m47g-local/. Original failed
full-run files are in full68-original-failed/receipt.json. Final evidence retains
separate native-v1/, unicode-native-v2/, legacy-v1/, alias-v1/ and test-reports/
groups plus the publication source mapping. The original failed941.03s run is
never represented as a successful single full68 run. The new commit's own
three-platform68/68 is still required below.


Before the routing repair, an actual controlled-photo run fails when a barrel
release prematurely commits a held stroke. Independent old-router tests also
reproduce cancelled-tip duplicate restart and side-button release failures. These
original failures, earlier Sony timeout/combined-deadline failures and partial
reports remain archived. A later passing reference calculation does not change
an original timeout into a pass.

Original local reports retain the `e8062ca` build stamp from the pre-publication
build. Exact hashes of every tested non-documentation change link these reports
to the eventual publication; they are not retagged. The base documentation
checkpoint [e8062ca / CI40575236982](https://github.com/snailjayxxx/JixelLight/actions/runs/40575236982)
is independently accepted: all three 66-test suites, both actual 17-mode
SDK-stripped packages, five original ZIPs and three full original job logs. Their
actual checkout e5d1564757607a5dd20eb084e370e739d666d2a5 has tree
a5bcecf45c10fcb17b28658391414e075609b578 equal to that documentation head.
Original D/E failures remain failed; the accepted F implementation remains
[5178b83 / CI40564889527](https://github.com/snailjayxxx/JixelLight/actions/runs/40564889527).

## Original committed G run failed

[e241cd4 / CI40593963118](https://github.com/snailjayxxx/JixelLight/actions/runs/40593963118) finishes all three standalone68-test suites without failures/skips: Linux1105.10s, Windows1783.93s and macOS1384.08s. Linux/macOS native controlled pressure and the actual23-mode SDK-stripped macOS package pass. GenuineSony CPU/GPU GUI ms are17416/19995 on Linux and20496/20718 on macOS; combined export/reference ms are63750.013/64040.022 and51638.011/48354.126, respectively.

Windows passes all older mouse/Erase workflows and four owned/transparent pressure modes, then fails genuineSony CPU pressure atphase18:60082ms exceeds the original60000ms. Paint/Erase, held ownership, oneUndo, Undo/Redo and reopen have passed; histogram count is32,947,264 but the full waveform has not published (plot pixels0). SonyGPU pressure, remaining RAW/stage validation and Windows package23 do not run. This original native report stays failed. The latest fully accepted checkpoint remains e8062ca/F5178b83.

All four available originalZIPs and three full original job logs are SHA256/CRC/source-audited and physically archived at evidence/ci-40593963118/terminal-failed-v1/receipt.json. Each actual job checks out37e3bbaf4bd4ae1252884c0922eff25a0c98a10a/tree5f723e56305371e69b573a1f6b67ba1f2615c85e equalG. No Windows package was produced.

## G.1: save the inspected UI frame

The pressure helpers previously grabbed the window once to inspect RGB, then immediately grabbed it again for the paired evidence image. They now save the exact actual UI frame whose RGB passed, as the existing mouse Erase helper already does. This removes seven redundant grabs per completed flow: baseline, four held Paint/Erase poses and two committed poses. Cancellation-owner packets, project/tool reentry, final scope/export and failure captures still take fresh frames after intervening input. All eleven captures and existing pressure/held RGB/ownership/Undo/persistence/export assertions remain. Grab call/total/max timing diagnostics are recorded without changing the45/60/120s gates.

This changes only the two controlled-pressure QA helpers; controller rendering, scope scheduling, input routing, RAW decoding, brush arithmetic, shaders, serialization and numerical limits are unchanged. A local result cannot establish a Windows deadline repair. The follow-up commit requires its own68/all3 and both actual23-mode package validation.

The final compiled App and the unchanged production wrapper pass all twelve affected
CPU/Metal, actual1x/2x, opaque/transparent/pinnedSony flows. One original SonyCPU1x
pass is retained from before the interruption; the remaining eleven run after
Gen8 reconnects. All210 non-documentation source hashes and both binary hashes
match before resumption. No completed case is restarted. Original preflight
fresh-report refusal and interrupted partial evidence remain separate.

| Genuine Sony capture follow-up | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Complete GUI, ms | 15464 | 11094 | 11456 | 11476 |
| Combined export/exit/independent reference, ms | 49237.704 | 34761.526 | 33728.245 | 45692.051 |

Every case retains all11 captures and the existing pressure, held/cancelled owner,
RGB, oneUndo/Redo, project reopen and exact export checks. The four Sony outputs
still share the original182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf
PNG SHA256. Maximum live-to-commit difference is1/255 against the unchanged2/255
limit; peak request reservation1,742,914,816 bytes stays below3GiB. Source ARW and
daily preferences remain byte-identical. The new timing counters record27 actual
window grabs per completed flow; these macOS measurements do not establish
Windows grab latency or prove its deadline repair.

An independent audit rechecks all existing native/export predicates, captures,
source210/binary pins, full Sony counts and source/preference hashes. All217
original reports/crops/scripts/source/binary records are physically archived at
evidence/pressure-frame-reuse-local-v1/receipt.json, including original-partial-v2/
and resumed-v3/ as distinct groups. Local reports retain the e241cd4 build stamp;
publication links them through exact source hashes without retagging reports.
Own committed three-platform68/68, both actual23-mode packages and original
job-checkout/artifact receipts remain required. The original G CI stays failed.



## Follow-up committed acceptance remains pending

CI retains every earlier gate and adds the 42-row input fixture, controller
pressure regression and six actual controlled-pressure photo workflows. Windows
and macOS packages deploy the export verifier and must pass all 23 modes after
SDK paths are stripped, including the original 17. Three-platform 68/68, actual
23-mode packages and original artifact/job-checkout receipts are required before
this new committed stage can be accepted.

These tests inject controlled Qt-QPA stylus packets into the actual application.
They do not verify a physical tablet driver, native GPU brush computation, native
FP32 RAW decoding or completion of F4/F0-F6. Those items remain unfinished. PR16
stays Draft; main and PR15 are unchanged. No owner operation is currently required.
