# LightCraft fusion implementation progress

Development branch: `design/lightcraft-fusion-20261008`; PR #16 remains Draft.
Baseline: `96572779c671583e9d170feec23b723197cc596c`.
Production `main` and PR #15 (`fix/a7r6-camera-base-as-shot`) are not updated or merged.

## Verified checkpoints

- Baseline CI: run **37738307832**, all Windows x64, macOS arm64 and Linux GPU jobs succeeded, including real Sony fixtures and desktop package validation.
- Session history checkpoint: **2681760259c4b8f41a8b14de3459dde20b06f09e**, run **37739677598** succeeded on all three platforms.
- Geometry/cache/PNG/CLI checkpoint: **c9dc8058309b0997e609ad2c11a2612c7657c777**, run **37741727439** succeeded on all three platforms, including GPU, real Sony renders, GUI and packages.
- Packaged CLI checkpoint: **a361e99**, run **37742871643** succeeded on all three platforms.
- Durable history/presets checkpoint: **ac3276d**, run **37746144128** succeeded on all three platforms.
- Catalog and virtual-copy checkpoint: **deba25f5832fc7506942ecbf5324f3f1b4aaa2ee**, run **37751488845** succeeded on all three platforms, including mandatory GPU, real Sony renders, GUI and desktop package validation.
- Shared-command/As Shot/save-retry checkpoint: **e24028635edbab155f386a6331559299a2e63c9e**, run **37754564896** succeeded on all three platforms, including mandatory GPU, real Sony renders, GUI and desktop package validation. Qt 6.8.3 Release and 46 local core checks passed; CPU RAW GUI smoke passed in 6.524 s.
- Version-management/date-index checkpoint: **aee4c4013d88dd0ce19d4c088d48b3dde1a6a2f6**, run **37757196524** succeeded on all three platforms, including mandatory GPU, real Sony renders, GUI and desktop package validation. Release build, 52 local core checks and CPU RAW GUI smoke (6.613 s) passed.
- Current preset-management batch: Release build, **54** local core checks and the complete 10-test CTest set passed (two local GPU suites explicitly skip). Empty/non-executable build artifacts were repaired and verified as actual ELF executables before acceptance; zero-output CTest results are not test evidence. CPU RAW GUI smoke passed in **7.171 s**, including the reachable preset menu, copy/date ordering, history/geometry and exact scopes. Cross-platform acceptance requires CI for this exact batch.
- Local verification: Qt **6.8.3** Release build; core, performance, monitor-color, RAW-worker, Sony look, real Sony fixture and CLI tests executed. Local offscreen environment has no compute backend: GPU/display-GPU tests skip and are **not GPU validation**. Every subsequent code batch must pass mandatory GPU CI before acceptance.

## Actual implemented scope

| Phase | Working code | Remaining scope |
|---|---|---|
| F0 | Alpha.11 baseline retained; cross-platform CI; existing real RAW/JPEG and GPU parity tests unchanged | Additional per-camera/cross-exposure evidence and performance measurements |
| F1 | Library/Develop shell, Filmstrip, asynchronous thumbnails, collapsible editing panels | Additional compact-window and layout polish |
| F2 | Independent named virtual copies with per-version Develop/history/curation/annotations, transactional rename/delete, shared read-only source and unique batch export destinations; collection albums, keywords, color labels, combined filename/keyword/album/label/curation filters; catalog/filename/rating and capture/import/Develop-date sorting (missing dates last); background metadata indexing; Ctrl/Meta and Shift selection; batch rating/flag/keywords/label/album assignment; WAL-consistent backup before legacy annotations/date migration; project restore; exclusive new-project creation and failed-create writer preservation; ratings, Pick/Reject; per-photo durable project history, Undo/Redo, 128 undo steps; slider gestures coalesced; full immutable Sony LUT snapshots; reset/paste/sync covered; bilingual history UI; user-local named Develop presets with rename, explicit replacement and portable import/export (Sony Look included, crop/curation excluded) | XMP |
| F3 | Source cache retained; independent 128 MiB prepared-preview LRU; keys include engine, QImage source identity, geometry/viewport; cache counters/timings in performance diagnostics; dependency manifest; diagnostic CPU capture uses explicit RAW semantics and current geometry | Splitting color kernels into stages, true float RAW source, highlight upgrades, output hashes and additional cache tiers |
| F4 | Normalized original-coordinate crop; centered 1:1/3:2/4:3 crop UI; 90° rotation, horizontal/vertical flip, reset; same geometry in CPU/GPU preview, full scopes and both export formats; persisted in adjustments and Undo/Redo | Interactive crop handles/straighten, transform/lens corrections, masks, Texture/Clarity/Dehaze, noise/sharpen, grading, healing |
| F5 | 16-bit RGBA PNG with target ICC; current and batch export; atomic QSaveFile with cancellation preserving destination; original protection; existing tiled JPEG and 1024-bin scopes retained | TIFF/WebP, proofing, waveform/parade/vectorscope, import Copy/Move and broader RAW validation |
| F6 | Shared scalar/HSL/curve/geometry Develop registry used by UI and offline CLI; strict JSON commands and undoable GUI command replay; CPU JPEG/PNG export, schema inspection, invalid-command checks and existing-output refusal | Full action registry/replay, headless batch catalogs and optional MCP |

CLI is included in the Windows ZIP and macOS app under `Contents/MacOS`, with a deployed CLI safety test after stripping development Qt search paths.

