# F4 M4.7E: transparent GPU presentation and stable full-scope validation

Semi-transparent images could change brightness when a live brush picture returned
to the committed GPU view. An additional native test with rotation, crop, a panned
200% view and an owned RGBA64 image measured a 90/255 screen difference, while
every prepared processing pixel matched an independent reference exactly.

The GPU item now enables Qt Quick alpha composition. A separate scene-graph draw
premultiplies monitor-device RGB after the existing monitor transform and keeps
alpha unchanged. Its pipeline cache includes the presentation mode. Ordinary
diagnostic draws retain straight RGB; the working output texture and display
shader are unchanged. This follows the [Qt Quick RHI alpha contract](https://doc.qt.io/qt-6.8/qquickrhiitem.html#alphaBlending-prop).

The existing display-ICC test remains. A new test checks all256 pixels at four
alpha levels, three independent analytic monitor LUTs, and18 alternating straight
and premultiplied draws on one stable target/pass. It requires unchanged working
output bytes, preserved hidden RGB in straight draws, zero transparent device
RGB, unchanged opaque device RGB, and correct transform-before-premultiplication
order. The1/255 limit concerns8-bit presentation; existing processing precision
gates remain unchanged.

An explicit owned-transparent native mode joins the original owned and genuine
Sony modes. Its PNG alpha42000/65535 is verified after decoding. All modes require
the held and committed Paint/Erase pictures to agree within2/255, alongside the
existing held input, Esc, single Undo, stored selection, project reopen, locale,
scope ownership and twelve controls-only capture checks. Standalone CI adds the
two transparent CPU/GPU runs; actual package validation retains all previous15
modes and adds these two, for17. The new mode retains isolated QA settings and
refuses a simultaneous Sony source; it supplies no substitute for real RAW.

## Original D failure and bounded repair

[D59dffd0 / CI40538315991](https://github.com/snailjayxxx/JixelLight/actions/runs/40538315991)
remains failed. All three65-test suites pass with Core215 and zero skips:
Linux1028.10s, Windows1781.39s, macOS1488.64s. Linux/macOS native validation and
the actual macOS15-mode package pass. Windows genuine Sony CPU eraser validation
exceeds the original60000ms at60750ms/phase16. Live previews, Esc, Undo/Redo,
project reopen and full32,947,264-pixel scopes had passed; the final failure crop
shows scopes updating after the compact-window resize. Remaining Sony eraser
GPU, RAW-matrix and Windows package steps did not run.

The native QA sequence now settles the compact Sony viewport before requesting
the exact waveform. Final English controls, current full-resolution waveform,
source pixels and the60000ms gate are still required. Observed phase/capture
times are recorded for diagnosis. This changes the ordering of the same checks;
a Windows timing repair still requires the new committed CI. D's four available
original ZIPs and three full logs preserve its failed result, actual checkout
a1d47521250b0e0926a3a9adc156a45dd30378e7 and treea0949e5535644482d1c58bf1f9866443c1659fdc
equal to its committed source. The missing Windows package was never produced.

## Actual local validation

All targets build serially on the8GB machine. The actual App passes twelve
native workflows with direct1x/2x capture metadata and unchanged source/preferences.

| Native Paint/Erase CPU / Metal ms | 1x | 2x |
| --- | --- | --- |
| Owned opaque | 6912 / 6684 | 7363 / 7318 |
| Owned transparent | 6178 / 6242 | 7258 / 7288 |
| Pinned genuine Sony | 16941 / 16358 | 19021 / 18514 |

Maximum held-to-committed screen difference is1/255, below the2/255 gate. Every
new workflow retains twelve controls-only crops; full new Sony photo PNG bytes
stay in memory. The Sony workflows retain native7028x4688/full32,947,264-pixel
statistics and the original60000ms deadline. The maximum operation reservation
observed in this matrix is1,726,695,808 bytes under3,221,225,472; this is not RSS.

All twelve previous owned/Sony/RAW CPU/Metal1x/2x workflows and twelve copied-Sony
exact/symlink/hardlink output refusals pass. RAW retains all23 screenshots,
full10,340,960 pixels and60000ms:15212/14684ms at1x and19958/19558ms at2x.
New/Sony capture metadata verifies actual DPR; older owned/RAW screenshot widths
are checked against their source-defined logical widths, allowing screen height
clamping. A fresh QWindow probe independently verifies baseDPR1 and actual1x/2x.

Full local65/65 passes in685.69 seconds, with zero failures/skips. Its62 Qt
reports and three real-Sony/CLI tests are verified from the actual CTest commands
and JUnit result. Core215, GPU36, performance39, Look21, admission17 and the
three existing live-preview shards pass; display-ICC now has4 checks. All18
admitted GPU rows retain maximum0/40. Real-source Paint/Erase exports,
exact ICC/pixel predicates, full-source scopes and operation-budget refunds pass.
Only existing evidence was re-read during report reconciliation; the full suite
was run once.

Independent pre-integration evidence preserves the original transparent-Metal
failure. Its eight transformed-view CPU/Metal, opaque/transparent, actual1x/2x
private cases pass after the correction, with exact prepared pixels and zero
held-to-committed screen error. The durable native tests above run against the
actual root App.

Evidence is in the physical build parent's alpha-presentation-native-v1/,
alpha-presentation-legacy-gui/, alpha-presentation-native-alias-refusal/,
alpha-presentation-local-post-audit.json and full65 log/XML. Local source stamps
remain59dffd0 and require exact tested-source links to the eventual commit.

## Publication gate

E requires its own committed65/all3/Core215/display-ICC4 and analytic alpha
evidence, both actual17-mode SDK-stripped packages, five original ZIPs, three
full job logs and actual checkout/tree proof. C remains the last fully accepted
aggregate until a newer run satisfies all gates. Local native success does not
establish Windows deadline success or complete F4/F0-F6.

Source32Mi/shared3GiB, research2Mi/tile8Mi/work96Mi, all color precision,
owned45000ms/RAW-Sony60000ms and performance120s gates remain. Pressure-device
input, native GPU brushes, native FP32 RAW and remaining F0-F6 work stay unfinished.
PR16 remains Draft; main and PR15 are untouched.
