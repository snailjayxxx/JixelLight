# F5 — export selected photos and versions

**Accepted on the exact implementation tree.** Implementation HEAD `8dc75fa43aa77693ff0a39d7990c960090bf75a8` on `design/lightcraft-fusion-20261008` passed local acceptance and [CI run 40289232242](https://github.com/snailjayxxx/JixelLight/actions/runs/40289232242), including all three platforms and both actual deployed packages. CI merge `f3dc08043f6296ee95a2ff36d52d5f252909c219` has tree `d93d50e3790dbc13982f266cdc93ad1901ca2352`, identical to that HEAD's tree. PR #16 remains Draft.

## Scope and user behavior

The export menu gains a separate **导出所选照片/版本… / Export Selected Photos/Versions…** action. It captures the currently selected catalog versions when opening Export Settings. The dialog displays that captured count and explains that changing or clearing the current selection does not redirect this export. Originals and virtual copies are independently selectable versions, even when they share a source file.

The existing **Export Current Photo** action retains its meaning; the all-catalog action is labelled **Export All Photos/Versions** to make its scope explicit. `exportCurrent` and `exportAll` are unchanged; the latter still exports the entire catalog. Opening either existing action clears the dialog's selected-only mode and captured keys. The existing naming pattern/sequence settings remain shared. Selected-only state and captured identities are transient dialog state, not saved selection preferences.

This stage does not add automatic subject selection, brush production access, new output formats, a second export queue or a new rendering algorithm. JPEG, PNG 16-bit, TIFF 16-bit and WebP continue through the existing exporters and output-color handling.

## Stable identities and snapshot timing

The controller exposes three new invokables:

```cpp
QStringList selectedVersionKeys() const;
QVariantMap exportVersionNamePreview(const QStringList &keys, const QString &pattern,
                                    int sequenceStart=1, const QString &format="jpeg") const;
bool exportVersions(const QStringList &keys, const QUrl &folder,
                    const QString &colorSpaceKey="srgb", int quality=92,
                    const QString &format="jpeg", const QString &pattern={}, int sequenceStart=1);
```

`selectedVersionKeys()` walks the catalog in stored order and returns each selected entry's exact `storageKey()`. It does not substitute filenames, indices, the current photo or all photos when the selection is empty. Callers may provide keys in any order; the shared catalog resolver produces the existing catalog order, making sequence numbers stable relative to that order. A deletion before a selected row can change its index without changing its identity.

The explicit-key APIs reject an empty request, more than 1,000 requested keys, an empty key, duplicate keys, unknown/removed keys or an ambiguous catalog identity. Failure never silently widens the request to current/all. An invalid-key preview returns `valid: false`, no names, count zero and a localized error. The preview otherwise uses the existing map schema: `valid`, the first three `names`, `count` and `error`.

There are three distinct snapshot boundaries:

1. Opening the selected action captures **which version keys** belong to this dialog. Changing or clearing the live selection leaves that scope intact. If a captured version is removed, validation fails instead of choosing its replacement row.
2. Naming preview resolves those keys against the **current version metadata**: source path, version name and recorded capture time. Renaming a version or receiving capture metadata can update its preview. Preview has no target folder, so it does not include that folder's occupied filenames. Actual acceptance plans names against the chosen folder and its current occupied names, preserving the existing default collision avoidance/custom collision refusal behavior.
3. `exportVersions` resolves the keys once into a catalog-order `PhotoEntry` value snapshot. That same resolved list drives both whole-batch names and `ExportRequest` creation. When queue admission succeeds, source paths, destination names, adjustment/geometry state, output space and quality are frozen in those requests. Later selection changes, edits or virtual-copy renames do not redirect or change accepted work.

The dialog therefore freezes scope before the folder picker, while **edit snapshots freeze at actual queue acceptance**. It does not promise to freeze every edit for the entire period that Export Settings or the native folder picker is open. Export workers load the frozen source paths through the existing source cache; the GUI does not synchronously decode every selected source to construct the plan.

## QML folder handoff

`exportSettingsDialog` owns `selectedOnly` and `capturedVersionKeys`. Its naming preview passes that captured list to `exportVersionNamePreview`, independently of subsequent `libraryChanged` selection notifications. On Continue, it copies the same keys and selected-only flag into `batchExportFolderDialog` before opening the folder picker. The folder's acceptance handler uses its captured list for `exportVersions`; ordinary all-catalog mode continues to call `exportAll`.

Stable automation names are:

| Object | `objectName` |
| --- | --- |
| New selected action | `selectedBatchExportAction` |
| Existing all-catalog action | `batchExportAction` |
| Export settings | `exportSettingsDialog` |
| Captured-selection explanation | `selectedExportSummary` |
| Folder handoff | `batchExportFolderDialog` |

The existing `batchExportTemplate`, `batchExportSequence`, `batchExportFormat`, `batchExportNamePreview`, `exportSettingsContinue` and `exportSettingsCancel` names remain available. Cancelling the settings dialog starts no export and does not alter edits/history. Reopening selected export captures a fresh selection; opening either existing export action resets the selected scope.

## Plan validation and publication

Before queue admission, selected export validates all requested identities, the complete naming plan, output format/sequence rules, destination paths and persistence flush. Names and requests use one resolved catalog-order list; there is no second selection lookup between them.

Only selected versions participate in naming validation. An unselected source lacking capture time does not prevent a date-named selected export. Unselected versions with duplicate name stems likewise do not introduce collisions into the selected plan. Within the selected plan, the existing custom-template collision rules still reject the entire request before queuing any file.

Every planned destination is checked against **all imported originals and reference photographs**, including unselected ones. Occupied names include files, directories and symlinks. The existing default pattern avoids occupied names; a conflicting custom name rejects the plan. Existing destinations and symlinks are also checked explicitly before queue admission. `flushEdits()` must succeed before the batch starts.

Accepted requests use the existing `ExportQueue` with `newFileOnly=true`. Its private staging directory, repeated destination checks and rename publication prevent replacing an existing target and clean up staging files. This remains per-file publication: rejecting an invalid plan queues nothing, while a later worker failure/cancellation follows the existing queue's completion semantics. The controller does not synchronously decode sources or move B5 pixel/work admission out of the exporters. Active-brush limits and UI/persistence restrictions are unchanged.

`ActionTrace` records `selected_export_queued` with the count and actual resolved catalog-order `version_keys`, rather than the caller's possibly reversed key order.

## Regression coverage

Six controller tests cover actual queue output and compare PNG16 pixels against independently decoded CPU references:

| Core regression | Required behavior |
| --- | --- |
| `selectedVersionExportOnlyWritesTwoOfTenAndAllStillWritesEntireCatalog` | Five originals and five virtual versions; exactly two requested versions export in catalog order even with reversed keys; sequence preview matches files and pixels; ordinary `exportAll` still writes all ten; originals remain byte-identical. |
| `selectedVersionKeysSurviveDialogSelectionChangesAndCatalogIndexShifts` | Captured keys remain bound to the intended original/copy after an earlier virtual copy is removed, selection changes/clears and another version is edited; output names and pixels still match the captured identities. This simulates the native-folder-dialog boundary without using its live selection. |
| `selectedVersionExportRejectsEmptyDuplicateUnknownAndRemovedKeysAtomically` | Empty/duplicate/unknown/removed/empty-string identities reject the complete batch, return an invalid preview and produce no queue signal or file; later valid export recovers. |
| `selectedVersionNamingIgnoresUnselectedMissingDatesAndNameCollisions` | Unselected same-stem versions and undated photographs can invalidate the all-catalog preview while a valid selected date-named request succeeds with the recorded camera wall clock. |
| `selectedVersionExportPreservesAllOriginalsAndRejectsWholeBatchCollisions` | Protect an imported original in the destination folder even when unselected; reject selected-name collisions and an existing target before any other planned file is written; preserve all original/occupied bytes. |
| `selectedVersionExportFreezesPathsNamesGeometryAndPixelsBeforeQueueRuns` | After acceptance, change selection, exposure, crop/rotation and virtual-copy name before pumping events; source/version destinations, exact output pixels and Display P3 ICC still match the accepted state; no staging directories or original changes remain. |

The existing batch-export/current-export, queue new-file safety, cancellation and output-format regressions remain required. The existing smoke-readiness policy test is extended across the new UI phases; it continues to require pixel readiness at phase 50.

## GUI and package acceptance

Smoke phases **296–299** exercise the new action through an actual menu click. They require selected-only settings, a visible captured-count summary, two captured keys, valid PNG names with sequences 0007/0008, an enabled Continue button and a preview that stays unchanged when the live selection changes. Cancel must close the dialog without queuing an export or changing edits/history. At 1180×720 the selected dialog must fit inside the window.

These phases, like existing naming phases 290–295, inspect QML state and may advance while the previous image is rendering. They do not weaken the original **60,000 ms** GUI deadline or phase 50's wait for the restored RAW's complete full-resolution scopes. The native folder's acceptance boundary is covered by explicit-key controller regressions; the GUI smoke checks the real selected-settings capture/cancel flow rather than automating a native OS folder choice.

The report adds mandatory `ui_selected_export_passed` and `ui_selected_export_trace` fields. The package validator requires the new success flag in addition to existing GUI checks. The new `.export-selected.png` screenshot brings the expected count to **23**, retaining all previous **22** screenshots. Existing screenshot timing continues to include each capture/save and the final screenshot.

## Acceptance evidence

| Validation | Status |
| --- | --- |
| Complete application and test build | Passed local Release build with Qt 6.8.3 on Apple M2. |
| Six targeted Core regressions and readiness policy | Passed all six new tests plus the policy regression: 9/0/0 including initialization/cleanup, 506 ms. Existing export tests also passed in the full suite. |
| Full local CTest, real Sony/DNG and GPU/output safety regressions | **10/10 in 85.49 s; 209 Core / 36 actual Metal GPU / 39 performance, zero skips**, with mandatory public Sony and DNG fixtures. |
| CPU and native Cocoa/Metal GUI | **14,566 / 15,766 ms** under unchanged 60,000 ms. Both passed selected capture/cancel, all trace flags, 1180×720 compact fit, all prior naming/UI checks, **23/23 saved screenshots**, restored **10,340,960-pixel full scopes**, and zero QML error markers. |
| Three-platform final-tree CI and actual deployed packages with mandatory selected-export flag | **Accepted: run 40289232242, exact tree `d93d50e3790dbc13982f266cdc93ad1901ca2352`; 209 Core / 36 GPU / 39 performance on every platform; 10/10 CTest, zero skips; both deployed packages and all four CPU/GPU GUI reports passed.** |

The accepted CI evidence below comes from the combined implementation/test tree. Earlier CI runs do not contain the new selected export or its mandatory package assertions.

Local reports: `build-local/local-selected-export-tests.xml`, `build-local/local-selected-export-gui-{cpu,metal}/report.json`; targeted and full logs are in the external build root as `selected-export-core-targeted.txt` and `selected-export-full-tests.txt`. The final GUI runs followed the last QML label polish; earlier intermediate GUI runs are excluded. The CPU selected-export screenshot was inspected at its compact size. GUI evidence covers settings capture/cancel, while actual output, native-folder-boundary identity behavior and accepted edit snapshots are covered by the six controller regressions.

### Exact-tree CI and deployed packages — 2026-10-26

Run **40289232242** completed successfully for `desktop (macos-15, JixelLight-macOS-arm64, arm64-osx-static)`, `desktop (windows-2022, JixelLight-Windows-x64, x64-windows-static-md)` and `linux-gpu`. Every checkout recorded merge `f3dc08043f6296ee95a2ff36d52d5f252909c219`, whose parents are base `4efe6aa2ec81eb83053dea239be5d7fa3435d500` and implementation HEAD `8dc75fa43aa77693ff0a39d7990c960090bf75a8`. GitHub's merge tree and the local HEAD tree both resolve to **`d93d50e3790dbc13982f266cdc93ad1901ca2352`**. Deployed reports identify the compiled merge as `source_commit: f3dc080`.

| Platform | Core | Actual compute GPU | Performance | CTest |
| --- | --- | --- | --- | --- |
| Linux / OpenGL Compute llvmpipe | 209 passed | 36 passed | 39 passed | 10/10, zero skipped/failed |
| macOS / Metal | 209 passed | 36 passed | 39 passed | 10/10, zero skipped/failed |
| Windows / Direct3D WARP | 209 passed | 36 passed | 39 passed | 10/10, zero skipped/failed |

All **six named `selectedVersion…` regressions in the coverage table above passed on each platform**. They exercise actual two-of-ten versus all-catalog publication, stable identities across selection/index changes, complete invalid-key rejection, selected-only naming, protection of every original/existing target, and accepted source/name/geometry/pixel/Display P3 snapshots. This is output evidence in addition to the native settings capture/cancel smoke.

Both `package-validation-Windows` and `package-validation-macOS` artifacts have `manifest.passed: true`, `sdk_paths_removed: true` and **six successful modes**: `gpu`, `gpu-diagnostic`, `cpu`, `cpu-diagnostic`, `sony-native-cli` and `develop-cli`. Every GUI/diagnostic mode reports **zero QML runtime errors**. The native Sony and Develop CLI modes passed with SDK paths removed; manifests retain executable/fixture/probe SHA-256 evidence.

| Actual deployed GUI | Elapsed ms / unchanged 60,000 ms gate | Selected export | Saved screenshots | Restored full scopes |
| --- | --- | --- | --- | --- |
| Windows CPU | 55,329 | Passed | 23/23 | 10,340,960 pixels |
| Windows Direct3D WARP | 38,397 | Passed | 23/23 | 10,340,960 pixels |
| macOS CPU | 32,444 | Passed | 23/23 | 10,340,960 pixels |
| macOS Metal | 32,052 | Passed | 23/23 | 10,340,960 pixels |

All four reports have mandatory **`ui_selected_export_passed: true`** and all 16 boolean trace checks true: `menu_opened`, `menu_visible`, `action_clicked`, `selection_set`, `dialog_visible`, `compact_fit`, `summary_visible`, `keys_frozen`, `preview_valid`, `selection_changed`, `preview_stays_frozen`, `continue_enabled`, `cancel_clicked`, `closed`, `edits_unchanged` and `no_export_queued`. Each trace captured exactly **two version keys**, produced two valid PNG preview names with sequence suffixes 0007/0008, and retained that preview after the live selection changed. The new `.export-selected.png` and all earlier screenshots are present as actual artifact files with successful timing records. All previous required GUI success flags remain true.

The prior processing/output gates also passed on this tree:

- The real **16,384-dab indexed viewport** GPU test passed in output encodings 0/1/3 (sRGB, Display P3, ProPhoto); every platform reports maximum and mean CPU/GPU code difference **0** for all three outputs. Full-frame/viewport/HDR brush fallbacks, parallel-tile/scopes parity and FP32 preview/geometry/over-range safety tests passed without skips.
- All three SHA-pinned public Sony ARW/JPEG scenes passed the real-fixture suite and accepted dataset regression. The neutral/as-shot/FL3 CPU/GPU matrix contains three scenes and nine valid comparisons on each platform, with maximum 16-bit channel difference **2**. This remains rendering consistency and the existing scene regression, with the documented photographic-calibration limits.
- Real sensor evidence integrity, readiness refusal and the two-source offline unpacked audit passed. `cameraCalibrationAccepted` and `defaultRawReplacementAllowed` remain false; no real FP32 trial or full-RGB development is claimed by the unpack-only audit.

### Analysis-cache and capture evidence on the accepted tree

Every deployed CPU/GPU report ends with analysis cache budget **335,544,320 B**, charged/cached **24,624 B**, peak charged **82,727,728 B**, hits **1**, misses/publications **6/6**, evictions **5**, and zero bypasses, allocation failures, cancelled requests, outstanding builds or leased bytes. Each report records one encoded pass for all three plots over **10,340,960 pixels**. Between the preceding full histogram and that first encoded-plot event, there are **zero `cpu_pipeline`, geometry or preparation timing events**, confirming reuse of the rendered analysis frame. Windows CPU/GPU records 1/2 scopes deferrals while preparation is active; macOS records no such event. No cancelled cache request remains.

| Deployed GUI | First three-plot encoded pass ms | PNG grab total ms | PNG save total ms |
| --- | --- | --- | --- |
| Windows CPU | 2,083.756700 | 654.050000 | 1,670.502800 |
| Windows Direct3D WARP | 2,078.017200 | 990.969800 | 2,150.109500 |
| macOS CPU | 668.833208 | 271.998249 | 2,351.573377 |
| macOS Metal | 694.334542 | 256.257580 | 2,331.630628 |

All capture totals include **23 successful records**, including the final screenshot. Maximum grab/save items were Windows CPU `export-naming-invalid` **47.6701 ms** / `crop` **121.4 ms**; Windows GPU `export-naming-en` **89.7009 ms** / `crop` **240.1567 ms**; macOS CPU `crop` **36.406875 ms** / `parade` **203.501333 ms**; macOS GPU `presets` **48.049584 ms** / `waveform` **212.243875 ms**.

The unchanged paired performance row also passed with three samples per path, 2,918,067 actual pixels, budget 33,554,432 B and peak charged 23,344,584 B on each platform. Baseline/reuse medians were Linux **1,600.733362 / 898.898073 ms** (thread budget 2), macOS **3,439.362125 / 1,795.353583 ms** (budget 1) and Windows **4,923.704000 / 2,911.271800 ms** (budget 2); `timing_gate: false`. GUI wall clocks prove their fixed acceptance deadlines; differences between CI runs are not attributed to selected export, cache changes or brush-disabled work.

Downloaded evidence is isolated under `/tmp/jixellight-40289232242-*`: the complete `evidence.log`, `validation-{Linux,macOS,Windows}` artifacts, both `package-{Windows,macOS}` validation artifacts and the extracted `summary.json`. The summary retains all paired samples, selected trace checks, manifests, cache counters, timing windows and screenshot totals; original reports retain the complete traces. These temporary files are supplementary inspection evidence; the linked CI run and its artifacts are the retained source of record.
