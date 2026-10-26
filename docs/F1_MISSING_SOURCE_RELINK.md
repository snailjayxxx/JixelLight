# F1 — retain missing sources and relink their version family

Candidate implementation on `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Local acceptance is recorded below; this stage requires its own final-tree three-platform and deployed-package run.

## Visible behavior

Opening a project now retains missing/unreadable originals and their virtual versions in catalog order. Library, filmstrip and navigator display a missing-source placeholder. Selecting one clears previous preview/GPU/scopes pixels and metadata; it does not decode a different row. Version names, stored edits, complete Undo/Redo history, ratings, flags, keywords, albums and dates remain available to persistence. Users can rate/tag missing versions and create, rename or remove their virtual copies. Develop mutations and image export require a readable source; history can still be viewed. Importing a Look profile on a missing row rejects safely.

A selected export containing a missing source refuses the entire plan before queueing. Unselected missing sources do not block an available selected export. All-catalog export refuses until all its sources are available; it never silently omits missing versions.

**Relink source…** is available from the project menu and missing-source UI. The file picker captures the exact version key and catalog generation when opened, so accepting it after switching projects cannot target a new project even if both projects contain the same source key. Background validation previews old/new paths and the complete affected version count. The request remains bound to that source family if the live selection changes.

For a known source identity, the candidate must match its complete SHA-256 and byte count. A legacy catalog without this identity cannot prove that a selected file is the original: the dialog says so explicitly and requires the user's checkbox acknowledgement. Successful relinking establishes that file as the family's baseline. This is content identity, not camera authenticity, capture matching or photographic provenance. Filenames, sizes, timestamps and partial cache keys do not establish identity.

## Validation, cancellation and cache lifetime

Each validation/confirmation worker hashes the complete candidate, decodes it through a fresh cache-bypassing source load, then hashes it again. The confirmation repeats this work against the identity shown in the preview; changing the candidate between preview and confirmation refuses the request. Only readable regular photographs up to **2 GiB encoded file size**, supported by the existing image/RAW decoder and its memory gates, are admitted. No source file is written.

Initial validation releases decoded pixels before waiting for the dialog. Confirmation retains its verified full source only through commit/publication. `LatestJob` provides one active worker plus one replaceable pending request; cancellation/project switches invalidate delivery. A changed/deleted version family, stale request ID or catalog generation refuses confirmation. Export and copy-import operations must be idle at confirmation, and dirty controller edits are flushed before the SQL transaction. The verified source key is checked again on delivery and after flushing so an ordinary change while queued cannot bind old pixels to a new cache identity.

After the SQL commit, all live family members adopt the new source path while retaining their independent edits/history/curation. Old source/preview/scopes generations are cancelled. The confirmed image enters the existing source cache under an explicitly invalidated key; validation never substitutes a cached image. The old disk preview is removed and a per-path in-process revision makes old in-flight keys unreachable. Thumbnail URLs/cache keys receive a family revision, including when the candidate has the same path, size and mtime. Windows cache/family comparisons fold path case while persisted user path spelling remains intact.

Complete hashes surround the decoder, but filesystem path mutation and a SQLite transaction are not one atomic filesystem operation. An external process can still change a file after the final verification; users must revalidate/relink a subsequently changed source. Cancellation is cooperative between reads/decoder checks and cannot interrupt an OS/NAS read that is itself blocked. No real-NAS cancellation latency is claimed by local synthetic tests.

## Catalog transaction and compatibility

The optional schema-1 `original_identity` table stores source key, byte count, full SHA-256 and nullable original sort key. Read-only snapshots and opening legacy projects do not create this table. Its first write uses the existing WAL-aware `VACUUM INTO` backup before migration. Invalid/future identity schemas refuse to open without rewriting records.

Background identity collection records currently readable imported/project sources without another image decode. Controller dirty rows are published before their identities are written. A saved baseline is immutable on a mismatch; failed persistence does not manufacture a successful baseline. A legacy source with no saved identity remains unknown when unavailable.

`ProjectDatabase::relinkSource` performs one SQL transaction: revalidate the source family and target conflicts; update an original `photos.path` and its curation/tags/dates keys if an original exists; update every associated `virtual_sources.source`; migrate/create the family's identity; commit. Adjustment/history JSON, timestamps, virtual UUIDs, version names and annotation values remain unchanged. A retained sort key prevents a renamed original from moving among same-second imports after reopening. Virtual-only families, relative legacy keys and mixed relative/absolute mappings are supported. Ambiguous original families, another family's target, conflicting baseline identities and orphan target annotations refuse instead of merging. Every SQL mutation failure rolls the whole transaction back and preserves the live controller paths. Source-family normalization is lexical and works when originals are missing; Windows comparisons also handle case aliases without lowercasing stored destination paths.

An original's storage key changes to its new path on success. An export dialog holding the old original key must be reopened; virtual-copy UUIDs remain stable. There is no automatic folder-wide search, similarity matching, bulk relink, source move or delete.

## Required regression evidence

`JixelLightProjectRelinkTests` covers real temporary-file move/rename and full-family updates; byte-exact adjustment/history and metadata rows; same-second ordering; virtual-only legacy confirmation/baseline; relative/absolute families; target/orphan/invalid-identity refusal; SQL triggers failing every mutation phase with complete rollback and healthy retry; legacy read-only behavior/WAL backup; and platform-specific case-family/path-spelling behavior.

`JixelLightMissingSourceTests` covers missing original/copy visibility and save/reopen with redo retained; missing Develop and valid-LUT import refusal; missing selected/all batch atomicity plus successful available-only selection; known identity family restore after changing selection with exact PNG16/Display P3/geometry output; full-hash refusal of changed middle bytes; same-path/size/mtime cached-old-image regression; unknown acknowledgement; changed candidate, cancellation, cross-project picker generation and deleted-version requests; and recording a new import's identity after its controller dirty rows are actually persisted.

The independent `--relink-smoke-report` uses only generated PNGs, native Qt pointer clicks and a compact **1180×720** window. It requires missing placeholders/cleared image state, real Cancel, disabled confirmation before unknown-identity acknowledgement, real checkbox/Confirm clicks, captured family despite changed selection, restored two-version export, project reopen and unchanged source SHA. Three screenshots are mandatory and inspected. It explicitly reports `file_picker_automated:false`: the native file selection itself is covered by the captured-key/generation boundary and controller tests, not claimed as automated. CI runs it beside the existing mask GUI smoke, and `ValidatePackage.py` adds a mandatory seventh `source-relink` check using the actual deployed app with development Qt paths removed.

The original **60,000 ms real-RAW GUI**, all **23** existing screenshots, **40/65535 GPU** parity, full scopes and **120 s** test limits remain unchanged.

## Local acceptance

- Initial database/controller suites passed; after independent review, the baseline-write, missing-LUT, picker-generation and final-key regressions were added.
- Complete local CTest passed **12/12 in85.90s**, including **209 Core /36 actual Metal GPU /39 performance /9 project-relink /9 missing-source** (Qt totals include initialization/cleanup), mandatory real Sony and DNG, zero failures/skips. Logs: `build-local/local-missing-source-tests.xml` and external `missing-source-full-tests.log`.
- Final compact UI runs passed offscreen in **424 ms** and native Cocoa in **788 ms**, all required flags, three saved screenshots and zero QML markers. Missing, confirmation and restored screenshots were inspected; the missing-source action has its own row.
- Serial real-DNG CPU/native-Metal GUI passed in **14,603 /15,206 ms**, each all23 screenshots, **10,340,960** final full scopes, selected-export assertions and QML0 under the unchanged60,000ms gate. Reports: `build-local/local-missing-source-gui-{cpu,metal}` and `local-relink-final-{offscreen,cocoa}.json`.
- A small follow-up clarifies that missing counts represent versions sharing missing sources. Final combined implementation with portable Look commands, its own three-platform CI and seven-mode deployed-package acceptance remain **pending**.
