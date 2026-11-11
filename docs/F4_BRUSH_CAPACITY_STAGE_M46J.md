# F4 M4.6J — target-specific brush capacity feedback

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. J `fbf61f638f4e660c717240f4dfc55830ae3d4d90` is not accepted: its Linux job was cancelled at the25-minute total-job budget after38/45 tests. Windows/macOS45/45 and both actual11-mode packages pass. The last fully accepted checkpoint remains [I at `8ed006a`](F4_BRUSH_REVEAL_WAIT_STAGE_M46I.md), [CI40445440071](https://github.com/snailjayxxx/JixelLight/actions/runs/40445440071). Earlier F/G/H failures remain retained.

## Committed Linux cancellation and successful desktop gates

[CI40457184947 Linux122033117031](https://github.com/snailjayxxx/JixelLight/actions/runs/40457184947/job/122033117031) was cancelled by the total job budget. GitHub's check annotation states: “The job has exceeded the maximum execution time of 25m0s.” Dependency/Qt preparation before the build consumed approximately13 minutes. At cancellation,38/45 tests had passed without an observed assertion failure; `brush-gpu-pan100` was running. Linux native pointer, Sony GUI and subsequent gates did not run. This is incomplete acceptance, not a45/45 pass.

The complete346,584-character original job log and original292,284-byte validation ZIP are retained under `/tmp/jixellight-40457184947-*attempt1*`. ZIP SHA-256 is `89a8c443b86caac1573c8832614e5d790758c935ffc8923c07bc1e68f576541b`; original artifact12713539526 was verified against GitHub's digest, CRC and safe extraction. Full-log checkout proves actual merge `1b43d27ca3cc9bd7878f575df6c260afa9085339`, tree `637634a5bb7cd5c7f7aa9b7fe7bdfe5bf558d628`, equal to J HEAD. Parent `brush-capacity-linux-cancelled-proof.json` records the partial result and source proof. J is not retroactively accepted.

Windows/macOS completed successfully on their original jobs:45/45 in1,138.71s/881.08s, zero failures/skips. Each retains212 Core/36 GPU/39 performance/21 Look/17 admission/new capacity3, and all18 admitted GPU cases across those two platforms have max0/65535. Owned native CPU/GPU Windows30,835/19,413ms and macOS23,796/24,421ms pass24/nine/all seven bilingual blocked cases and four ready/recovery cases/full2,659,021/QML0/45s. Sony native49,536/43,477ms and23,680/23,617ms retains16/six/full32,947,264/sourceSHA/shared3GiB/60s.

Both actual SDK-stripped11-mode packages pass:

| Actual package CPU / GPU ms | Original RAW | Owned brush | Real Sony brush |
| --- | --- | --- | --- |
| Windows | 54,951 / 37,640 | 30,965 / 19,380 | 49,431 / 43,035 |
| macOS | 32,358 / 30,096 | 24,420 / 23,673 | 22,902 / 15,765 |

Original RAW retains23 screenshots/full10,340,960/all UI/QML0/60s; owned retains24/nine/7 blocked+4 ready/current full scopes/45s; Sony retains16/six/current full statistics/sourceSHA/3GiB/60s. Mac packaged CPU observes one pending reveal before passing; all other J owned counters are0 and are not credited with that delayed path. Full original Windows966,143-character/CRLF and macOS954,770-character logs prove the same actual1b43d27/tree637634 as Linux. All five original validation ZIPs have verified GitHub sizes/SHA/CRC/safe extraction; `/tmp/jixellight-40457184947-receipts.json` and parent `ci-40457184947-summary.json` record them, accepted=false. Actual standalone Mac Metal capacity capture was inspected: visible English hint/no transient; its intermediate previous-result scope remains labelled separately from final full-statistics acceptance.

After all current jobs finished, K applies a Linux-only25→45-minute total-job repair. All owned45s, RAW/Sony60s, performance120s and numerical limits remain unchanged. No passed local heavy suite is restarted for this workflow-only repair. J remains failed; fresh committed K acceptance is required.

## Problem and behavior

Previously the canvas advertised a Cross cursor and “Drag to paint” at the shared layer, selected-layer stroke, and whole-document dab limits. Layer/stroke requests were refused by the controller; a full document could still start a transient path before its final edit was refused. The UI now reads the same target-specific refusal used before creating a gesture. It shows a localized recovery hint and Forbidden cursor and ignores pointer input for that target.

The existing limits remain 16 layers, 64 strokes per layer and 16,384 document dabs. Existing enabled layers remain paintable at the layer limit. A new layer remains paintable when only the selected layer has reached its stroke limit. A full document blocks both. Disabled layers and zero-EV strokes continue to count. Disabled/missing/invalid-target precedence remains explicit.

`brushCapacity` exposes read-only counts, shared maxima and new-layer availability. Each existing `brushLayers` row exposes its own append availability and language-independent refusal key. QML derives the actual hint, cursor and New layer button from these values. The existing layer selector tooltip shows the counts without adding rows to the compact panel. Source/display readiness and whole-operation admission remain separate guards; the new metadata changes neither edit schema nor image processing.

## Actual native RED

Before changing production controller/header/QML code, the owned native scenario installed a valid full document: four 4,096-dab strokes, enabled with zero EV. This avoids both an invalid single 16,384-dab stroke and an unrelated active-work refusal. It is a test-owned PNG fixture, not Sony evidence or an active 16,384-dab rendering benchmark.

The unchanged I production code failed phase40 at **10,291ms**: 16,384 dabs, Cross cursor2, the old Drag hint, enabled New layer, and a real pointer gesture starting a yellow transient path. Escape preserved document, history, ROI and source bytes. Original JSON, capacity/failure PNGs and the failed log remain in `build-local/local-brush-capacity-red/` and parent `brush-capacity-native-red.log`.

The initial private post-run auditor also incorrectly required full statistics while this count fixture was still updating. It exited1; its original failure is retained. Independent inspection of the same artifacts confirmed the product RED without rerunning the GUI. The observed 149,292-pixel previous/interim scope is explicitly **not** full-statistics acceptance. Parent `brush-capacity-native-red-summary.json` records both the reproduced product defect and this auditor error. Final GREEN restores the original intent and waits for its current complete visible scope.

## Controller and native GREEN

The new `brush-capacity` CTest shard has the existing 120s bound. It verifies read-only counts/notifications, all shared maxima, target-specific layer/stroke limits, disabled full documents, refusal before any drawing notification, the final single available dab as one Undo, remove/Undo/Redo, project reopen, and switching sources away and back with refreshed availability and unchanged source hashes. Its expanded focused run passed in5.04s. Source switching/reopen at capacity are controller coverage; they are not described as additional native capacity scenarios.

Each of four actual owned native runs retains the old23 flags/eight captures and locale/manual-scroll assertions, adding `capacity_guard` and a ninth capture. Real native press/move/release verifies seven blocked cases: full document in English and Chinese, Undo back to full in Chinese, new target at16 layers in both languages, and selected target at64 strokes in both languages. Each requires the visible exact hint, Forbidden14, correct New layer availability, no transient path and unchanged document/history/ROI.

Four further cases require a real gesture to start, then Escape with state preserved: after removing a stroke, after Redo frees capacity, an existing layer at16 layers, and a new layer when the selected layer has64 strokes. The original document and English UI are restored. Completion requires the current full2,659,021-pixel scope, the displayed plot source/status, visible waveform ink, and current preview under the unchanged45,000ms deadline.

| Fresh owned native matrix | CPU ms | Metal ms |
| --- | --- | --- |
| Normal display | 13,306 | 12,846 |
| Actual2× display | 13,989 | 13,418 |

All four pass24 flags/nine captures, all seven blocked and four ready/recovery cases, full statistics and QML0. The actual2× Metal capacity capture was inspected: the English full-document hint is visible and no transient path is drawn. Its scope still says “updating (previous result)” during the intermediate count fixture; final current full statistics are checked separately after restoring the original intent. All four local reveal-wait counters are0; the delayed reveal branch remains evidenced by I's actual packaged Metal run rather than attributed to these normal-path checks.

Physical evidence is in `build-local/local-brush-capacity-matrix/`; parent `brush-capacity-matrix-local-summary.json` and `/tmp/jixellight-brush-capacity-matrix-local-summary.json` contain the nine tested source hashes and full wrappers. Daily native preference bytes remain unchanged. These reports retain the precommit base8ed006a and passed=true/accepted=false; they are not relabelled as committed J CI.

## Regression and committed acceptance

The complete local suite passes **45/45 in455.10s**, zero failures/skips, with mandatory Metal, pinned public Sony and real DNG. It retains212 Core/36 GPU/39 performance/21 Sony Look/17 admission checks and adds the capacity shard's3 QtTest entries. All nine admitted GPU cases have maximum error0/65535. Large Sony whole/ROI sRGB, Display-P3 and Rec.2020, full statistics and PNG16/TIFF16/WebP8/JPEG checks pass under their original120s bounds. Parent `brush-capacity-full-tests-summary.json` retains the JUnit, Qt totals, original evidence and tested source hashes; raw log is `brush-capacity-full-tests.log`.

Fresh actual Sony normal/2× CPU/Metal checks also pass. Each retains all16 flags/six photograph captures kept privately, native7028×4688, full32,947,264 histogram and waveform pixels, pinned source SHA, actual selected-stroke deletion/Undo, current visible controls/plot, shared3GiB and the original60,000ms deadline. The actual2× Metal Chinese controls/full waveform crop was inspected. Only controls/waveform crops and numerical/hash evidence enter the public evidence directory.

| Fresh real-Sony native GUI | CPU ms | Metal ms |
| --- | --- | --- |
| Normal display | 12,837 | 12,957 |
| Actual2× display | 15,258 | 14,707 |

Original real DNG GUI passes CPU14,252ms and Metal15,519ms with23 successful screenshots each, all UI/selected-export flags, full10,340,960 scopes, QML0 and the unchanged60,000ms bound. Diagnostic CPU124ms/Metal500ms, native linear/radial mask923ms and missing-source relink487ms pass their existing validators. These are fresh results on the changed source snapshot, not reused I/H timings or a performance-improvement claim. Daily preference SHA remains unchanged across all checks.

Physical evidence is in `build-local/local-brush-capacity-gui/`; full Sony photograph captures remain separately under physical build parent `private-sony-brush/capacity-*`. Parent `brush-capacity-gui-local-summary.json` and `/tmp/jixellight-brush-capacity-gui-local-summary.json` retain all ten checks, nine source hashes and their precommit8ed006a stamp. Log is parent `brush-capacity-gui-validation.log`.

No old acceptance result is substituted for these changed production paths. A subsequent repaired committed workflow must complete all-three45/45 suites, native wrappers, actual tested source/tree proof and both actual11-mode SDK-stripped packages before acceptance; J retains its original Linux cancellation.

## Preserved boundaries

Source32Mi pixels, shared3GiB operation budget, research2Mi, tile8Mi, work96Mi and GPU40/65535 are unchanged. Large RAW/export/statistics acceptance, original RAW/Sony60,000ms GUI gates, owned45,000ms and120s performance shards remain. The pinned public Sony ARW remains native7028×4688/32,947,264 pixels with its original SHA; full photograph captures remain outside public artifact folders. This slice adds capacity feedback to the guarded brush workflow; eraser, pressure, live-drag color, native GPU brush, native FP32 RAW and F0–F6 completion remain unclaimed.
