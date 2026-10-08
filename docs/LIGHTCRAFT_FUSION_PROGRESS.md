# LightCraft fusion implementation progress

Development branch: `design/lightcraft-fusion-20261008`; PR #16 remains Draft.
Baseline: `96572779c671583e9d170feec23b723197cc596c`.
Production `main` and PR #15 (`fix/a7r6-camera-base-as-shot`) are not updated or merged.

## Verified checkpoints

- Baseline CI: run **37738307832**, all Windows x64, macOS arm64 and Linux GPU jobs succeeded, including real Sony fixtures and desktop package validation.
- Session history checkpoint: **2681760259c4b8f41a8b14de3459dde20b06f09e**, run **37739677598** succeeded on all three platforms.
- Next implementation batch: local Qt **6.8.3** Release build; core, performance, monitor-color, RAW-worker, Sony look, real Sony fixture and CLI tests executed. Local offscreen environment has no compute backend: GPU/display-GPU tests skip and are **not GPU validation**. The new batch must pass mandatory GPU CI before acceptance.

## Actual implemented scope

| Phase | Working code | Remaining scope |
|---|---|---|
| F0 | Alpha.11 baseline retained; cross-platform CI; existing real RAW/JPEG and GPU parity tests unchanged | Additional per-camera/cross-exposure evidence and performance measurements |
| F1 | Library/Develop shell, Filmstrip, asynchronous thumbnails, collapsible editing panels | Additional compact-window and layout polish |
| F2 | Project restore, ratings, Pick/Reject; per-photo session history, Undo/Redo, 128 undo steps; slider gestures coalesced; full immutable Sony LUT snapshots; reset/paste/sync covered; bilingual history UI | Durable history, albums, virtual copies, keywords/labels, richer sorting/selection, named editable presets, XMP |
| F3 | Source cache retained; independent 128 MiB prepared-preview LRU; keys include engine, QImage source identity, geometry/viewport; cache counters/timings in performance diagnostics; dependency manifest; diagnostic CPU capture uses explicit RAW semantics and current geometry | Splitting color kernels into stages, true float RAW source, highlight upgrades, output hashes and additional cache tiers |
| F4 | Normalized original-coordinate crop; centered 1:1/3:2/4:3 crop UI; 90° rotation, horizontal/vertical flip, reset; same geometry in CPU/GPU preview, full scopes and both export formats; persisted in adjustments and Undo/Redo | Interactive crop handles/straighten, transform/lens corrections, masks, Texture/Clarity/Dehaze, noise/sharpen, grading, healing |
| F5 | 16-bit RGBA PNG with target ICC; current and batch export; atomic QSaveFile with cancellation preserving destination; original protection; existing tiled JPEG and 1024-bin scopes retained | TIFF/WebP, proofing, waveform/parade/vectorscope, import Copy/Move and broader RAW validation |
| F6 | Shared scalar develop parameter registry used by UI and offline CLI; JSON develop.set commands; CPU JPEG/PNG export, schema inspection, invalid-command checks and existing-output refusal | Full action registry/replay, headless batch catalogs and optional MCP |

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

CLI shares the existing scalar ranges and clamps finite numeric values. It uses CPU reference processing, As Shot look resolution and camera baseline metadata; it never downloads models or opens a remote service. GUI Undo/Redo is session-local; the restored current adjustments are persisted by the existing project writer.

## Regression coverage in this batch

- Undo branching, redo invalidation, gesture boundaries and bounded history.
- Per-photo isolation, Sony As Shot/manual state, reset/paste and restored project state.
- Cache reuse, exposure-independent prepare keys, geometry/source invalidation and cancellation.
- Geometry orientation/pixel mapping, untouched original pixels, legacy default state and JSON round trip.
- Exact PNG 16-bit pixel comparison for explicit RAW and non-RAW semantics, ICC round trip, cancellation and batch PNG.
- CLI schema, finite/range validation, invalid/unknown commands, JPEG/PNG export and original/existing-file preservation.
- GUI smoke additionally rotates and undoes a photograph and exercises Undo/Redo before requiring full-resolution scopes and the GPU backend on CI.

No LightCraft source files or third-party assets are copied in this batch; these are independent C++/QML implementations of the plan's architectural ideas. Existing engine version, Sony evidence validation, GPU parity threshold, 1024 bins and monitor-ICC separation remain unchanged.
