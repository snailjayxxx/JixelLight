# F4 M4.7D: continuous Paint/Erase canvas feedback

Dragging now shows the current exposure-brush stroke before release. Esc removes
that temporary picture; releasing still applies one normal history edit. The
project document, export state and full-source scope requests remain committed
while drawing. Paint/Erase layer rules and stored-stroke operations are unchanged.

A separate queue finishes one active immutable snapshot and replaces one pending
snapshot, so continuous input can show completed stroke prefixes. Context and
monotonic sample checks reject older gestures, catalogs, photos, sources, edits
and prepared views. Completing a sampler copy includes the visible tail without
changing the final stroke's spacing. A cleared or cancelled preview releases its
operation leases, including the result stored in an idle Qt future.

The versioned image-provider slot reuses the existing monitor transform and
retains leased processing pixels through display aliases. Monitor changes rekey
presentation without editing processing pixels. The canvas uses one visible
photo plane at a time, preserving alpha composition; the committed CPU/GPU view
returns when the temporary image is cleared. The preview uses the current normal
prepared source and normalized frame, with the existing admission and shared
operation budget. This does not add a process RSS cap or a native GPU brush.

## Local compiled validation

The actual root App is built with parallel1. Three new120s test shards pass
queue/prefix and cancellation behavior, independent monitor RGB/alpha and lease
lifetimes, and controller catalog-epoch0/history/scope-sequence isolation plus
final sampling/Undo and viewport/catalog cancellation. These internal checks
are accompanied by actual Cocoa pointer and displayed-pixel checks.

| Native continuous Paint/Erase | CPU ms | Metal ms |
| --- | ---: | ---: |
| Owned1x | 6725 | 6540 |
| Owned2x | 7590 | 7500 |
| Genuine public Sony1x | 17980 | 16733 |
| Genuine public Sony2x | 18656 | 18599 |

All eight workflows keep the native pointer held while input continues, observe
Paint/Erase RGB changes away from the guide, then verify Esc restores both
pictures without altering document/history/revision. Original release, single
Undo, selected deletion, project reopen, locale/scope/manual-scroll checks remain.
Twelve controls-only PNG crops are verified per run, including two live poses;
whole new Sony photo PNG bytes remain in memory. Full32,947,264-pixel Sony
statistics, original45s/60s deadlines and3GiB operation budget are retained.

All twelve previous owned/Sony/RAW1x/2x CPU/Metal workflows pass. RAW retains
all23 screenshots, full10,340,960 pixels and60s:14916/15144ms at1x and
20022/21058ms at2x. Twelve exact/symlink/hardlink refusals against a copied
pinned Sony source include both new live-crop aliases. Genuine/copied originals
and daily preferences keep their hashes. Maximum operation reservation observed
in the local native evidence is1,726,695,808 bytes under3,221,225,472; this is
not process RSS or a new claim about monitor-buffer accounting.

An original local matrix attempt is retained as failed because the physical Qt
base DPR changed2->1 during capture: native functional checks passed, but the
requested1x capture check correctly rejected0.5. A fresh QWindow probe measured
base1 and verified factors1/2 produce actual1x/2x. The subsequent actual capture
metadata passes every new1x/2x check; no product source or acceptance threshold
changed for that environment correction. Older screenshot dimensions are also
checked against their source-defined logical widths; the current screen can
constrain window height.

Full local65/65 passes in674.92 seconds with zero failures/skips. Core215,
GPU36, performance39, Look21 and admission17 checks pass, together with all
three new queue/display/controller shards. All18 admitted GPU rows retain
max0/40; both paint-only/erase export families and complete real-source scopes
keep their exact-pixel/ICC and numerical predicates. New-shard original1.52s
reports are archived before the full run. All source and original/preference
hashes match. An auxiliary audit-extraction setup error is preserved separately;
it did not fail or rerun the passed full suite.

Evidence is in the physical build parent's live-brush-native-v2/,
live-brush-legacy-gui/, live-brush-native-alias-refusal/,
live-brush-local-post-audit.json and the full65 logs/XML. Local source stamps
are preserved as the precommit c656b13 and require exact tested-source links to
the eventual commit. The accepted C run and original DPI failure stay preserved.

## Publication gate

D requires its own committed65/all3/Core215/new3 evidence plus both actual15-mode
SDK-stripped packages, five original ZIPs, three full job logs and actual checkout
tree proof. Local model/API success alone is not native UI or aggregate acceptance.
Pressure-device input, native GPU brush, native FP32 RAW and remaining F0–F6
remain unfinished. All source32Mi/research2Mi/tile8Mi/work96Mi, precision,
owned45000ms/RAW-Sony60000ms and performance120s gates remain unchanged.
PR16 remains Draft; main and PR15 are untouched.
