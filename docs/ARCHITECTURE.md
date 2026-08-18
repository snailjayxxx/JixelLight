# JixelLight Architecture — alpha.1

## 1. Goals carried directly from the product plan

JixelLight is structured as a non-destructive, batch-first editor. The original image is never modified. Each asset owns an `AdjustmentState`; preview and export are products of the source image plus that state.

The first implementation keeps two design commitments from day one:

1. **Professional Scopes are a real engine boundary**, not a disposable histogram widget.
2. **Diagnostics are part of product architecture**, not debug print statements added later.

## 2. Current runtime flow

```text
Image file
   |
   +--> full resolution QImage ---------------------------> JPEG export
   |
   +--> max-2048px preview
              |
              v
        ImagePipeline (CPU reference)
              |
              +--> ProcessedImageProvider --> QML preview
              |
              +--> ScopesEngine (1024 bins)
                         |
                         +--> RGB histogram
                         +--> Luma histogram
                         +--> clipping diagnostics
```

The CPU implementation is intentionally isolated behind `ImagePipeline` and `ScopesEngine`. A later Qt RHI compute implementation can keep the controller and QML interfaces stable.

## 3. Adjustment model

`AdjustmentState` currently stores:

- exposure EV
- temperature
- tint
- contrast
- highlights
- shadows
- whites
- blacks

Each imported photo owns an independent state. Copy/paste and batch-sync copy state objects rather than destructively modifying pixels.

## 4. Scopes engine

The scopes engine accepts a display-referred processed preview and produces:

- 1024-bin red histogram
- 1024-bin green histogram
- 1024-bin blue histogram
- 1024-bin Rec.709-style luminance histogram
- shadow clipping percentage
- highlight clipping percentage

The API is deliberately resolution-independent so 256 / 512 / 1024 bins can later be selectable without rewriting the UI.

## 5. Diagnostics

### Logging

Qt messages are redirected to a per-session log in the platform application-data directory.

### Action Trace

`ActionTrace` is a ring buffer of the latest 1000 user/system actions. Examples include image import, selection, parameter updates, sync, export, and Bug Snapshot creation.

### Bug Snapshot / Diagnostic Bundle

The one-click diagnostic bundle is a valid store-mode ZIP written without an external compression dependency. It contains the current state, action trace, current processed preview, and session log.

### Crash Reporter foundation

`CrashReporter` installs a terminate handler and common fatal-signal handlers. Platform-native Windows minidumps and richer macOS stack integration remain roadmap items.

## 6. Project database

`ProjectDatabase` creates a SQLite `Project.db` with WAL mode and tables for metadata, photos, and adjustment JSON. The alpha UI can create a project folder and persists imported files/current states there.

## 7. Next engineering targets

1. Integrate LibRaw and create a float/half-float image buffer.
2. Add Exiv2 metadata ingestion.
3. Introduce LittleCMS and explicit working/display color spaces.
4. Replace CPU preview/scopes hot paths with Qt RHI compute while keeping the CPU path as a correctness reference.
5. Add thumbnail/cache workers and large-library model virtualization.
6. Complete v0.1 crop and project restore flows.
