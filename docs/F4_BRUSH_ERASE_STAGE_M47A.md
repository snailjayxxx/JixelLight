# F4 M4.7A: ordered brush erasing

Status: committed N run failed its Linux sync/reopen regression; not accepted.
The last fully accepted checkpoint is M4.6M at
`1f4b26e55ec7c3e4fe3dca2386eef924c31b354c`,
[CI40477576605](https://github.com/snailjayxxx/JixelLight/actions/runs/40477576605).
This feature requires its own committed **62-test, three-platform and both
15-mode deployed-package** acceptance. PR16 remains Draft.

## User behavior and stored intent

Choose Paint or Erase beside the layer selector. Erase affects the selected
existing enabled brush layer and preserves its exposure and other layers. A
completed gesture creates one Undo step; Escape and changes to mode, style,
source or viewport cancel an unfinished gesture. Missing, disabled and full
targets refuse before transient input. New layer returns to Paint.

Within a stroke, coverage remains the maximum of its dabs. Across stored
strokes, Paint computes `1-(1-a)*(1-s)` and Erase computes `a*(1-s)` in order.
Later Paint can restore erased coverage. Mode is typed and frozen when the
gesture starts. Stored erasers use the existing guarded stroke selection,
deletion, Undo/Redo and project-reopen paths.

Paint-only canonical schema1 bytes stay unchanged. Schema2 is emitted only
when an eraser exists, including disabled or zero-EV layers, and explicitly
stores every stroke's mode. Removing the last eraser normalizes back to1.
Missing, unknown or wrongly typed modes, unsupported schemas, extra fields
and invalid command batches fail atomically. Older binaries cannot read
schema2 and must reject it rather than discard masks. CLI schema metadata
advertises1+2 while its default remains1.

Existing16-layer,64-stroke-per-layer and16,384-dab limits count erasers too.
Source32Mi pixels, shared3GiB, research2Mi, per-tile8Mi and request96Mi work
limits remain. Color processing, RAW decoding, shader tolerance and existing
paint assertions are unchanged. GPU display uploads an admitted CPU reference;
this is not native GPU brush computation or native FP32 RAW decoding.

## Actual local verification

- Fresh complete Core ABI and all-target rebuild, physical parallel1 on the
  8GB machine. All original47 CTest entries pass in453.63s: Core215/0/0,
  both new gesture/refusal shards3/0/0 each, old GPU36/performance39/Look21/
  admission17 and genuineSony/export/scopes checks retained.
- After adding production data rows,15 new shards and6 affected legacy rows
  pass21/21 in345.54s, zero skips, with62 registered entries. Every paint-only
  row and original120s gate remains. This is two successive local runs, not
  a claimed single full62 run or committed CI acceptance.
- Fractional Paint/Erase/Paint covers native Sony whole/uneven ROI in
  sRGB/P3/ProPhoto, all4096 histogram bins and three exact scope plots,
  full-source PNG16/TIFF16/WebP8 and JPEG/ICC, JPEG cancellation and recovery,
  and all four cropped CLI codecs. Native source is7028×4688/32,947,264;
  CLI output is5,085,762 pixels. The pinned69,120,000-byte public ARW has SHA
  `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d`.
- Nine additional admitted GPU rows cover signed HDR/alpha and genuine Sony
  fit/pan100 across three outputs: maximum0/65535 under the unchanged40 gate.
  Full-source JPEG maximum5/255 meets the unchanged5 gate. Eight new large
  requests peak at1,703,890,688 reserved bytes under3,221,225,472; end charges
  return to0. Their sampled process RSS peak is895,156,224 bytes, an observation
  of these cases rather than a general application-memory guarantee.

Native mouse/key checks refuse empty-layer erasure, Paint/Erase, transient
state, Escape, one Undo, visible pixels, Undo/Redo, stored erase deletion,
schema1 recovery, exact project reopen, loaded-layer cancel and full waveform.
Chinese1540×920 and compact English1180×720 are checked at actual1×/2× DPR.
Only nine controls crops per run are saved; whole-window PNG bytes are hashed
in memory. No Sony photograph is published.

| Actual DPR | Owned CPU / Metal ms | Real Sony CPU / Metal ms |
| --- | --- | --- |
| 1× | 4,679 / 4,144 | 16,200 / 15,454 |
| 2× | 6,283 / 5,998 | 23,030 / 23,361 |

All eight retain owned45,000ms/Sony60,000ms, source hashes,3GiB and exact
full-source statistics/plots. Original owned/Sony/RAW workflows also pass all
12 normal/2× CPU/Metal cases. Original RAW15,200/14,989ms and22,267/20,792ms
retain23 captures, every prior UI/vignette guard, full10,340,960 and60,000ms.
Daily preferences and original Sony/DNG hashes stay unchanged throughout.
Six native report/crop alias refusals return2 without modifying a copied
pinned source, covering direct paths, symlinks and hardlinks.

## Failures retained and corrected

Native v2 exposed a translated-model reset: the mode box displayed Paint
while editor intent remained Erase. Stable model values with translated
display/delegates fix it. Visual inspection then caught the same older scope
selector problem and a stale translated plot status; the scope model is now
stable and its status binding explicitly observes language. Final native
checks require visible Erase, Luma waveform and Full resolution.

V3's eight narrower passes remain separate. V4's inner native workflow passed,
but its orchestrator wrongly assumed a1× screen; actual DPR was2. An empirical
Qt window probe calibrated the child environment, and fresh V5 captures
explicitly verify actual1×/2×. A later extra auditor also assumed an incorrect
old owned-window size; the source defines1180×800, and the same artifacts were
re-audited at1180×800/2360×1600. Both failed audit records remain; no failed run
is retroactively accepted and passed heavy tests were not restarted for them.

Local evidence lives under the physical build parent in `eraser-full47-*`,
`eraser-production21/`, `eraser-native-v2/`, `eraser-native-v3/`,
`eraser-native-v4/`, `eraser-native-v5/`, `eraser-legacy-gui/`,
`eraser-native-alias-refusal/` and `eraser-local-final-post-audit.json`.
Reports retain their precommit1f4b26e stamp and exact source manifests;
they are not relabeled as new committed or deployed results.

Both workflows invoke four new owned/Sony CPU/GPU eraser checks, and actual
SDK-stripped package validation now requires15 modes including all prior11.
Committed N source/artifacts are audited below; its Linux failure remains.
M4.7B applies a targeted version-identity test repair with its own acceptance. Pressure-device
input, continuous drag color, native GPU brush and remaining F0–F6 work stay
unfinished. Main and PR15 remain untouched.

## Committed outcome: failed Linux sync/reopen regression

N a53d201 /[CI40493231189](https://github.com/snailjayxxx/JixelLight/actions/runs/40493231189) is not accepted. Linux61/62 in869.85s fails only the unchanged selective-sync/reopen Core case (214/1/0, exposure1.25 rather than−1 at the assumed ordinal). New17eraser shards/threeCoremethods and all18admittedGPUrows max0/40 pass; laterLinuxGUIsteps do not run. Windows62/62 in1216.39s/macOS62/62 in1361.58s andboth actual15-mode packages pass withalloldgates.

| Actual package CPU / GPU ms | RAW | Owned brush | Sony brush | Owned eraser | Sony eraser |
| --- | --- | --- | --- | --- | --- |
| Windows | 46984 / 29483 | 28549 / 16968 | 30319 / 23876 | 13007 / 6277 | 31372 / 25179 |
| macOS | 34112 / 30453 | 23902 / 23011 | 22845 / 16357 | 7039 / 6441 | 17388 / 22325 |

Each eraser retainsninecontrols/English/fullsource/SHA/shared3GiB/owned45s-orSony60s, priorRAW23/allUI/full10340960/60s andallotherpreviousmodes remain. All5original ZIP/full3logs andactual all3ebabc2d/treeequalHEAD verified. A targeted stable-version-ID test repair is prepared in M4.7B; N's failure remains its original outcome, andM1f4b26e remains lastfullyaccepted. The controlledreorder proves the unsafe ordinal assumption withB's undo intact; it is not called a natural replay oftheCI failure.

Original validation archive receipts (also copied byte-for-byte to the physical
build parent at `evidence/ci-40493231189/`, without redownloading):

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| validation-Linux | 362016 | `3c52956f4fca3e4dbee69ae5241402eedc1e9636d4290c47e6ab88e7dfc580bf` |
| validation-Windows | 31340486 | `914afb99bbfd6441e0b7fce31301f9d4abe95a06feaf629fb62bf72b795d8959` |
| package-validation-Windows | 18114922 | `d8029bf058ff693533d91f8ff536d73476f0d0181f627f92d277f810a782933c` |
| validation-macOS | 34294086 | `e79ffd8631f2d641e4807a4ed907aacc097ed501355c843ca25ff8e34101c43b` |
| package-validation-macOS | 24000003 | `2a7e2050943d742dc8e37a396f22e1020936d9484fd5501789e67ee80d88f4e3` |
