# F4 M4.7I.1: count completed-frame scope plots in one traversal

The original I Windows run reaches the full Sony waveform but completes GUI
validation at60371ms, exceeding the original60000ms limit. Its failed report
and complete source-proven CI evidence remain unchanged. The final statistics
stage includes12.13s for the full histogram and6.59s for the three encoded scope
plots. H `de2cfd9` / CI40688164562 remains the latest fully accepted checkpoint.

`renderEncodedScopePlots` now reads an immutable completed RGBA64 frame once
while updating the existing waveform, parade and vectorscope counts. Integer
luma levels, horizontal bins, double-precision chroma arithmetic, rounding,
pixel totals and display-ink generation retain the individual reference modes'
algorithms. Row cancellation remains mandatory and discards all partial results.
The existing three count buffers are reused without another full-frame image.
Cache keys, leases, admission, shared3GiB limits, streamed fallback, RAW decoding,
color kernels and brush sampling are unchanged.

The existing core regression compares the completed-frame batch against the
individual reference modes across18 raw/geometry/look combinations. Additional
single-column, padded-row, extreme-channel and transparent hidden-RGB cases
check exact plot images, pixel totals, source bytes and cancelled results. The
Qt test count remains215; assertions and original tolerances are retained.

An isolated CPU comparison also checks all three images at8192×4096 pixels
(32Mi), source SHA and cancellation in both execution orders. The fused path
takes425.210/424.484ms versus1772.744/1758.672ms for the individual modes on this
Mac. This is a synthetic encoded-frame microbenchmark, separate from genuine
Sony RAW/UI acceptance. Its original binary, source, reports and initial
standalone build failure remain at
`evidence/private-scope-plot-I1-v1/receipt.json`.

## Local validation and retained failures

A serial production App/Core/scope/admission/export-verifier build passes.
Nine focused CTest cases pass in107.05s with zero failures/skips: Core215,
admission17, scheduling6 and the six other scope/cache/controller/native/large
Paint/Erase groups at3 each. The pinned genuine Sony full32,947,264-pixel
histograms/plots retain exact reference comparisons and shared budget checks.

The first actual UI matrix has six successes, then transparent CPU2x fails the
unchanged canonical Erase-stroke predicate at phase12,5893ms, before requesting
the optimized full-resolution plots. Its original13 captures, report, source,
binary and private output are preserved at
`evidence/scope-plot-I1-root-native-v1-original-failed/receipt.json`.

The native QA now records the compared canonical documents, oracle, drawing
state, cursor and editor radius when this predicate fails. The strict predicate
is unchanged. Twelve targeted transparent CPU2x repetitions pass; the original
failure has not been reproduced or explained. These diagnostic runs do not
establish full matrix acceptance. Their original evidence is retained at
`evidence/scope-plot-I1-transparent-diagnostic-v2/receipt.json`.

The diagnostic change touches only the owned native QA helper. The four binaries
used by the nine numeric cases and the core library retain their exact hashes; their original
reports and original source fingerprints are preserved without retagging.
A fresh complete production-wrapper matrix passes all12 wide opaque/transparent/
Sony CPU/Metal1x/2x workflows. Eight owned flows retain80 actual radius-key cases,
48 held/cancelled pen guards,24 bilingual radius captions,48 statistics-language
checks and17 captures each. Four genuine Sony flows retain11 controls-only crops
each, the complete32,947,264-pixel histogram/plots, original60/combined120-second
limits and separate fresh-source PNG16/ICC verification.

| Genuine Sony Root validation | CPU1x | Metal1x | CPU2x | Metal2x |
| --- | ---: | ---: | ---: | ---: |
| Complete GUI, ms | 10847 | 10181 | 11910 | 12512 |
| Combined export/exit/reference, ms | 36995.817 | 36887.464 | 37794.050 | 35830.844 |
| Encoded three-plot counting, ms | 321.131 | 305.528 | 307.919 | 324.987 |

All four exported PNGs retain SHA256
`182eab83ab8b58a533c1a9bd7b59ceb6d21f3f61873cf26fe05fe888fc326baf`.
Peak request reservation is1,770,329,984 bytes under3GiB; this is not RSS.
Maximum live/committed display difference remains1/255 within the original2/255.
All210 native-tested source files, both native binaries, source ARW and daily
preferences remain fixed. The original `e0405de` stamps stay in the reports;
publication uses exact source fingerprints rather than rewriting them.

An independent strict audit exits0 and archives the original12 native reports,
180 captures, nine numeric cases, tested binaries and source mapping at
`evidence/scope-plot-I1-root-local-v2/receipt.json`. The original failed matrix
and targeted diagnostic repetitions remain separately preserved. Fresh passes
do not explain the original phase12 failure.

## Committed acceptance pending

This follow-up still requires its own68-test Linux/Windows/macOS run, all six
native pressure flows, both actual23-mode SDK-stripped packages, original ZIPs
and full logs proving actual job checkout trees. In particular, local Mac
timings do not establish a Windows deadline repair. The original45000ms owned
GUI,60000ms Sony GUI and combined120000ms export/reference limits are unchanged.

The original I failure and the unexplained local Erase predicate failure remain
visible. Physical tablet validation, native GPU brushes, native FP32 RAW and
remaining F0–F6 are unfinished. PR16 stays Draft; main and PR15 are unchanged.
