# F3 Stage 6A — opt-in calibrated floating Bayer mosaic tiles

Status: implemented on design/lightcraft-fusion-20261008; exact-head three-OS CI required for acceptance.

## Scope and safety contract

This is the first independently testable layer toward a genuinely sensor-derived FP32 RAW pipeline, not a completed RAW developer or a new normal-preview option.

- SensorLinearMosaic normalizes a single U16 Bayer sensor SITE to one float, preserving negative below-black codes and values above externally calibrated white. It never invents RGB or reconstructs clipped highlights.
- A caller must supply the complete original-file SHA-256, a separate SHA-256 identifier for an external camera/mode calibration, the explicit 2x2 visible-origin Bayer CFA map (R, two greens, B), and absolute site-specific black/white codes. No LibRaw auto-white, guessed color phase, or unknown black pattern is implicitly treated as verified calibration.
- The conversion uses sensor coordinates and calculates phase relative to the visible active area's top-left. A tile must fit inside the validated active region, and outputs are row-major float CFA sites (not QImage RGBA, not a 16-bit intermediate). One tile is capped at 1,048,576 samples / 4 MiB; sensor dimension and total-site caps reject unreasonable inputs.
- The v1 decoder fingerprint includes the complete file SHA, independent calibration-reference SHA, all four CFA codes and black/white levels, and an algorithm version. It is stable across platforms, and calibration changes invalidate a later FP32 cache key.
- RawDecoder::calibratedMosaicTile is a separate explicit laboratory entry point. It hashes the original source file before opening LibRaw, checks the source identity, uses the same RAW resource budget and cancellation semaphore, unpacks without dcraw_process, rejects Fuji/X-Trans/Foveon/non-Bayer and unsupported pitch/patterns, checks the full 8-row Bayer phase and all per-site black levels against LibRaw, then converts the requested tile. It reports a changed source's size/mtime; it never invokes the normal photo UI, writes a project, or enables persistent FP32 caching.
- Only 0x0 or 2x2 repeating black-pattern shapes are supported. More complex patterns, non-green fourth CFA components, nonlinear sensor transfer functions, packed/padded planes, dual-gain, masked sensor data, multi-image RAW and calibration uncertainty fail closed or remain unimplemented.

LibRaw's author explicitly explains the black baseline plus channel correction plus repeating pattern semantics: https://www.libraw.org/node/2565. The source metadata fields and COLOR behavior are documented at https://www.libraw.org/node/31. The external white-level decision still requires actual camera/ISO/exposure characterization; linear_max and maximum must not silently be conflated.

## Verification

- Synthetic RGGB/dual-green and active-margin fixtures check exact per-site normalization, negative and >1 headroom, phase selection, source immutability, expected sample and threshold counts.
- Cache fingerprint tests change source digest, calibration digest, CFA order, black and white independently; each produces a new hash. Invalid digest, channel layout and denominators are refused.
- Unsafe geometry/pitch, oversized tiles, cancellation, null buffers, unsupported CFA and source SHA mismatch are rejected without assuming another camera format.
- Existing main RAW decoder and Sony Creative Look remain unchanged; the original GPU 40/65535 parity, exact Alpha/scopes, real RAW/JPEG GUI and deployed package gates remain mandatory in Windows/macOS/Linux CI.

## NOT complete; next stages

This does NOT implement trustworthy sensor-linear WB, demosaicing, sensor gains or ProPhoto/ICC conversion, automatic calibration, cross-camera proof, sensor highlight reconstruction, full-frame tiled stitching or SourceCache wiring. The actual default RAW path still produces LibRaw RGBA64. Stage 6B must obtain held-out Sony/DNG/CR3/NEF samples, validate phase and white/black against independent references, then implement/test demosaic and camera-to-working color conversion before any opt-in real-photo rendering; 6C can connect the validated FP32 scene to the versioned .jlfs cache and preview/export pipeline.
