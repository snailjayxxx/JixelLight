# F3 stage 4 — versioned scene-linear FP32 source cache (opt-in)

Development branch: `design/lightcraft-fusion-20261008`, Draft PR #16.
This is the **persistence and provenance foundation** for later native FP32
camera data, **not** a completed float LibRaw decoder. Existing real Sony ARW/
JPEG files remain on the accepted LibRaw 16-bit RGBA64 / `.jlpv` path.

## Why a separate source format is necessary

The existing `SourceCache::fileKey` deliberately includes LibRaw version,
AHD/white-balance/highlight settings and the `disk-schema-4:RGBA64-native-endian`
marker. `SourceCache::storeDiskPreview` writes 8-byte/pixel RGBA64 to a
`.jlpv` placeholder. Using that format for any future scene-linear FP32
source would silently discard RGB values >1 or <0, and bit patterns such
as negative zero. It must never be treated as a full-resolution FP32 source.

This stage adds a wholly separate opt-in format and two cache APIs:

- `SourceCache::floatDecoderSignature(configurationBytes)`: SHA-256-bound to
  the complete camera/decode configuration plus the JixelLight color-engine
  version. Consumers **must** include sensor crop, per-channel black/white
  levels, WB, camera matrix, CFA/demosaic choices, profiles and their versions
  in this configuration. An empty or oversized descriptor is rejected.
- `SourceCache::storeFloatScene(source, decoderSignature, cancel)`:
  accepts only full-resolution non-placeholder `RGBA32FPx4` input with a
  canonical 64-character lowercase source key and a canonical 64-character
  decoder signature. Rejects NaN/Infinity, RGB outside ±65536 or invalid
  alpha, non-finite dimensions and oversized frame/metadata/ICC.
- `SourceCache::floatScene(sourceKey, decoderSignature, cancel)`:
  independently validates provenance, shape, ICC, SHA-256 and complete
  pixel finiteness before releasing an immutable float image to the caller.
  It never falls back to the lossy preview or a differently configured
  decoder.

Neither operation is invoked from the current `loadSource`: a real
camera-linear float decoder needs to exist **first**. Both operations
share the existing `SourceCache` in-memory LRU budget with conventional
decoded RGBA64, under a separate namespaced composite key so RAW v16
and future float camera data never alias. An over-budget frame may still
be persisted and read, but is deliberately not inserted into the RAM LRU.

## On-disk format: JLFS v1 (`.jlfs`)

The path is `SHA256("JixelLight-FP32-scene-cache-v1" + engine version +
sourceKey + decoderSignature).jlfs`; no user-supplied string is placed
verbatim in a filename. The file contains:

| Section | Contents |
|---|---|
| 32-byte little-endian header | magic JLF1, schema 1, width, height, JSON size, ICC size, pixel byte count |
| JSON metadata | source/decoder signatures, JSON-safe capture metadata, image text fields, device pixel ratio and DPI |
| ICC | original source ICC bytes, or empty if no QImage color space exists |
| Pixel rows | exactly width × height × 4 IEEE-754 binary32 channels (R,G,B,A), little-endian, visible bytes only |
| 32-byte trailer | SHA-256 over header, metadata, ICC and every pixel byte |

Writing uses `QSaveFile` atomic replacement on the *cache* filesystem,
per-row streaming SHA-256 and cancellation before commit. On read,
size and version are checked **before** image allocation; the file hash
and decoded floating values are checked **before** memory cache insertion.
Padding from external non-contiguous QImage scanlines is explicitly
excluded from both storage and digest. Big-endian builds convert each
FP32 bit-pattern to canonical little-endian on write and restore native
bits on read. Signed zero and fully transparent hidden RGB are preserved
without color conversion or premultiplication.

Cache limits are deliberately explicit: 1 GiB maximum per frame, 128 KiB
JSON metadata, 512 KiB ICC, at most 32768 pixels per dimension and a
separate 4 GiB total JLFS LRU disk budget. Larger sources bypass this
cache rather than allocate an unbounded temporary buffer. The old
1 GiB `.jlpv` preview quota and format remain intact; neither cleanup
routine touches the other suffix. For a large high-resolution float RAW
requiring more than this budget, the future decoder must provide a
policy for bypass/redecode or bounded tile caching.

A successful store invalidates any prior FP32 RAM entry **after** the
atomic disk commit. The writer does not cache a QImage borrowed from its
caller, since that image may use external non-owning or padded scanlines;
the next `floatScene()` call reconstructs an **owned, validated** QImage
and shares it in the existing RAM LRU. Rewrites of the same identity
cannot leave stale resident float snapshots.

The old `SourceCache::put` and `storeDiskPreview` APIs now explicitly
reject FP32 sources. This prevents accidental cache collisions or lossy
conversion if an upstream decoder switches representation without
also supplying a decoder signature. Existing RGBA64/JPEG behavior stays
unchanged, including original pixel identity, catalog semantics,
GPU staging, export ICC and Sony Look.

## Tests required before acceptance

- Bitwise round trip, including RGB >1, negative colors, signed -0.0,
  alpha 0/partial/1, fully transparent hidden color and ICC/metadata/DPI.
- Padded-row QImage: only visible 16-byte FP32 pixels are persisted,
  never padding. The canonical float fingerprint is identical after reload.
- Distinct source/decoder fingerprints cannot retrieve another cached
  frame. Invalid hash inputs, future/malformed versions, appended/truncated
  data and modified payload must be rejected.
- Atomic QSaveFile cancellation and invalid-pixel/oversize metadata
  refusal cannot overwrite a valid cache or disturb existing .jlpv data.
- Memory entries are subject to one shared LRU budget; concurrent
  independent disk readers must return identical valid frames.
- Three OS CI: mandatory actual GPU precision, real Sony ARW/JPEG,
  60-second GUI, correct 1024-bin Scopes, Windows/macOS deployment
  and offline CLI. This cache is not presented as accepted without
  exact-commit complete Success.

## Still missing

Native camera-linear float mosaic unpack/demosaic from LibRaw with
black-level subtraction, WB, camera-color-matrix, sensor-white
normalization and per-channel unclipped headroom. Simply casting
LibRaw's already quantized and possibly saturated RGBA64 bitmap to
float is **not** high-dynamic-range recovery. A later F3 change must
explicitly choose how to produce trusted FP32 output, calibrate it
against held-out real RAW/JPEG fixtures, bind *every* camera/decode
setting to the decoder signature and integrate the opt-in cache into
`loadSource` only after sensor-data tests pass.

Source performance is also not validated by these synthetic frames:
large memory allocations, NVMe/disk throughput and tile cache
policies must be measured separately before default enablement.
