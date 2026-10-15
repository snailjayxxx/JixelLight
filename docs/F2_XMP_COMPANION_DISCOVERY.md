# F2.7 — Read-only XMP companion discovery (planned next incremental merge)

Implementation status: code prepared on the independent development branch, exact-head Windows/macOS/Linux CI pending. Do **not** accept, merge or modify main before full CI passes.

This extends the **existing** XmpSidecar parser and explicit user-authorized import command; it never adds another XMP decoder or silently applies external metadata.

The `XmpSidecar::discoverCompanion(photoPath)` API inspects only four literal same-directory sibling filename candidates: `file.ARW.xmp`, `file.ARW.XMP`, `file.xmp`, `file.XMP`. Canonical duplicate paths are collapsed. Only a single readable regular XMP file with finite size (0 < size ≤ 64 MiB) is preselected. Multiple distinct siblings produce status `ambiguous`; an unreadable, empty, oversized, symlink or otherwise unsafe candidate produces `unsafe`, never auto-selected. There is no directory-wide content scan, XML parsing, network access or database write merely from discovery.

`PhotoController::openXmpImportDialog` uses the unique sibling as the QFileDialog initial path. The existing separate confirmation remains mandatory, as some imported star ratings, flags, labels, keywords and albums cannot all be undone. If there are multiple/unsafe siblings the user must choose manually. Adobe Camera Raw adjustments remain unsupported, and no untrusted Develop settings are translated.

Regression cases verify no sibling, unique basename case, distinct ambiguous sidecars, unsafe empty sidecar, missing originals, and source-byte immutability. Windows/macOS/Linux core CI, deployed package smoke, source integrity and Sony/GPU guardrails must remain unchanged. This is F2 metadata ergonomics, **not** F3 sensor/calibration work.

Unfinished: case-insensitive nonstandard suffix lookup on case-sensitive file systems, XMP in-place conflict merge with a preview/rollback plan, automatic watch/refresh, and Adobe Develop transforms. Those must be separate, explicitly tested design milestones.
