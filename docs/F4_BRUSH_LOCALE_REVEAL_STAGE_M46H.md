# F4 M4.6H — preserve brush controls across language changes

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Candidate based on G `3428eec995c09c94fa960b4dc6946f0e55e169e9`. Physical app build and actual local native checks pass. This candidate still requires its own committed three-platform44/44 and both actual eleven-mode packages, especially the Linux Noto language-reflow case. E24ac54d remains the last fully accepted checkpoint.

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

This slice changes only panel positioning and its native harness/wrapper; model, command, kernel, persistence, headers, CTest and CI configuration are unchanged. No duplicate heavy local44 run is needed for those unchanged sources. New committed-H CI must nevertheless pass all44/44 without skips on Linux/Windows/macOS, actual native owned/realSony/RAW modes, both complete11-mode packages and exact source/tree/artifact checks. Local macOS success alone does not establish the Linux Noto reflow fix. Capacity feedback remains deferred until this observed regression closes. Eraser, pressure-device input, live-drag color, native GPU brush, native FP32 RAW and overall F0–F6 completion remain outside this slice.
