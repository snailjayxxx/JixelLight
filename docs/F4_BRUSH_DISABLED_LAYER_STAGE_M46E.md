# F4 M4.6E — disabled brush-layer input and canvas feedback

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Based on accepted M4.6D `bee4c609ccbb7353b13870250c658d6092cdb1e9` / [CI40411270928](https://github.com/snailjayxxx/JixelLight/actions/runs/40411270928). E is accepted at `24ac54d3f516c68e4606d36f253ba248ecbd0318` / [CI40419140975](https://github.com/snailjayxxx/JixelLight/actions/runs/40419140975).

Selecting a disabled saved brush layer previously left a cross cursor and “Drag to paint” canvas hint. The controller safely refused the stroke, but the displayed instructions contradicted that result. The overlay now derives paint availability from the selected layer's existing enabled state. Disabled targets show a forbidden cursor and a localized instruction to enable the layer; pointer presses create no transient stroke. New layers remain paintable, and re-enabling restores the ordinary cursor/input/hint. Selection and enabled-state changes cancel transient guides through the existing cancellation path.

The UI reads existing layer summaries. No model selection state, command, persistence schema, pixel kernel, cache key, source decoder, admission limit or export semantics change. The controller's exact-ID and source/revision guards remain authoritative.

## Actual native RED/GREEN

The new native check reproduced RED on actual M2 Metal after4,013ms at phase14. Input was correctly ignored and source/history/document/viewport stayed unchanged, but the Chinese hint still said “Drag to paint” and cursor shape was Cross2 rather than Forbidden14. Retained report/screenshot: physical build `brush-disabled-gpu-red.json*`, parent `brush-disabled-gpu-red-wrapper.log`. This is a new UI criterion exercised by an uncommitted harness, with precommit base stamp `bee4c60`; it does not reinterpret the earlier D acceptance.

The scenario now checks actual Chinese and English disabled hints, forbidden cursor and native pointer moves/releases with no gesture/history/document/ROI mutation. It re-enables the layer, requires restored pointer/hint plus the loaded current full waveform and visible ink, then retains the final English controls capture. All previous20 required flags/five captures remain; one composite disabled-target flag and one disabled English screenshot make21 flags/six captures. The existing45,000ms internal and65-second wrapper gates remain unchanged. All eleven deployed package modes remain required.

Final local CPU/Metal GREEN passed5,914/6,715ms, all21 required flags/six captures/full2,659,021 statistics/902 visible waveform samples/QML0. Actual scale2 CPU/Metal also passed6,315/6,398ms with clipped2360×1440 captures. Disabled and restored English screenshots were visually inspected. Reports are physical build `local-brush-disabled-smokes/`, with parent `brush-disabled-green-validation.log`; the RED, first GREEN and final build logs remain separately retained.

Real pinned Sony native CPU/Metal passed12,757/13,502ms, all14 flags/five private captures/full32,947,264 histogram/waveform pixels/source SHA/3GiB within60,000ms. Full photographs stay in private parent `private-sony-brush/disabled-green-{cpu,gpu}/`; public physical build `sony-brush-{cpu,gpu}-disabled-green.json*` contains controls-only crops. Original RAW GUI CPU14,011/Metal15,325ms passed23 screenshots/10,340,960 scopes/all UI and selected-export flags/QML0 under the original60,000ms. Diagnostic CPU117/Metal273ms, mask927ms and relink473ms passed; OS picker automation remains outside scope. Daily native preference hashes remain unchanged across all smoke modes.

Pixel/model/command/test-suite sources are unchanged, so no duplicate local43-suite run was started for this QML/harness-only change. The accepted D43-suite evidence is recorded at D, not attributed to E. The committed E run subsequently passed its own43/43 on all three platforms and both actual eleven-mode SDK-stripped packages, as recorded below. No native GPU brush, native FP32 RAW, pressure-device, eraser or continuous-drag color capability is added; F0–F6 remain unfinished.

## Committed E acceptance

[CI40419140975](https://github.com/snailjayxxx/JixelLight/actions/runs/40419140975) passed43/43 with zero skips: Linux645.52s, Windows1,123.23s, macOS869.84s. All27 admitted GPU cases retain max0/65535. Every platform logged actual checkout `036a4e28793758ef8f027400ac37a98e3c0533b0`; its tree `22f6efb94674a24225a2476601a1baf95a954919` equals E HEAD, and all report stamps match that checkout.

| Actual native check, CPU/GPU ms | Linux | Windows | macOS |
| --- | --- | --- | --- |
| Owned pointer,21 flags/six captures | 6,243 / 7,558 | 14,966 / 10,951 | 10,793 / 10,993 |
| Real Sony,14 flags/five private captures | 19,423 / 21,234 | 44,547 / 42,343 | 22,059 / 22,124 |

Chinese/English forbidden cursor14, ignored native input and restored current full waveform pass on all platforms. Real Sony retains32,947,264 native histogram/waveform pixels, unchanged source SHA, shared3GiB and60,000ms. Owned pointer retains2,659,021 full statistics and45,000ms.

Both actual SDK-stripped eleven-mode package validations pass. Windows/macOS original RAW CPU55,412/34,628ms and GPU37,272/30,926ms retain23 screenshots,10,340,960 scope pixels, all UI and selected-export checks, QML0 and the original60,000ms. Packaged owned CPU14,962/10,893ms and GPU10,871/11,082ms retain21 flags/six captures. Packaged real Sony CPU44,410/19,743ms and GPU42,538/22,491ms retain all14 flags/native full statistics/3GiB/60,000ms. All four real-Sony CLI formats and queued ST TIFF exact references remain green; the native OS picker remains outside automated scope.

Five original validation/package ZIPs were SHA256/CRC/traversal checked, and three complete job logs retained. Machine acceptance: physical build parent `ci-40419140975-summary.json`, with original ZIPs and logs in `/tmp/jixellight-40419140975-*`. Full Sony photographs stay private; public artifacts contain controls crops and hashes. Earlier B/C failed evidence remains failed. Later source changes require their own run.