The PNG writer uses full-frame rendered memory; JPEG retains its streaming 128-row path. Geometry currently runs on CPU before the existing GPU color pipeline, and is labeled accordingly. Decoded RAW storage remains RGBA64; GPU staging is FP32. These are not claims of a float RAW decoder or completed F0–F6.

## CLI examples

```sh
JixelLightCli --schema
JixelLightCli --commands adjustments.json --space display-p3 input.ARW new-output.png
JixelLightCli input.jpg new-output.jpg
```

`adjustments.json`:

```json
[{"command":"develop.set","parameter":"exposure","value":0.5}]
```

CLI shares the existing scalar/HSL/curve ranges and clamps finite numeric values. It also supports normalized `geometry.crop`, relative integer `geometry.rotate`, flip toggles, geometry reset, curve reset and Develop reset. Unknown fields, invalid indices and invalid geometry are rejected without partial state changes. It uses CPU reference processing, As Shot look resolution and camera baseline metadata; it never downloads models or opens a remote service. GUI Undo/Redo snapshots and the cursor are persisted atomically with current adjustments in the catalog adjustment JSON (`_history`, schema 1). Legacy projects without history initialize from their current state. Unknown or corrupt histories reject project opening before replacing the active writer. Shared Sony LUTs are serialized once per photo history and validated on restore, including the same fitted-engine compatibility rule as Look profile imports. Persisted/copied/synced/preset As Shot state preserves the symbolic user request; camera-derived code/parameters are resolved only for rendering. Earlier draft histories with only stale resolved As Shot metadata differences are restored to the exact user snapshot, while differing scalars/strength are rejected. Failed writes remain sticky per category/photo until the same data is successfully retried; GUI recovery retains history and catalog annotations too.

User-local named presets live in the application data directory and use atomic writes. Save/import refuse duplicate names; Rename refuses collisions; Update is a separate operation with GUI confirmation. Unreadable/unknown-version files remain protected. Portable `.jixelpreset.json` files validate the current rendering engine and full Sony state, exclude geometry/curation and use a same-folder temporary file with a non-overwriting final rename; importing requires an unused name and never applies the preset to the active photo automatically.

## Regression coverage in this batch

- Copy rename preserves redo; delete remaps selection, reloads the following version under a new epoch, rolls back all catalog tables on failure, and safely clears the canvas when the last copy is removed. Original paths are rejected by copy-management APIs.
- Date JSON validation, actual EXIF capture indexing, missing capture dates, independent version edit dates and import-date preservation across copied/reopened projects. Legacy dates derive only from the SQLite import record; no file mtime is invented as a capture time. A WAL-consistent backup precedes the additive date table, and edits/history/dates share a transaction. Unknown date schemas preserve the active writer.
- Undo branching, redo invalidation, gesture boundaries and bounded history; reopening at an undone cursor preserves redo; corrupt/unknown versions and LUT hashes are rejected without switching the active writer.
- Shared GUI/CLI geometry/HSL/curve commands, strict-field/index checks, invalid-command state isolation, command Undo/Redo and deployed CLI transformed PNG checks.
- Actual Sony RAW As Shot metadata → saved symbolic history → reopen/undo, narrow older-draft compatibility and failed-write retry of history/tags.
- Virtual-copy state/history/catalog isolation, project recreation and reopening, duplicate-key rollback, future mapping rejection, source-byte preservation and distinct PNG batch results; RAW GUI smoke rapidly switches original/copy before history/geometry/scopes checks.
- Catalog selection/annotation isolation, legacy WAL-consistent backup and recovery from backup failure, unknown catalog-schema rejection, saved album/keyword/label recovery, compact Library filtering/sorting smoke.
- Named preset persistence, Sony fitted-engine rejection, Sony As Shot round trip, geometry exclusion, duplicate refusal, deletion and unreadable-file preservation.
- Preset rename/replacement isolation, portable round trip, import engine rejection, duplicate/destination refusal and original-file protection; management does not change the active photo or its undo history. GUI smoke opens/closes the actual in-window management menu, with screenshot QA and qualified dialog properties.
- Per-photo isolation, Sony As Shot/manual state, reset/paste and restored project state.
- Cache reuse, exposure-independent prepare keys, geometry/source invalidation and cancellation.
- Geometry orientation/pixel mapping, untouched original pixels, legacy default state and JSON round trip.
- Exact PNG 16-bit pixel comparison for explicit RAW and non-RAW semantics, ICC round trip, cancellation and batch PNG.
- CLI schema, finite/range validation, invalid/unknown commands, JPEG/PNG export and original/existing-file preservation.
- GUI smoke rotates and crops a photograph, requires exact cropped pixel counts, undoes both, and restores full-resolution scopes. Local Leica DNG: 5,170,480 cropped pixels → 10,340,960 restored pixels; CPU smoke 6.632 s, two prepare-cache hits. These are container measurements, not a real-GPU or 24MP performance claim.
- Layout screenshot QA corrected geometry-control overflow and made the current history step visible.
- Headless CLI and deployed-package CLI safety checks cover builds without graphic platform plugins and without development SDK lookup paths.

No LightCraft source files or third-party assets are copied in this batch; these are independent C++/QML implementations of the plan's architectural ideas. Existing engine version, Sony evidence validation, GPU parity threshold, 1024 bins and monitor-ICC separation remain unchanged.
