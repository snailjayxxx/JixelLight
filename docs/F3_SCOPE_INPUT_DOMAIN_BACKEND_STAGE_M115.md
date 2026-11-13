# F3 scope input-domain backend — M115

This is an unexposed backend stage. Source/Working/Display selection is not yet a user-facing feature; cache, controller, UI and admitted float-brush integration remain pending.

`ImagePipeline::analysisRegion` supplies Source and Working as unassociated RGBA32FPx4 in linear sRGB/D65. Source is the decoded color converted to those primaries before edit/spatial operations. Working captures the existing `perceptual_look` boundary, after working edits but before output curves, output encoding, LUT, grain and output-detail stages. Display delegates to the existing RGBA64 renderer. Geometry is supplied by the caller. Neither linear domain applies monitor ICC. Source/Working retain float alpha and finite signed/over-white values at their specified boundaries; existing perceptual-stage range policy is unchanged.

The historical Display implementation remains its own compile-time specialization. Source skips edits; Working uses the existing spatial input and halo calculation. Invalid float sources, unknown domains, cancellation and empty regions refuse. Direct Working analysis with active brushes refuses with an admission error until float-output accounting/leases are integrated; it never bypasses the existing brush limits. Real RAW decoding remains LibRaw RGBA64, and this API does not claim native-FP32 RAW demosaic.

Four new Core cases cover Source invariance and hidden/HDR/negative pixels, Working exposure and output-stage exclusion with exact alpha, spatial ROI/whole consistency and refusal boundaries. Expected Core259. C++/GPU/native/default-output compatibility checks remain pending at this snapshot; no full102/platform qualification is claimed.

The private backend was paused for the Windows proof-path correction. Its original four files and diff are preserved byte-for-byte when its base advances frombf7d387 to57cba07, after that parent completes local full102/928.45s/all33/thirteen binary guards. CI432 remains independent; original CI430/431 Windows failures stay retained. This branch has not merged main or protected PR15.
