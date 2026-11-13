# F4 M137: committed native brush plus luminance-range qualification

This test-only checkpoint defines the next extension of the existing native
committed-brush route. The production admission policy still returns
`FrameFailure::CPUPolicy` for every active luminance mask, and the native colour
shader has only the older 52-slot declaration. No production behavior changes
in this checkpoint. The focused first build must establish the current refusal
before implementation; it is not a product regression or accepted GPU result.

Three mandatory GPU methods require actual coverage and colour dispatches,
independent owning CPU-reference rendering, maximum RGB error 40/65535,
exact source float alpha, finite output and exact 1024-bin histograms of
the actual rendered pixels. They cover 96 source/output/inversion/exposure
combinations, 60 signed-HDR/narrow-shoulder/viewport combinations, ordered
Paint/Erase/Paint and a negative layer. The admission matrix retains malformed
intent, Point Color, colour masks, Adobe grain and restricted-backend bright
perceptual policies. Accounted source, work, output and reply leases must be
released after the domain shuts down; the normal route must not call finish.

The registry stays 104 CTest groups / 98 Qt result files. GPU Qt results grow
from 82 to 85 only when the new methods pass; this statement is an expectation,
not an execution claim. Existing methods and all resource/deadline/precision
limits remain. Private source frames exercise RGBA64 and explicit FP32 scene
input; they do not establish calibrated float RAW or physical device evidence.

Parent da2395c closes local full104 in 962.77s, all33 validation contexts and
sixteen runtime guards; its independent draft PR85/CI441 is still pending.
Parent acceptance cannot accept the new source. The first focused baseline,
fresh implementation build, full104/all98, all33, own three-platform CI,
installed 88-context desktop packages and strict delivery are not yet run.
