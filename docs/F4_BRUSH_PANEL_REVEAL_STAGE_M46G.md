# F4 M4.6G — reveal the brush controls after layout

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Committed G `3428eec995c09c94fa960b4dc6946f0e55e169e9` is **not accepted**: its Linux native English-layout check failed. All three44/44 suites and both actual eleven-mode Windows/macOS packages passed. The follow-up [M4.6H](F4_BRUSH_LOCALE_REVEAL_STAGE_M46H.md) addresses the observed language reflow. E24ac54d remains the last fully accepted checkpoint; F's cancelled macOS package stays incomplete.

## Committed outcome

[CI40434536139](https://github.com/snailjayxxx/JixelLight/actions/runs/40434536139) finished with Linux failure and Windows/macOS success. All44/44 CTest passed, zero skips: Linux546.49s, Windows1,126.36s, macOS872.99s. Each retained212 Core/36 GPU/39 performance/21 Sony Look/17 admission; all27 admitted GPU cases have maximum error0/65535. All three full job logs show actual merge `401156aa48838a94fb3429e5ea6e844f3b434446`, tree `6a968ded43dbb2937a40e8cb06886b64082aa161`, equal to G HEAD's tree.

Linux native CPU stopped at phase19 after7,771ms: “English layer controls are clipped.” After Chinese-to-English reflow, contentY838 remained while the brush header moved toY−89 and EV toY36 in a244px viewport, with pending=false/attempts3. Earlier wheel/re-entry/manual-cancel checks passed, full2,659,021 statistics and QML0 remained. Real CJK glyphs are now legible in the actual Noto-equipped Chinese reveal capture; the English failure capture shows the upper controls clipped. Linux's later native GPU, real-Sony and original RAW GUI checks were not run. Numerical/CTest success does not close this UI failure.

Windows and macOS both completed all actual eleven SDK-stripped package modes. Original RAW CPU/GPU: Windows54,765/37,066ms; macOS32,941/30,243ms, all23 screenshots/full10,340,960/QML0/original60,000ms. Owned native CPU/GPU: Windows23,429/14,450ms; macOS15,753/15,253ms, all23 flags/eight captures. Real Sony CPU/GPU: Windows49,292/43,496ms; macOS15,809/16,203ms, all16 flags/six private captures/full32,947,264/sourceSHA/shared3GiB/60,000ms. The macOS60-minute total-job repair is verified independently by this complete package; G still fails overall on Linux.

All five original ZIPs have verified size, SHA-256, CRC and safe extraction; all three full job logs are retained under `/tmp/jixellight-40434536139-*`. Failure-aware summary: `/tmp/jixellight-40434536139-summary.json` and physical build parent `ci-40434536139-summary.json`, accepted=false. Linux failure JSON/reveal/failure PNGs are preserved.

| Original artifact | ID | Bytes | SHA-256 |
| --- | --- | --- | --- |
| validation-Linux | 12703787069 | 1,777,579 | `adf38c5d7b5790d65f11d2df7d8dafdef9113491d1959d932f9261c3870577a1` |
| validation-Windows | 12705036493 | 28,721,007 | `c0d378c7156264c5ec3bbb932b4189a23bee19391a9d784c6b11380265f9c0a2` |
| package-validation-Windows | 12705306136 | 15,529,457 | `e9ba29a17ad95b04edf27cdae7d733aedb64d9c2c07359b837b2fc843d4282ff` |
| validation-macOS | 12704479393 | 31,623,735 | `cb3c5fa5d9573c7b953f871dd5bf6e82ecaaf8bc0972efb8157bbb693138057a` |
| package-validation-macOS | 12705480258 | 21,347,406 | `f4c69383e3dcf7c2b4cffba638d19bf321ca046c3438cb549ec6061638e57ef3` |

Choosing Paint brush sometimes left Crop/Rotate controls visible in the right panel. Actual2× real-Sony Metal stopped at phase1 after4,542ms, before painting or operation-budget allocation: the brush overlay was enabled, but the EV control was clipped. CPU2× had passed15,731ms. The retained controls screenshot establishes a UI failure, with QML0 and unchanged source/preferences. The original one-shot30ms handler did not confirm actual visibility. A pre-layout/clamped scroll position is a hypothesis; the failed run did not record internal geometry.

The new entry request checks populated layout geometry and the actual header/EV rectangles. It recomputes a clamped position while pending and ends after two consecutive visible observations, bounded to60 attempts at16ms intervals. Closing/hiding the tool or native wheel/drag/flick input stops the request, so manual scrolling retains control. No persistent loop runs while the user edits.

## Actual UI verification

The owned-source native scenario uses real Qt wheel and tool-button events to scroll away, re-enter, interrupt a pending reveal by scrolling, and re-enter again. It checks that masks, history and ROI stay unchanged, then retains `.reveal.png`. All old22 flags/seven captures remain; one composite reveal flag/eighth capture makes23. Real Sony keeps all15 flags/six private captures and adds actual final visibility/pending/viewport geometry, making16 flags. No deadline or earlier criterion is relaxed.

| Local native check, CPU / Metal ms | Normal | Actual2× |
| --- | --- | --- |
| Owned pointer,23 flags/eight captures | 9,420 / 9,117 | 9,630 / 9,556 |
| Public real Sony,16 flags/six private captures | 13,736 / 13,450 | 15,019 / 15,165 |

All owned cases retain full2,659,021 statistics, actual selected path/Delete/Undo/Redo, manual-scroll cancellation and unchanged document/history/ROI within45,000ms. Final owned geometry reports pending=false, attempts3, headerY0 and EVY129 within a240px viewport. All Sony cases retain native7028×4688=32,947,264 histogram/waveform pixels, pinned source SHA, shared3GiB, actual selection/delete/Undo/project reopen and60,000ms. Sony2× reports pending=false, attempts2, headerY0 and EVY129 within a160px viewport; Metal reservation peak1,721,312,960 bytes.

Original RAW GUI CPU14,069/Metal14,785ms retains23 screenshots,10,340,960 scope pixels, all UI/selected-export checks and QML0 under60,000ms. Diagnostic CPU158/Metal253ms, old mask923ms and relink513ms pass; OS picker remains unautomated. Daily native preference SHA remains unchanged across all modes. The reveal and real-Sony controls captures were visually inspected.

## Evidence and provenance

Actual RED: physical build `sony-brush-gpu-selection-hidpi2.json*` controls-only, parent `brush-selection-hidpi2-validation.log`; full photographs stay private in parent `private-sony-brush/selection-hidpi2-gpu/`. It is not overwritten or retroactively accepted. First GREEN: `sony-brush-gpu-reveal-first-hidpi2.json*`, parent `brush-reveal-sony2-first-validation.log` and `brush-reveal-sony2-first-local-summary.json`; full photos private in `private-sony-brush/reveal-first-hidpi2-gpu/`.

After first Sony2× GREEN, only the owned wheel-test helper moved its probe to a4px blank gutter to avoid sliders/dropdowns. The stored first source hash matches the separate candidate, and all source outside that unused wheel helper, including the entire Sony flow and production QML, is byte identical to the final source. That scoped GREEN is reused with explicit provenance. Final owned Metal9,117ms is also reused rather than rerun. All remaining configurations use the final source. No test source is silently attributed to a later version.

Final reports: physical build `local-brush-reveal-first-smokes/` (owned Metal), `local-brush-reveal-final-smokes/` (CPU/2×/diagnostic), `sony-brush-reveal-final-{cpu,gpu}.json*` and `sony-brush-reveal-final-hidpi2-cpu.json*` controls-only, `local-brush-reveal-gui/` (legacy modes). Full Sony photographs/projects remain in private parent `private-sony-brush/`. Parent logs retain first/wheel builds and first/final validation separately. Complete summary/source hashes: parent `brush-reveal-final-local-summary.json` and `/tmp/jixellight-brush-reveal-local-summary.json`. Reports retain precommit base stamp62aa35d; committed G CI must establish its own source/tree.

## CI completion and boundaries

F [CI40427312316](https://github.com/snailjayxxx/JixelLight/actions/runs/40427312316) passed44/44 on Linux643.59s, Windows792.77s and macOS1,058.53s, zero skips, but its macOS package task was cancelled at the configured45-minute total job limit. The [job annotation](https://github.com/snailjayxxx/JixelLight/actions/runs/40427312316/job/121936184385) confirms the reason. Windows's actual eleven-mode package passes; macOS has three completed GPU modes and no final manifest. F remains unaccepted.

G assigns a60-minute total CI job budget to macOS and retains45 minutes for Windows, allowing the complete build/test/deployment sequence to finish. Owned45,000ms, original RAW/realSony60,000ms,120-second test shards, source32Mi pixels/shared3GiB/research2Mi/tile8Mi/work96Mi/GPU40/65535 remain unchanged. Linux CI adds `fonts-noto-cjk`: F's Chinese captures lacked glyphs and cannot establish text legibility. This changes runner prerequisites and adds no bundled application font or Linux package claim.

Model, command, persistence, source, pixel-kernel and CTest sources are unchanged, so no duplicate heavy local44 run was started. G's own committed evidence is recorded above and remains unaccepted because of the actual Linux UI failure. The local results below that outcome are not substituted for missing Linux gates. Partial erasing, pressure-device input, live-drag color, native GPU brush, native FP32 RAW and F0–F6 completion remain outside this slice.
