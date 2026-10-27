# F4 M4.6I — observe bounded brush reveal completion in native checks

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Accepted at `8ed006a1e5910f267dd6e40d4a240c5c1ba22f05` by [CI40445440071](https://github.com/snailjayxxx/JixelLight/actions/runs/40445440071): all three44/44 suites and both actual11-mode packages pass with complete source/artifact proof. This supersedes E24ac54d as the last fully accepted checkpoint for the guarded brush workflow.

## Committed acceptance

All three CI jobs finished successfully: Linux653.80s, Windows934.91s, macOS960.72s for44/44 CTest each, zero skips. Each retains212 Core/36 GPU/39 performance/21 Sony Look/17 admission and all older routing/export/statistics checks. All27 admitted GPU cases have maximum error0/65535. Full job logs show actual merge `14f89f46d1b14b4fde5e5f48f5c82a981f9b04a5`, tree `27bec4518c1f1853098001e63c6a981aaac3bd2c`, equal to accepted HEAD.

| Standalone native CPU / GPU ms | Owned23/eight plus locale | RealSony16/six |
| --- | --- | --- |
| Linux | 9,377 / 10,663 | 20,068 / 22,057 |
| Windows | 22,609 / 13,718 | 42,900 / 36,537 |
| macOS | 17,186 / 17,877 | 23,827 / 24,083 |


Both actual SDK-stripped11-mode Windows/macOS packages pass. Every owned case retains the23/eight and locale criteria/full2,659,021/QML0/45,000ms; realSony retains16/six/native32,947,264 histogram+waveform/sourceSHA/shared3GiB/60,000ms. Full Sony photographs stay private. Original RAW in both packages retains23 screenshots/full10,340,960/all UI and selected-export checks/QML0/original60,000ms.

| Actual package CPU / GPU ms | Original RAW | Owned brush | RealSony brush |
| --- | --- | --- | --- |
| Windows | 50,838 / 32,848 | 22,804 / 13,733 | 43,366 / 36,690 |
| macOS | 32,653 / 31,938 | 16,773 / 16,243 | 22,671 / 22,431 |


**Packaged macOS Metal records one actual pending observation**, then passes in16,243ms with headerY0/EVY129, owner=true, pending=false. This exercises a pending-completion wait in the affected packaged backend under the original45s gate. Local/Linux/Windows counters remain0; no delayed-path coverage is attributed to those runs. The older H phase31 failure stays failed and retained.

Five original ZIPs have verified sizes/SHA-256/CRC/safe extraction; full976,937-character Linux/927,160-character Windows/914,928-character macOS logs retain original line endings. Originals and accepted summary are under `/tmp/jixellight-40445440071-*`; physical build parent retains `ci-40445440071-summary.json`. Actual English package layers and Chinese Sony controls/waveform captures were inspected.

| Original artifact | ID | Bytes | SHA-256 |
| --- | --- | --- | --- |
| validation-macOS | 12710065426 | 31,588,470 | `7607363b9d228857f7a0f1722e481e485b71cacbfaa95afb021c6921e7048ce0` |
| package-validation-macOS | 12709468344 | 21,327,366 | `10399bd0c6659e2fc5144331ec3f04f422bf826621186748f1ab7d4c302c51ef` |
| package-validation-Windows | 12709462327 | 15,540,464 | `e4e17633b15eeecca4236676f099715ce4375a9fd38136563f387d9f2165f2d5` |
| validation-Windows | 12709312115 | 28,726,499 | `d6bd4597b65c1331ce012fea8d968eb35e4424cc7aa54104561ed1f09ff44049` |
| validation-Linux | 12708907463 | 32,158,564 | `0f3227181f6cc052aeb50480f98c5e929a9eb475d1e209584f4682b1294efa3a` |

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

## Boundaries and next work

The committed I acceptance above is complete; no failed H/G/F run is retroactively accepted. Source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi/GPU40/65535, real RAW, owned45,000ms/RAW-Sony60,000ms and120s performance shards remain. Full Sony photographs stay private. No native GPU brush, native FP32 RAW, eraser/pressure/live-drag color or F0–F6 completion is claimed. Next scoped work is capacity feedback from the shared layer/stroke/dab limits, with its own native/controller evidence before acceptance.
