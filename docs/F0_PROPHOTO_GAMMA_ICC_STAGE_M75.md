# F0 M75 — ProPhoto ICC transfer matches the existing renderer

The CPU and GPU output encoders already use pure gamma1.8 for ProPhoto. Qt6.8.3's named ProPhoto/ROMM profile instead declares a linear toe. An independent LittleCMS TRC measurement at encoded .01 decodes the old profile to .000625, while the renderer's actual gamma1.8 means .000251189. Thus dark ProPhoto output values could be interpreted too brightly by ICC-aware consumers.

The ProPhoto output profile now uses the same standard ProPhoto primaries with an explicit gamma1.8 transfer, described as `ProPhoto RGB (gamma 1.8)`. Pixel math and values are not changed. sRGB/P3/Adobe output profiles, input decoding, Sony fitting, source files, packed plans and the engine compatibility identifier remain unchanged. Existing exported files are not rewritten; the correction applies to newly produced ProPhoto output.

A new independent core check decodes all three ICC TRCs at dark/midtone values and compares them with gamma1.8. It also decodes the actual CPU-rendered ProPhoto samples back to known linear levels, including .0001/.0005/.001, and verifies an actual PNG16 export's exact pixels and corrected ICC bytes. The new check plus the existing output-profile/conversion checks pass5 Qt results in70ms before freezing. Initial fixture compilation and accidental legacy RAW-overload use are retained as failed development attempts; the fixture now explicitly requests non-RAW processing. No image tolerance is widened.

Only the profile factory, one core regression method and documentation are staged. Core count becomes220; GPU remains82 and CTest remains95. The Adobe color-mask precision policy in parent31e3954 is independently staged. Soft-proof core/UI work remains outside this commit.

Fresh exact-head full95/native validation, GPU preflight, all three platform jobs and both84-context packages remain required. No old result is substituted for this head's acceptance, and no merge/release is performed.
