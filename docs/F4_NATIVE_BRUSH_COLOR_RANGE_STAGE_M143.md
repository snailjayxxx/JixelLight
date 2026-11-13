# F4 M143: committed native brush plus colour-range baseline

This test-only checkpoint extends the separate native-brush GPU group without
changing production. An active colour-range mask still returns
`FrameFailure::CPUPolicy` in native frame admission. A fresh focused build must
retain that existing refusal before implementation. It is an unsupported-feature
baseline, not a new product regression, and does not hold the independently
qualified recovery repair at dde0abc.

Three added methods require 72 integer/FP32, encoded/linear/RAW-intent,
output/exposure/inversion combinations and 108 hue-seam, low-light, neutral,
minimum-admitted-range and viewport combinations. They combine colour and
luminance selections with ordered Paint/Erase/Paint, a negative brush layer,
white balance, exposure, spatial masks and vignette; the seam matrix also uses
grain. Each admitted case uses the unchanged actual native coverage/colour
dispatch helper, an independent owning CPU reference, finite FP32 output,
maximum RGB16 error40/65535, exact float alpha and exact1024-bin histograms of
the actual rendered pixels. Normal finish/readback/resource assertions remain.

Ten additional admission checks cover explicit non-RAW source identity,
deliberate RAW mismatch, malformed range/inversion, narrow hue/chroma/lightness,
Adobe output, Point Color and restricted-backend positive exposure. Narrow
selections and the established Adobe colour-mask precision policy must retain
CPU fallback. No precision, memory, worker or deadline budget changes.

After implementation the separate native group is expected to contain8 Qt
results and336 actual compute/reference comparisons (156 inherited,180 new).
The legacy GPU group remains82; the registry remains106 CTest groups /100 Qt
result files and18 guarded runtimes. Those are expectations, not execution
claims. Preflight must eventually require the complete enlarged native report.

Parent dde0abc has completed local106/106 in988.89s, all100 zero-failure Qt
reports and33 validation contexts with18 unchanged runtimes. Its own PR86/CI442
is still running, separately from this source. Frozen parent files, failed
ancestor evidence and acceptance holds remain untouched. This checkpoint has
not compiled or executed its new methods, full/native/platform/package gates.
FP32 fixture input is not calibrated float RAW or physical-device evidence.
