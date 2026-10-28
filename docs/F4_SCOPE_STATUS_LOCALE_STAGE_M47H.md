# F4 M4.7H: refresh completed histogram status after a language switch

Changing the interface language could leave the completed histogram status in its
previous language while the surrounding controls translated. The controller kept
a localized producer label. The status getter now translates the six known
producer labels when read, preserves unknown diagnostic labels and retains the
existing error/updating/crop messages. QML already tracks the language property.
Histogram data, processing plans, render keys, requests, pixels, edit state and
history do not change.

## Actual photo UI regression

The original PhotoController.cpp is byte-identical to published G. A consistent
private application reproduces three failures: CPU preview, GPU preview and CPU
full statistics retain the old language. The actual UI label and controller
getter agree on the wrong text while all histogram arrays/count/revision/intent
and history stay unchanged. Original failures, native reports, captures, source
and binary are retained under evidence/private-scope-locale-original-v1/.

The first private repair passes8 actual CPU/Metal,1x/2x,opaque/alpha42000 photo
cases and48 label checks. A separate candidate then starts from an exact archive
of published G and changes only PhotoController.cpp, BrushPressureSmokeRun.cpp
and ValidateBrushPressure.py. Its own consistent serial App build and8 actual
production-entry-point cases also pass, with136 verified captures. Native reports
retain their original unknown source stamp. These are private candidate results;
Root production integration and committed acceptance are still pending.

Each run checks EN/ZH/EN in both preview and completed full-statistics views.
The actual scopeStatusLabel text is compared with an independent Python language
oracle. All four histogram arrays, source-pixel count, render revision, Develop
intent and history cursor remain identical during each language switch. Existing
pressure Paint/Erase, barrel/cancel ownership, one Undo/Redo, project reopen and
exact PNG16/ICC checks remain mandatory. The owned helper retains its eleven
existing captures and adds six locale poses, for seventeen; Sony retains eleven.
All forty-five/sixty/120-second gates and source32Mi/shared3GiB remain.

An additional private regression uses the pinned genuine Sony ARW with the existing
mouse brush workflow. CPU/Metal at1x/2x pass14902/12156/19227/17068ms, all original
60000ms/full32,947,264 histogram-and-plot/source/shared3GiB predicates and six
controls-only crops per case. Full photographs remain outside the report archive.
This legacy flow does not substitute for the newer11-crop pressure-Sony export
and combined120s checks required in Root and committed CI.

## Root integration and local acceptance

The production change starts from accepted G.1 at39bb07e. Root changes exactly
PhotoController.cpp, the owned pressure GUI helper and its validation wrapper;
there is no private source-only bypass. A serial compiled App/verifier build
passes and all12 production-wrapper CPU/Metal actual1x/2x opaque/transparent/Sony
flows pass. The eight owned cases perform48 actual UI label checks and keep all
histogram arrays/count/revision/intent/history stable during EN/ZH/EN changes.
Each owned case has17 captures; all four Sony cases retain11 controls-only crops.

The driver first measures the actual application-window DPR with a neutral owned
CPU run. Its successful actual2x result is retained once; the remaining cases
adjust the environment factor and assert measured1x/2x. No display preference or
passed case is changed/restarted. Source210 and both binary hashes stay frozen.
Original local reports retain the parent39bb07e build stamp and will be linked
to publication through exact source hashes rather than being retagged.

| Genuine Sony H local validation | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Complete GUI, ms | 10892 | 10913 | 12060 | 12548 |
| Combined export/exit/reference, ms | 33460.317 | 33356.452 | 33618.227 | 33799.345 |

Every Sony output still contains32,947,264 pixels and the original PNG SHA256
182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf, with exact
16-bit pixels/ICC verified by a separate fresh-source process. The native Sony
reports stay partial; the verified aggregate passes. The45/60/120s gates,
source32Mi, shared3GiB and reservation refunds remain. Maximum live/commit
RGB difference is1/255, within the original2/255 limit. Original ARW and daily
preferences stay byte-identical; reservation counters do not measure RSS.

Two additional actual2x CPU/Metal compact Erase workflows pass all twelve controls
captures and existing native language/scroll/reentry/old pressure/RGB/Undo/reopen
predicates. Their complete results remain separate from the new17/11-capture
pressure matrix. An independent audit rechecks all14 reports and existing numeric,
input, source, image/capture and export predicates. Physical evidence is archived
at evidence/scope-locale-H-root-local-v1/receipt.json. Unchanged heavy numerical tests and
already passed private matrices are not repeated locally for this getter/QA-only
change; its own complete68-test CI remains mandatory.

Earlier private native evidence remains private and keeps its unknown stamp. The
first G.1-based private nominal1x run actually measured2x: its native behavior
passed but the original driver assertion failed; that failed requested1x result
is preserved. A separate calibrated private8 then passed48 checks/136 captures.
Neither is relabeled as Root acceptance or as a published wrapper pass.

## Committed acceptance remains pending

The underlying [G.1 / CI40675071304](https://github.com/snailjayxxx/JixelLight/actions/runs/40675071304)
is strictly accepted: all68/all3, both actual23-mode SDK-stripped packages and
all five original ZIPs/three full original job logs prove actual99c8fb0/treea59f0b0
equal39bb07e. Its immutable terminal receipt is in evidence/ci-40675071304/.
The original G60082ms Windows failure remains failed.

H requires its own unchanged68-test suites on all3 platforms and both actual
23-mode SDK-stripped packages, including the new17-capture owned locale checks,
unchanged11-capture genuine Sony, precision/alpha/full-scope/budget/refund gates,
original artifacts and actual job-checkout source proofs. Physical tablet,
native GPU brush, native FP32 RAW and remaining F0–F6 are unfinished. PR16 stays
Draft; main and PR15 are unchanged. No owner operation is required.
