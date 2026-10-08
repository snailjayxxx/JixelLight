# LightCraft fusion implementation progress

Development branch: `design/lightcraft-fusion-20261008`; PR #16 remains Draft.
Baseline: `96572779c671583e9d170feec23b723197cc596c`.
Production `main` and PR #15 (`fix/a7r6-camera-base-as-shot`) are not updated or merged.

## Verified checkpoints

- Baseline CI: run **37738307832**, all Windows x64, macOS arm64 and Linux GPU jobs succeeded, including real Sony fixtures and desktop package validation.
- Session history checkpoint: **2681760259c4b8f41a8b14de3459dde20b06f09e**, run **37739677598** succeeded on all three platforms.
- Geometry/cache/PNG/CLI checkpoint: **c9dc8058309b0997e609ad2c11a2612c7657c777**, run **37741727439** succeeded on all three platforms, including GPU, real Sony renders, GUI and packages.
- Subsequent UI/crop-scopes and headless packaged-CLI validation is tracked on PR #16; do not infer acceptance of a later HEAD from a prior checkpoint.
- Local verification: Qt **6.8.3** Release build; core, performance, monitor-color, RAW-worker, Sony look, real Sony fixture and CLI tests executed. Local offscreen environment has no compute backend: GPU/display-GPU tests skip and are **not GPU validation**. Every subsequent code batch must pass mandatory GPU CI before acceptance.

## Actual implemented scope

| Phase | Working code | Remaining scope |
|---|---|---|
| F0 | Alpha.11 baseline retained; cross-platform CI; existing real RAW/JPEG and GPU parity tests unchanged | Additional per-camera/cross-exposure evidence and performance measurements |
| F1 | Library/Develop shell, Filmstrip, asynchronous thumbnails, collapsible editing panels | Additional compact-window and layout polish |
| F2 | Independent named virtual copies with per-version Develop/history/curation/annotations, shared read-only source and unique batch export destinations; collection albums, keywords, color labels, combined filename/keyword/album/label/curation filters; catalog/filename/rating sorting; Ctrl/Meta and Shift selection; batch rating/flag/keywords/label/album assignment; WAL-consistent backup before legacy annotations migration; project restore; exclusive new-project creation and failed-create writer preservation; ratings, Pick/Reject; per-photo durable project history, Undo/Redo, 128 undo steps; slider gestures coalesced; full immutable Sony LUT snapshots; reset/paste/sync covered; bilingual history UI; user-local named Develop presets (save/apply/delete, Sony Look included, crop/curation excluded) | Virtual-copy rename/delete, capture/import/edit-date sorting, preset renaming/overwriting/export, XMP |
| F3 | Source cache retained; independent 128 MiB prepared-preview LRU; keys include engine, QImage source identity, geometry/viewport; cache counters/timings in performance diagnostics; dependency manifest; diagnostic CPU capture uses explicit RAW semantics and current geometry | Splitting color kernels into stages, true float RAW source, highlight upgrades, output hashes and additional cache tiers |
| F4 | Normalized original-coordinate crop; centered 1:1/3:2/4:3 crop UI; 90° rotation, horizontal/vertical flip, reset; same geometry in CPU/GPU preview, full scopes and both export formats; persisted in adjustments and Undo/Redo | Interactive crop handles/straighten, transform/lens corrections, masks, Texture/Clarity/Dehaze, noise/sharpen, grading, healing |
| F5 | 16-bit RGBA PNG with target ICC; current and batch export; atomic QSaveFile with cancellation preserving destination; original protection; existing tiled JPEG and 1024-bin scopes retained | TIFF/WebP, proofing, waveform/parade/vectorscope, import Copy/Move and broader RAW validation |
| F6 | Shared scalar develop parameter registry used by UI and offline CLI; JSON develop.set commands; CPU JPEG/PNG export, schema inspection, invalid-command checks and existing-output refusal | Full action registry/replay, headless batch catalogs and optional MCP |

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

CLI shares the existing scalar ranges and clamps finite numeric values. It uses CPU reference processing, As Shot look resolution and camera baseline metadata; it never downloads models or opens a remote service. GUI Undo/Redo snapshots and the cursor are persisted atomically with current adjustments in the catalog adjustment JSON (`_history`, schema 1). Legacy projects without history initialize from their current state. Unknown or corrupt histories reject project opening before replacing the active writer. Shared Sony LUTs are serialized once per photo history and validated on restore, including the same fitted-engine compatibility rule as Look profile imports. User-local named presets live in the application data directory and use atomic writes; existing names require deletion or a new name, and unreadable/unknown-version files are protected from replacement.

## Regression coverage in this batch

- Undo branching, redo invalidation, gesture boundaries and bounded history; reopening at an undone cursor preserves redo; corrupt/unknown versions and LUT hashes are rejected without switching the active writer.
- Virtual-copy state/history/catalog isolation, project recreation and reopening, duplicate-key rollback, future mapping rejection, source-byte preservation and distinct PNG batch results; RAW GUI smoke rapidly switches original/copy before history/geometry/scopes checks.
- Catalog selection/annotation isolation, legacy WAL-consistent backup and recovery from backup failure, unknown catalog-schema rejection, saved album/keyword/label recovery, compact Library filtering/sorting smoke.
- Named preset persistence, Sony fitted-engine rejection, Sony As Shot round trip, geometry exclusion, duplicate refusal, deletion and unreadable-file preservation.
- Per-photo isolation, Sony As Shot/manual state, reset/paste and restored project state.
- Cache reuse, exposure-independent prepare keys, geometry/source invalidation and cancellation.
- Geometry orientation/pixel mapping, untouched original pixels, legacy default state and JSON round trip.
- Exact PNG 16-bit pixel comparison for explicit RAW and non-RAW semantics, ICC round trip, cancellation and batch PNG.
- CLI schema, finite/range validation, invalid/unknown commands, JPEG/PNG export and original/existing-file preservation.
- GUI smoke rotates and crops a photograph, requires exact cropped pixel counts, undoes both, and restores full-resolution scopes. Local Leica DNG: 5,170,480 cropped pixels → 10,340,960 restored pixels; CPU smoke 6.632 s, two prepare-cache hits. These are container measurements, not a real-GPU or 24MP performance claim.
- Layout screenshot QA corrected geometry-control overflow and made the current history step visible.
- Headless CLI and deployed-package CLI safety checks cover builds without graphic platform plugins and without development SDK lookup paths.

No LightCraft source files or third-party assets are copied in this batch; these are independent C++/QML implementations of the plan's architectural ideas. Existing engine version, Sony evidence validation, GPU parity threshold, 1024 bins and monitor-ICC separation remain unchanged.
