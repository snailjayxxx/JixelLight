# F4 M139: explicit source semantics in the native luminance policy fixture

The first implementation head588fc5f passes Python283 and a fresh Release
build in106.61s. The focused real-Metal run completes all96 source/output
and60 narrow/HDR/viewport comparisons: maximum RGB16 error4, exact float
alpha, exact actual-pixel histograms and released operation charges. The
inherited Paint/Erase film-grain method also passes. The policy method fails
its first accepted-plan expectation, leaving the whole run failed at5/1/0/0
in9.729s. Mandatory real-Sony/GPU preflight, targeted groups, native UI and
full CTest are not executed after this failure.

All84 original evidence files/140,966,401 bytes, every built runtime, build
graphs and Git-verified source copies remain separate and failed. The raw
focused log SHA256 is
`11a4dadd0cf9458a2393efd73ef7a9fae9d8ea1e1df90b499c61ceb0bfde664d`.

Source inspection identifies the fixture mismatch: the three-argument
`ProcessingPlan::compile` compatibility entry infers RAW from LinearProPhoto,
while this policy fixture explicitly captures a non-RAW canonical source.
The fixture now uses the explicit non-RAW overload for its accepted and
unsupported-effect cases. A separate deliberate RAW/source mismatch must
return `InvalidSource`; malformed uniforms, Point Color, colour masks, Adobe
grain and restricted-backend bright exposure keep their original assertions.
Each of the eight policy attempts now logs its actual refusal enum. The
original failed run logged booleans rather than refusal enums; its diagnosis
is source inspection, not an invented earlier runtime observation.

Production app/core/shader/UI bytes are identical to588fc5f. All156 numeric
comparisons and other assertions remain unchanged. A fresh third physical
build, all85 mandatory GPU results, local104/all98/all33, own three-platform
CI and installed88/strict delivery remain pending. The next frozen check
also guards `JixelLightGpuTests` in addition to the sixteen workflow/runtime
guards, for seventeen binaries; that is an expanded identity check, not a
new product test group or physical-device claim.
