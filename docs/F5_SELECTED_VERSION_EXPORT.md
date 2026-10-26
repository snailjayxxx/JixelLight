# F5 — export selected photos and versions

Implementation on `design/lightcraft-fusion-20261008`; PR #16 remains Draft. The combined implementation passed local Core, full real-Sony/Metal CTest and CPU/native-Metal GUI acceptance. Final-tree CI/deployment evidence remains pending below.

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
| Three-platform final-tree CI and actual deployed packages with mandatory selected-export flag | Pending. |

Only evidence from the combined implementation/test tree can accept this stage. Earlier CI runs do not contain the new selected export or its mandatory package assertions.

Local reports: `build-local/local-selected-export-tests.xml`, `build-local/local-selected-export-gui-{cpu,metal}/report.json`; targeted and full logs are in the external build root as `selected-export-core-targeted.txt` and `selected-export-full-tests.txt`. The final GUI runs followed the last QML label polish; earlier intermediate GUI runs are excluded. The CPU selected-export screenshot was inspected at its compact size. GUI evidence covers settings capture/cancel, while actual output, native-folder-boundary identity behavior and accepted edit snapshots are covered by the six controller regressions.
