# JixelLight

JixelLight is a cross-platform desktop photo editor for Windows and macOS, designed around fast batch photography post-processing rather than single-image compositing.

Current version: **v0.1.0-alpha.1**

## What works in this alpha

- Import multiple JPEG / PNG / BMP / TIFF images through Qt image codecs.
- Fast preview pipeline using a 2048 px working preview while preserving the full-resolution source for export.
- Non-destructive adjustment state per photo.
- Exposure, temperature, tint, contrast, highlights, shadows, whites and blacks.
- Professional Scopes foundation with **1024-bin** RGB overlay + luminance histogram.
- Shadow / highlight clipping percentages.
- Copy adjustments, paste adjustments, and sync current adjustments to all imported photos.
- JPEG export from the full-resolution source.
- SQLite project database foundation.
- Session logging, action trace ring buffer, crash marker, one-click Bug Snapshot / Diagnostic ZIP.
- GitHub Actions build matrix for Windows and macOS.

## Not implemented yet

This is intentionally an alpha vertical slice, not a claim that the complete v0.1 roadmap is finished.

- RAW decoding (LibRaw integration is next).
- Exiv2 metadata pipeline and full EXIF browser.
- LittleCMS / ICC color-management pipeline.
- GPU compute implementation of processing/scopes (the engine boundaries are separated so Qt RHI can replace the CPU reference path).
- Crop UI, tone curve, RGB curve, HSL, color grading, local masks and retouching.
- Thumbnail cache and large-library virtualization.

## Requirements

- CMake 3.24+
- C++20 compiler
- Qt 6.5+ with: Core, Gui, Quick, Qml, QuickControls2, Sql

## Build

### macOS

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
open build/JixelLight.app
```

### Windows (PowerShell)

```powershell
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release --parallel
.\build\Release\JixelLight.exe
```

Depending on the generator, the executable can also be at `build/JixelLight.exe`.

## Diagnostic workflow

Use the **🐞 Report current problem** button in the toolbar. JixelLight writes a ZIP into the user's Downloads folder containing:

- `manifest.json`
- `actions.json`
- `current_preview.png`
- current session log (when available)

The action trace keeps the latest 1000 actions in memory, comfortably above the design requirement that Bug Snapshot preserve at least the latest 500 actions.

## Architecture

See [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

## Project status

The repository follows the JixelLight product/technical plan v0.2 stored in the project's Google Drive design folder. The implementation deliberately starts with the final Scopes and Diagnostics boundaries so temporary code does not become permanent architecture.
