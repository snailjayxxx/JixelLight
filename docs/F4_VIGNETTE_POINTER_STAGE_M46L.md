# F4 M4.6L — verify the native vignette target before its first click

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Committed L c60ecf5 / [CI40470594391](https://github.com/snailjayxxx/JixelLight/actions/runs/40470594391) is **not accepted**: macOS fails one standalone cancellation test before native/package validation. Last fully accepted checkpoint remains I8ed006a/[CI40445440071](https://github.com/snailjayxxx/JixelLight/actions/runs/40445440071). J/K failures remain retained rather than retroactively accepted.

## Actual failure and resulting change

K [CI40462199953](https://github.com/snailjayxxx/JixelLight/actions/runs/40462199953) completes45/45 on all three and the full Linux job. Mac package attempts11 modes;10 pass. Original-RAW CPU GUI fails only `ui_vignette_passed` at33,444ms/QML0/23 successful captures/final10,340,960 scopes. Vignette amount is revealed and a click is dispatched, but amount remains0; midpoint/feather, reset/Undo and all other UI flags pass. This particular control phase uses an owned64×48 JPG within the larger real-DNG workflow. Original JSON, vignette screenshot, full logs, original ZIP and digests remain in the [K record](F4_BRUSH_CI_BUDGET_STAGE_M46K.md).

Previously `revealItem` success only established that contentY was assigned, followed by a single timer tick before clicking. It did not establish completed layout, an idle scroll container, or that the native point actually hit the intended slider. L checks that missing readiness evidence before sending the same one native click. It does not replace the click with a controller setter or retry clicks until the value changes.

The new smoke-only helper polishes the target's ancestor layouts, verifies positive visible/enabled bounds, window and ancestor clips, idle scroll flags and the actual child hit path, then observes two matching pointer poses. Phase283 continues only after those conditions hold. The same `<−0.5` amount change, all old vignette/Undo/scope/UI checks and original60,000ms deadline remain. Two extra readiness flags are required; wait/not-ready/polish counters and the actual pose are recorded. A missing/stuck target still fails within the original deadline. Production QML, controller, image model, kernels and RAW decoder remain unchanged.

K also confirms Windows45m0s whole-job exhaustion during the package CLI stage, despite9 successful GUI reports. L changes Windows's whole-job budget45→60 minutes, retaining Linux45/macOS60 and every step/individual timeout. All RAW/Sony60s, owned45s, performance120s, source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi and GPU40/65535 remain unchanged.

## Fresh actual local evidence

The physical Qt6.8.3 app build passes with SDK15.4, parallel1 on this8GB host. All four fresh actual Cocoa runs use the pinned real DNG and the complete existing GUI scenario. Each passes all UI flags,23 successful screenshots, final10,340,960 pixels, QML0 and the original60s deadline, plus the new native-target guards and amount−1.904. Daily preference bytes and original DNG SHA remain unchanged.

| Actual Cocoa RAW GUI | CPU ms | Metal ms |
| --- | --- | --- |
| Normal display | 15,825 | 18,058 |
| Actual2× display | 20,707 | 20,082 |

All four record a ready hit target inside window/clips with idle scrolling and two matching poses. Each waits once for the second pose; not-ready and polish-change counters are0. These are normal-path passes, not a local reproduction of K's intermittent missed click or evidence that a not-ready path was exercised. Actual new packaged acceptance remains required. The2× Metal vignette capture was inspected with a visible negative amount and controls.

Reports/logs/23 captures per mode are in `build-local/local-vignette-pointer/`; parent `vignette-pointer-local-summary.json` and `/tmp/jixellight-vignette-pointer-local-summary.json` retain exact tested source hashes, original precommit183d42d stamp and passed=true/accepted=false. Build/validation logs are parent `vignette-pointer-first-build.log` and `vignette-pointer-validation.log`. No report is relabelled as new committed CI.

## Scope of reuse and acceptance

Existing shared click/reveal/drag helpers and Diagnostic/Mask/Relink scenario bodies remain byte-identical to K; only the new helper and legacy vignette phase/guard change. `app/BrushSmokeRun.cpp`, QML, core and all CTest/test/wrapper source remain unchanged. The previous verified local45/brush/Sony/diagnostic/mask/relink evidence is reused with explicit byte equality and original stamps; no duplicate heavy local suite is started for this smoke-only repair. K's failed package result remains failed.

L requires fresh exact committed all-three45/45, native24/nine/7 blocked+4 ready, Sony16/six/full32,947,264/sourceSHA/3GiB, originalRAW23screens/full10,340,960/all UI/new target trace/60s, both actual SDK-stripped11-mode packages and full actual-source/tree/original-artifact proof. Eraser, pressure, continuous drag color, native GPU brush, native FP32 RAW and F0–F6 completion remain unclaimed.

## Actual committed outcome — Mac standalone failure retained

Linux45/45 in425.18s and the complete native/RAW workflow pass. Windows45/45 in1,127.87s and its actual11-mode SDK-stripped package pass. Each passing platform retains nine admittedGPU max0/65535.

| Actual Windows package CPU / GPU ms | Original RAW | Owned brush | Real Sony |
| --- | --- | --- | --- |
| L | 55,128 / 37,762 | 30,719 / 19,293 | 49,356 / 43,364 |

RAW retains23 successful captures/allUI/new target guards/amount−1.904/full10,340,960/QML0/60s; the CPU vignette capture was inspected with visible−1.90. Both wait counters1/not-ready0/polish0 remain normal-path evidence. Owned24/nine/7 blocked+4 ready/full2,659,021/45s and Sony16/six/native32,947,264/sourceSHA/shared3GiB/60s retain prior criteria. Windows's configured60-minute whole-job workflow completes; this is not claimed as an observed crossing of the old45-minute boundary.

Mac completes all45 CTest entries in911.13s:44 pass/one fail/zero skips. Core211 pass/one fail; only brushParallelRequestsIsolateRuntimeFailureAndInFlightCancellation fails the cancelled-future null assertion. Its own helpers/pending are observed before three unrelated futures start, but cancellation is set after those starts, allowing normal completion in that gap. Mac native/package stages are skipped. There is no Mac package manifest or validation ZIP. [M addresses the test gap](F4_PARALLEL_CANCEL_STAGE_M46M.md); L has no actual Mac vignette-package acceptance.

All three original full checkout logs show e7dc45ecbd76d98383d20ae80dd1616ef1029e21, tree d904cbd112be64ddb9a35964a4fe4e75a461836f equal L HEAD. Character counts Linux1,061,032/Windows966,328 (original CRLF)/Mac485,336. Four original ZIP sizes/SHA/CRC/safe extraction are verified:

| Artifact | ID | Bytes | SHA-256 |
| --- | --- | --- | --- |
| validation-Linux | 12718776484 | 32,774,972 | 8a59517b2cf131f88df9580d2a8d5449752536715876bb21b562b465a6a7e36e |
| validation-Windows | 12719724359 | 29,057,981 | 87963325a0dd0dfa0ce1410ab909065235f07ebe3f4a821eb3ca40c4973d41c5 |
| validation-macOS | 12719699470 | 362,373 | 75c885d6c046f53ca17a7176aef87a28a47de87caff7f88b444d23f57ecbe6ef |
| package-validation-Windows | 12721330515 | 15,854,201 | 2f4331d4387f52e55510805d8e4113691a7ab87d2b28596e8e9fd1706c40c960 |

The /tmp/jixellight-40470594391-summary.json, parent ci-40470594391-summary.json and receipts retain accepted=false, the original failure and skipped Mac package. No absent fifth artifact is invented.
