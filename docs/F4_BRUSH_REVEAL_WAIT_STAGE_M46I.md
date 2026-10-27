# F4 M4.6I — observe bounded brush reveal completion in native checks

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Candidate based on H `21cd3d87029fadca196a12fd3684976ca87ea464`. Physical build and four fresh actual native owned checks pass; own committed CI is pending. Last fully accepted remains E24ac54d.

## Observed packaged failure

H [CI40440387028](https://github.com/snailjayxxx/JixelLight/actions/runs/40440387028) passes all44/44 on all three and the actual Windows11-mode package. macOS attempts11 package modes;10 pass. Owned Metal stops at phase31/7,684ms: controls are visible (headerY0/EVY129/viewport162), while pending=true/owner=true/attempts2. The retained failure PNG confirms visibility. The native check assumed three smoke timer ticks were sufficient for the independent QML timer to finish. All original artifacts/logs and the failed result remain in the [H runbook](F4_BRUSH_LOCALE_REVEAL_STAGE_M46H.md); H is not retroactively accepted.

## Narrow change

Only `startBrushCanvasSmokeRun` changes. Automatic re-entry phases31/33 and the automatic English reflow phase19 observe pending=false before running the same actual visibility, document, history and ROI assertions. Manual wheel/cancel/locale checks remain immediate guards. The outer45,000ms deadline and production16ms/60-attempt request bound remain unchanged; a stuck request still fails within the original deadline. No larger fixed sleep or accepted visibility criterion is introduced.

`reveal_wait_ticks` records native observations made while an automatic request is still pending. Every old23 required flags/eight captures and both locale invariants remain required. Production QML, controller, model, renderer, Sony/legacy/shared helpers, headers, CTest and CI configuration are unchanged.

## Actual local verification

The physical Qt6.8.3 app build passes. Parallelism1 was selected because this8GB host had about80MiB free and substantial compressed/wired memory; no other task was interrupted. Existing SDK-version linker warnings remain.

| Fresh actual owned native, CPU / Metal ms | Normal | Actual2× |
| --- | --- | --- |
| All23/eight plus locale invariants | 9,693 / 9,189 | 10,072 / 11,616 |

All four retain full2,659,021 statistics, actual painted pixels/selected-stroke Delete/Undo/reopen, native wheel/re-entry/manual-scroll/locale behavior, headerY0/EVY129/viewport240/pending=false/owner=true, QML0 and the original45,000ms. The actual2× Metal English layers/scopes capture was inspected. Daily native preference SHA is unchanged.

All four local pending-observation counters are0: this run did not reproduce the delayed-completion timing. These normal-path passes are not claimed to exercise a pending wait or close the packaged CI failure; new committed package acceptance remains required.

## Evidence and scope of reuse

Physical build `/Volumes/MacNAS-Dev/JixelLight-Build/build-local/local-brush-reveal-wait-smokes/` retains new JSON/log/wrapper/eight captures per mode. Parent logs: `brush-reveal-wait-build.log`, `brush-reveal-wait-validation.log`. Complete local summary: parent `brush-reveal-wait-local-summary.json` and `/tmp/jixellight-brush-reveal-wait-local-summary.json`, passed=true/accepted=false. Raw new reports retain precommit base21cd3d8; they are not new committed-I CI.

App source SHA-256 is `af8305c0ab749341b73f6410452d1926400c6bcc92914d87e9328b3980ff300b`. The validation script proves byte equality to H before the owned scenario and from the Sony scenario to end of file. QML/wrapper hashes also remain identical to H. The unchanged H local Sony normal/2×, legacy RAW, diagnostic/mask/relink evidence is explicitly reused for those unchanged paths, with its original3428eec precommit-H stamp and committed-H source-hash proof retained. It is not relabelled as a new I run or a timing improvement. No duplicate heavy local44 or Sony/legacy matrix is started for this owned-harness-only change.

## Remaining acceptance

Own committed I still requires44/44 without skips on Linux/Windows/macOS, all native owned/realSony/RAW criteria, both complete11-mode SDK-stripped packages, full actual merge/tree proof and original artifact hashes. Source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi/GPU40/65535, real RAW, owned45,000ms/RAW-Sony60,000ms and120s performance shards remain. Full Sony photographs stay private. No native GPU brush, native FP32 RAW, eraser/pressure/live-drag color or F0–F6 completion is claimed. Capacity feedback remains deferred until this gate closes.
