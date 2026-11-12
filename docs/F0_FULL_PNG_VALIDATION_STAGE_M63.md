# F0 M11 — faster complete PNG evidence decoding

The independent PNG verifier spends considerably more time checking full-window screenshots than the App spends performing a native workflow. One observed dehaze invocation takes about25s overall while its native operation takes about4s. This change speeds the verifier's reconstruction loop while keeping every pixel, CRC, ICC byte and existing rejection bound.

## Reconstruction and unchanged checks

None rows need no predictor loop. Sub and Up use widened integer byte lanes: a modulo256 sum cannot carry into the next lane, and Sub reconstructs its channel-byte prefix in doubling steps. Average retains exact integer-floor arithmetic. Paeth moves filter selection outside the byte loop, uses exact flat-neighbor shortcuts and keeps the original left/above/upper-left tie order. Filtering remains byte-wise for16-bit samples, as specified by [PNG filtering](https://www.w3.org/TR/png-3/#9Filters).

A source comparison restores the original reconstruction loop and removes only the new helper/mask, then requires byte-for-byte equality with the entire original script. All parse bounds, CRC checks, complete bounded zlib decoding, ICC checks, unknown-filter refusal, native-endian pixel hashing and output metadata code are unchanged. Limits remain32MiB input,64MiB inflated data,8192 dimensions and2MiB ICC. No external numerical/image library is introduced. Temporary integer work is bounded by the current row.

## Evidence

Four new unittest methods cover all five filters, RGB/RGBA at8/16 bits, one-/two-/three-pixel and wider rows, explicit unequal-predictor Paeth ties, full8192-pixel carry cases and known source bytes. They also retain ICC, CRC, shape/depth/interlace, malformed zlib, truncated/overlong data and trailing-stream refusals. All197 Python methods pass. Both desktop/Linux workflow sections add the new test command; every original workflow line, product command, path and deadline remains in order.

One local comparison decodes the same2360×1560 RGBA8 screenshot with old and new implementations:3267.4ms versus1089.0ms. Its original2/3/4 filter mixture is retained. A307×211 RGBA16 export takes58.65ms versus7.87ms. Both comparisons require identical full pixel bytes and metadata, including source SHA, ICC, filters, complete row count and native-pixel hash. The sources remain unchanged. These single M2 observations run while product CTest is active and are not a cross-platform speed guarantee.

All36 retained source, geometry, color-reference, full-window and export PNG proofs from actual CPU/Metal dehaze plus original clarity/texture Metal workflows are rechecked. Every metadata field and complete decoded/native-pixel hash matches the previously frozen records. Their full-window captures are not cropped, scaled or replaced.

Actual CPU/English and Metal/Chinese dehaze App invocations also pass with the changed verifier and the same parent product binary561767a. One scoped comparison takes25,662/17,205ms with the old verifier and11,519/8,589ms with the new one; all workflow, cache, pixel/ICC, frozen-ZIP and ownership checks remain mandatory.

The product source is inherited from GPU-dehaze561767a. Tooling changes are separate from its clean90-test and native acceptance identities; current platform/package acceptance remains required. No image-engine speed, broader photographic quality or whole F0–F6 completion is inferred from verifier timings.
