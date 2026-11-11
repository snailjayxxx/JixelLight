# F4 M5 deterministic film grain

The Develop panel now offers bilingual Amount (0–100), Size (.5–4) and Reset
controls. One slider gesture makes one history entry. Grain participates in
per-version history, projects, virtual copies, effects-only paste/sync, named
and portable presets, explicit XMP snapshots, CLI commands and all exporters.
`develop.set` exposes `grainAmount` and `grainSize`; `grain.reset` preserves
every other adjustment. The shared scalar registry now has 17 parameters.

The implementation uses a fixed-seed unsigned integer hash and smoothly
interpolated monochrome value noise. A 512/size lattice on each normalized
post-crop/orientation frame axis anchors the texture across full frames,
viewports and row strips. It does not generate new random values on redraw.
Amplitude fades from one to two lattice cells per output pixel and is zero
above that range, so small previews suppress unresolved fine texture. This is
a frame-relative synthetic grain effect, rather than a physical film model.

The encoded-RGB delta follows output transfer and Sony LUT application and
precedes quantization and existing Sony spatial detail. Mean-brightness taper
protects encoded black/white endpoints; alpha stays unchanged. CPU, normal
QRhi color compute and the admitted native Paint/Erase shader use the same
field. No grain frame or additional full-frame buffer is allocated.

The expanded signed/HDR FP32 fixture exposed a 79/65535 difference at an
Adobe RGB gamut edge. The original failed report is retained locally. Active
grain with Adobe RGB therefore uses the established CPU-reference upload on
every QRhi backend; native brush admission refuses that combination with
`CPUPolicy`, preserving the complete CPU fallback. The original 40/65535 RGB
limit and exact-alpha requirement remain. sRGB, Display P3 and ProPhoto RGB
still exercise actual compute. This is explicit scene-input testing, not a
native floating-point RAW decoder claim.

Default amount 0 follows the original arithmetic and canonical snapshot.
An inactive nondefault size is retained as editing intent. Active state uses
the optional strict `grain` schema 1 section. Malformed, redundant, unknown
or out-of-range sections reject project opening before replacing the current
writer; effects transfer validates before changing any group. Existing engine
and Sony profile identifiers remain unchanged.

Diagnostics keep the original nine color boundaries and hashes when grain
is off, adding a final `grain` boundary only when active. Dependency metadata
records the field, preview filtering, operation order and Adobe RGB policy.
The packed plan grows from 51 to 52 vec4 slots; render/statistics cache keys
already include every slot. Monitor ICC remains presentation-only.

Local validation uses Qt 6.8.3, Cocoa/Metal on Apple Silicon. The original
SDK-26 AGL link failure is retained; the successful local configure resolves
the real AGL framework from the installed macOS 15.4 SDK. No repository SDK
override, math change or replacement framework is introduced.

The new `film-grain` CTest is appended after the existing 76 tests. Its 11
observations cover fixed-noise values and default identity, alpha/endpoints,
serial/parallel/region agreement, all histogram channels and three plots,
viewport anchoring, cancellation, strict snapshots and failed-open writer
preservation, presets/XMP/history, 12 PNG/TIFF/WebP ICC round trips, actual
controller persistence/copies, and real 300/350-pixel panels at height 780.
The real panel is hosted with Main.qml's Fusion palette; native pointer input
changes Amount, Reset and Undo in both languages. This is a focused component
window, not an additional normal-MainApp or packaged-GUI acceptance claim.

The GPU additions cover integer and signed/HDR FP32 sources, four output
spaces, full/viewport fields, exact output histograms, truthful Adobe RGB
fallback diagnostics and actual native Paint/Erase composition. CLI safety
also decodes deterministic repeated PNG16 output, isolated reset and unchanged
source bytes. Workflow uploads retain the new grain screenshots alongside
the existing reports. Current three-platform and shipping acceptance remains
required after publication; previous CI362 accepts the unchanged parent only.

Final related local reports pass Core217, Performance39, GPU38 and the new
film-grain11, each with zero failures/skips/blacklisted observations. The last
four CTests complete in 41.97 seconds. Among 40 new output/viewport cases,
30 actual-compute cases have maximum RGB error 2/65535 and ten Adobe RGB cases
use the explicitly asserted CPU fallback. Both native Paint/Erase cases have
maximum RGB error 1/65535 and exact alpha; Adobe native admission is explicitly
refused. Existing brush Paint/Erase HDR and display-color tests also pass.
The CLI validator returns `ok: true`. The test Sony profile is a byte-identical
copy of the existing local fitted fixture, not a fresh fit or new evidence.

Original local SDK-link, outdated 15-parameter expectation, source-white
endpoint assumption, QML-host MOC failure, Adobe RGB 79-unit failure and the
first CTest profile-location refusal remain in the separate original logs.
The endpoint test now verifies actual encoded white after the existing
shoulder. The component host uses a separate QML fixture. No failure is
reclassified as a successful run.
