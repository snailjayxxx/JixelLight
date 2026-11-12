# F3 M9 — Lossless PNG encoding latency

This stage selects a faster lossless PNG compression setting in both ordinary and admitted-brush raster exports. It does not change source decoding, geometry, color stages, Point Color, 16-bit output samples, alpha, ICC, TIFF/WebP behavior, overwrite/preflight rules, QSaveFile atomicity, cancellation checks, render ownership/budgets or operation deadlines.

## Change and evidence required

Both PNG writers use a shared `LosslessPngCompression=35` on Qt's 0..100 scale. Qt6.8.3 maps this setting to deflate level3. This is compression effort, not image quantization; encoded file bytes can differ while every decoded channel and the profile remain identical. Existing saved PNG files remain readable, and originals are not rewritten. Rendering/cache/profile engine versions are unchanged because working/output pixels are unchanged.

A `raster_encode_commit` performance span now records format, pixel count and PNG compression setting around file open/write/atomic commit. Rendering time remains separately observable. This is intended to distinguish encoding from rendering and teardown rather than infer codec time from a whole export timer.

The implementation uses only the existing public QImageWriter API; no PNG codec code or external library is copied. The pinned SDK's [PNG implementation](https://github.com/qt/qtbase/blob/v6.8.3/src/gui/image/qpnghandler.cpp) documents the compression mapping, and [QImageWriter](https://doc.qt.io/qt-6/qimagewriter.html#setCompression) defines compression as format-specific.

An external preliminary probe uses a deterministic 6000×4000 unassociated RGBA64 gradient/noise fixture with hidden RGB at zero alpha and partial alpha, tagged Display P3. Every decoded pixel and every ICC byte is compared for every candidate:

| Qt setting | Write and commit | Encoded bytes | Complete decoded channels/ICC |
| --- | ---: | ---: | --- |
| Default | 14901.381458 ms | 125025238 | exact |
| 20 | 3755.796542 ms | 129803837 | exact |
| 35 | 4924.864166 ms | 124170151 | exact |

The selected35 setting is about3.03× faster and0.68% smaller in this single synthetic observation. This does not establish a general speed/size guarantee. Complete decoded RGBA16 SHA-256 is e6c80348547c1338abdb90ab66e671cbc102a45d519b1825aec7274175f1d8dc; ICC SHA-256 is1c885889209cbac6d1476be366fa05a7a696abd324c5c354327e0ab87ad5179a for all candidates. The probe source, original build failures, output PNGs and result JSON remain outside Git in `~/.cache/jixellight/png-encoding-probe`.

## Original failure remains distinct

PNG row-decoder QA M63 improved a different verifier. CI379 macOS passed90/90 tests but then failed its separate Sony controlled-pressure export/reference workflow. The native export consumed98.534s, leaving17.454s for independent RAW/render/complete PNG verification under the unchanged120s combined slice. The original failed log is retained (2022758 bytes, SHA-256 a0ace411f28330ca750ae5c592bafeb59835a3174f60691fe0ec78d36bb2e03a). The previous native performance snapshot records an11.828s complete CPU render. The difference motivates direct encoding instrumentation; it is not a complete causal proof.

Scoped product checks now pass: eight existing core export/refusal/cancel/geometry/ICC methods (10 Qt results), four-space Point Color exports (3 Qt results), the extended CLI and all203 Python methods. The original Sony controlled-pressure CPU and Metal-display flows also pass its unmodified GUI60s and combined export/reference120s gates. An independent complete reference now additionally records little-endian RGBA16/ICC hashes and RAW/reference/PNG decode timings, while strengthening exact ICC-byte checking. It retains the existing complete image comparison and all original deadline/budget checks.

A same-machine before/after run uses the same checksum-pinned Sony RAW, exact packed plan and all32947264 pixels. Both complete pixel hashes are38d8d85bafc7d5a00affc732b50af404b91cc3a19ae592763d966cb0e795e062; both ICC hashes arebd1d9043b41705c5f52c5ff6dd3e83aa71f08bda3c5f173b70991f5000914047. These fingerprints come from the separately rendered independent reference and complete decoded exports. Encoded files differ intentionally; the faster setting uses201326317 bytes versus192926975 in this real sample (about4.35% larger). Native export takes29044ms before/10216ms after; combined export/reference takes40815.860ms before/21125.613ms after under the same120000ms gate. The Metal-display counterpart also passes with21575.523ms combined,10052ms native export and the same complete pixel hash. This is one measured source/settings pair.

Both former selected-input paths also pass: stabilized CPU combined21089.053ms and explicit one-pixel CPU21026.017ms, with complete independent pixel/ICC proof. Actual CPU/Metal Point Color Main App workflows also retain complete staging/history/scopes/export verification under the new encoder.

Exact clean92 regression, frozen-head actual native reruns and all platform/package delivery gates are still required before acceptance of this stage. No successful fix for the original cloud deadline is claimed at this preliminary source state. No parity/budget/deadline/assertion is relaxed.
