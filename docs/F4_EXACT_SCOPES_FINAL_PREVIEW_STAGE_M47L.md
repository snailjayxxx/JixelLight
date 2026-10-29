# F4 M4.7L: start pending full statistics after the final preview

A current final preview now starts its pending exact histogram immediately.
Interactive frames keep their existing debounce. Stale revisions, preparation,
preview statistics and the absence of a pending request do not start work.
Repeated presentation cannot resubmit the same pending request. Plots retain
their histogram/completed-frame ordering and uncached/failed/cancelled fallback.

Only submission time changes; image arithmetic is unchanged. LatestJob remains
one running plus one replaceable pending request, with existing cancellation and
shared 3 GiB admission. No image buffer is added. Owned-canvas QA records read-only
phase entry times and actual histogram/plot readiness; input/capture gates stay.

## Actual local validation

The serial App/Core/scope-routing/export-verifier build passes. Fresh
**Core215/0/0** passes in **20.5681 s**. All five scope suites pass, including
**12/0/0** scheduling checks: four original completed-frame/uncached/failure/
cancelled-exact cases and six new final-current/stale/interactive/preparing/
preview/no-pending contexts. Controlled worker checks retain GUI responsiveness,
one submission, exact63,237-pixel histograms and unchanged intent/history/source.

The first Root attempt retains **Core214/1** at
`evidence/F4-M47L-root-core-race-v1-failed/receipt.json`. Its version-switch test
observed a worker cache hit before GUI publication, then compared0 with7,680.
The test now awaits the public current full-statistics status before the same
pixel comparisons, retaining its 10s asynchronous limit. The failure stays failed.
Only the Core test binary changes in the follow-up build; the five passed scope
suites are reused by byte-exact App/Core-library/scope-test/verifier proof.

All **18 actual Root UI workflows** and four CLI entry guards pass:

| GUI, ms | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Existing canvas | 14,981 | 16,197 | 15,965 | 16,909 |
| Genuine Sony pressure | 9,874 | 9,919 | 10,333 | 10,198 |
| Sony export/reference partition | 35,052.868 | 34,439.148 | 35,703.185 | 55,483.326 |

Guide CPU1x/2x passes in **3,107/3,376 ms**.
Opaque/transparent pressure retains input, cancellation, alpha, radius, locale,
Undo/Redo and export checks. Every canvas flow keeps24 old flags, pan checks,
seven capacity refusals/four recoveries and final current histogram/plot each
**2,659,021** pixels. Every genuine Sony result retains **32,947,264** source/
statistics/plot pixels and independent fresh-source PNG16/ICC SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.

Strict audit0 preserves **250 original PNGs**, reports, five binaries and
**213 tested code sources** at `evidence/F4-M47L-root-local-v2/receipt.json`.
Raw `531380d` stamps remain unchanged and require exact committed source matching.
Native captures use an owned internal-SSD directory; each original byte is copied
to physical evidence storage after existing child/wrapper timers. All captures,
45/60s GUI and120s export/reference limits remain. Daily preferences are unchanged.

## Original failure and limits

Original [CI306](https://github.com/snailjayxxx/JixelLight/actions/runs/40806921358)
on `531380d` stays **failed**. All68/all3 pass zero-fail/skip in
Linux/Windows/macOS1,169.22/1,772.75/1,419.27s. Linux/macOS strict native guide and
actual Mac24 package pass. Windows's old owned CPU canvas times out at
**45,074ms/phase41** waiting for its final waveform: all 7 refusals/4 recoveries
pass and the histogram has2,659,021 pixels; plot0/is not current. Help/package are
not reached. Encoding takes414.021ms; the earlier USB explanation cannot be
transferred, and this timeout cause remains unproven.

All4 produced validation ZIPs,3 complete actual-checkout
`104dcc0d3597d25bd5ec947dbb4a3481b77847ba` logs, matching tree
`a8a7c72f863012111cdc8b28532679b6c640eae0` and source213 remain at
`evidence/ci-40806921358/terminal-failed-v1/receipt.json`.

A separate paired private Cocoa diagnostic passes the same owned UI in
20,774/15,626ms. This is a bounded local observation, not a Windows speed or
timeout-cause proof. Earlier submission performs more cancellable background/
cache work in that burst:40 versus12 full-histogram calls and732.337 versus
411.068ms cumulative histogram work. Original failures/private reader and compile
errors remain archived.

Own committed all 68/all 3, both actual24-mode SDK-stripped packages, five original
ZIPs and three complete actual-source logs remain required. Latest fully
accepted implementation stays K/`1fbfe71`/CI304. The 32 Mi source admission,
shared 3 GiB reservation and colour/precision gates remain; reservation is not RSS.
Physical drivers/nativeOS/nativeGPU brush/nativeFP32 RAW and remainingF4/F0–F6
stay unfinished. PR16 remains Draft.
