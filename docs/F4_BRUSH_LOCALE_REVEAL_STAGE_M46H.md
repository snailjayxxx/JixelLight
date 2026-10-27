# F4 M4.6H — preserve brush controls across language changes

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Committed H `21cd3d87029fadca196a12fd3684976ca87ea464` is **not accepted**: macOS packaged owned Metal failed its phase31 reveal-completion assertion. All three44/44 suites and the actual Windows11-mode package passed. Linux language reflow is verified; the [M4.6I follow-up](F4_BRUSH_REVEAL_WAIT_STAGE_M46I.md) addresses the observed native-test synchronization failure. At this failed H checkpoint, E24ac54d was the last fully accepted source; the later I acceptance is recorded below.

## Committed outcome

[CI40440387028](https://github.com/snailjayxxx/JixelLight/actions/runs/40440387028) finished Linux/Windows success and macOS package failure. All44/44 passed without skips: Linux647.21s, Windows1,108.15s, macOS879.32s;212 Core/36 GPU/39 performance/21 Sony Look/17 admission remain, all27 admitted GPU cases max0/65535. All three full logs show actual merge `9a252be664111be6e5593a3d4bb93688144b1b10`, tree `6a72cb6ecfbae204fb495ce4bfb462aa24fb6e68` equal to H HEAD.

| Standalone native CPU / GPU ms | Owned23/eight plus locale | RealSony16/six |
| --- | --- | --- |
| Linux | 9,577 / 10,510 | 19,902 / 22,065 |
| Windows | 23,900 / 14,491 | 48,002 / 40,930 |
| macOS | 17,206 / 15,753 | 21,683 / 21,216 |


Linux's English headerY0/EVY125 within244px, pending=false/owner=true, and both locale/manual-scroll invariants pass; actual English controls and legible Chinese Sony controls/waveform captures were inspected. The prior G English clipping regression closes on Linux. Original RAW CPU19,926/GPU23,975ms retains full10,340,960/QML0/60,000ms; GPU has23 screenshots, while the existing Linux CPU workflow omits screenshots.

The actual Windows SDK-stripped11-mode package passes. Original RAW CPU54,683/GPU37,349ms retains23 screenshots/full10,340,960/QML0/60,000ms; ownedCPU23,943/GPU14,706ms and SonyCPU48,262/GPU42,138ms retain all criteria.

macOS attempted all11 modes:10 passed; only owned Metal failed at phase31/7,684ms. Its failure JSON reports critical_visible=true, headerY0/EVY129/viewport162, owner=true, attempts2 and pending=true. The actual failure screenshot shows the controls. The native test checks completion after three smoke timer ticks while the independent bounded QML reveal is still running. It must observe completion before asserting the same actual visibility. This is not accepted as success. Original Mac RAW CPU33,331/GPU29,971ms retains23 screenshots/full10,340,960/QML0/60,000ms; ownedCPU16,380ms and SonyCPU22,025/GPU22,905ms pass, along with diagnostics/relink/CLI.

All five original ZIPs have verified size, SHA-256, CRC and safe extraction. Full logs preserve979,557 Linux/925,441 Windows/910,516 macOS characters, including original Windows CRLF. Originals, failure PNG/JSON and failure-aware summary are retained under `/tmp/jixellight-40440387028-*`; physical build parent also retains `ci-40440387028-summary.json`, accepted=false.

| Original artifact | ID | Bytes | SHA-256 |
| --- | --- | --- | --- |
| package-validation-macOS | 12707840065 | 20,721,224 | `dea45ea523008b5441f6059566573f36dc6cf654df135788039cad6468315581` |
| validation-Windows | 12707770857 | 28,719,545 | `5e9742e05b12de315f980deab55a69aec8e3a3f7d552add91ee1308c7f59d4ed` |
| package-validation-Windows | 12707583883 | 15,502,236 | `f3edc76dd62a5c587840e7be459f67bc4e2edec5eeecec658572d6feca93986e` |
| validation-Linux | 12707485221 | 32,157,637 | `f332c7aba227b2cd39b6e5c00e7111a42bff52f2e63afc1a7c27dd612d181602` |
| validation-macOS | 12707082639 | 31,640,055 | `56a55e7e488fbbff8bea06eee82b8a2d9660541eefa6def31176e2ac44d2110d` |


## Observed failure and change

G [CI40434536139](https://github.com/snailjayxxx/JixelLight/actions/runs/40434536139) passed44/44 on all three platforms and both actual Windows/macOS packages, but Linux native CPU failed at phase19/7,771ms after Chinese-to-English text reflow. Stored contentY838 left the header atY−89 and EV atY36 in a244px viewport, pending=false. The actual Chinese reveal and English failure captures show legible CJK text and clipped English upper controls respectively. Earlier manual wheel/re-entry checks passed; later Linux GPU/Sony/RAW checks were skipped. G is not retroactively accepted.

Entering Paint brush now gives its bounded reveal request ownership of the automatic position. Stable completion retains ownership, allowing a language change to request a fresh geometry check. Manual wheel/drag/flick input, hiding/collapsing/leaving the tool, or an exhausted request relinquishes ownership. Language changes then preserve the user's manual scroll position. The existing16ms/60-attempt bound, valid-layout checks and two consecutive visible observations remain. No perpetual scroll correction or longer fixed sleep is added.

The owned native scenario now changes English then Chinese while manually scrolled away and requires owner=false, pending=false and the same manual position. After actual tool re-entry, a later English switch with automatic ownership must request a new reveal and satisfy the original actual-control visibility assertion. Reports add `automatic_owner`, `manual_locale_scroll_preserved` and `language_reveal_requested`. All existing23 flags/eight captures and every earlier criterion remain required; the two new locale booleans are additional invariants. Real Sony retains16 flags/six private captures.

## Actual local verification

Qt6.8.3 physical app build passed with parallelism2 on the8GB M2. Separate target-flag C++ syntax and QML lint passed; the same59 inherited QML warnings remain. Actual native checks supersede static-only evidence.

| Actual native check, CPU / Metal ms | Normal | Actual2× |
| --- | --- | --- |
| Owned pointer,23 flags/eight captures plus locale invariants | 9,695 / 9,747 | 10,094 / 11,662 |
| Pinned public Sony,16 flags/six private captures | 13,529 / 13,196 | 14,751 / 15,189 |

All owned checks retain full2,659,021 statistics, selected stored-stroke Delete/Undo, layer operations/history/ROI, QML0 and45,000ms. Manual scrolling remains respected across both language changes; automatic positioning requests another reveal and finishes with visible header/EV, pending=false, owner=true, attempts2/headerY0/EVY129/viewport240. Actual English layer controls and real-Sony2× final controls were visually inspected.

All Sony checks use the pinned69,120,000-byte public ARW, SHA-256 `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d`, decoded7028×4688=32,947,264 native pixels. Full histogram/waveform, source bytes, shared3GiB, actual paint/select/Delete/Undo/project reopen and60,000ms pass. Sony2× retains a160px controls viewport with headerY0/EVY129, pending=false, owner=true, attempts2. No generated source is substituted.

Original RAW GUI CPU14,312/Metal14,790ms retains23 screenshots/full10,340,960 scopes/all UI and selected-export checks/QML0/original60,000ms. Diagnostic CPU118/Metal468ms, mask923ms and relink485ms pass; the OS picker remains unautomated. Daily native preference SHA is unchanged. Source32Mi/shared3GiB/research2Mi/tile8Mi/work96Mi/GPU40/65535 and120s shards remain unchanged.

## Evidence and provenance

Physical build: `/Volumes/MacNAS-Dev/JixelLight-Build/build-local`. First owned CPU GREEN is retained at `local-brush-locale-first-smokes/brush-cpu.json*`; it is reused without another run because the stored three-source hash matches final source byte for byte. Other owned/diagnostic checks are in `local-brush-locale-final-smokes/`. Sony public numeric/hash/controls-only reports are `sony-brush-locale-final-{cpu,gpu}.json*` and `sony-brush-locale-final-hidpi2-{cpu,gpu}.json*`. Full photos/projects stay in private parent `private-sony-brush/locale-final-*`; photographs are not published. Original RAW/mask/relink evidence is in `local-brush-locale-gui/`.

Parent build/validation logs: `brush-locale-first-build.log`, `brush-locale-first-validation.log` and `brush-locale-final-validation.log`. Complete local summary/source hashes: parent `brush-locale-final-local-summary.json` and `/tmp/jixellight-brush-locale-local-summary.json`, passed=true/accepted=false. Raw reports retain precommit base3428eec; they are not relabelled as committed-H CI. Final source SHA-256:

| File | SHA-256 |
| --- | --- |
| ui/DevelopRightPanel.qml | `1aa602bf55483d4d3547ac12d32bdfbb2babbc0dd1a4d3ecd492c7ab4870a01c` |
| app/BrushSmokeRun.cpp | `7402a4631655f4e11f2ad035fbff6c4092ad40194974f23deae416858adda812` |
| tools/ValidateBrushCanvas.py | `c0390eacd3408701deb272021063109e1b2a42da9745101ff2126925fd51f8e6` |

G's original five ZIPs, three full logs, actual merge/tree proof and Linux RED remain as recorded in the [G runbook](F4_BRUSH_PANEL_REVEAL_STAGE_M46G.md). No G result is attributed to H.

## Remaining acceptance

H's committed outcome is complete and failed as recorded above; none of the local results is substituted for the missing packaged Metal criteria. [M4.6I](F4_BRUSH_REVEAL_WAIT_STAGE_M46I.md) retains H's production code and observes bounded reveal completion in the native scenario. I `8ed006a` subsequently passes its own44/all three and both complete11-mode packages, including an actual pending wait in packaged Metal. H itself remains failed. Capacity feedback is the next scoped follow-up. Eraser, pressure-device input, live-drag color, native GPU brush, native FP32 RAW and overall F0–F6 completion remain outside this slice.
