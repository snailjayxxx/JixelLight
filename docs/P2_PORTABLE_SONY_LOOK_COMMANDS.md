# P2 — portable Sony Look commands

Implementation developed on `codex/p2-look-commands` from `8dc75fa` and integrated into `design/lightcraft-fusion-20261008` after missing-source recovery. This stage exposes the existing independent Sony Look approximations through the shared command registry, GUI adapters and offline CLI. It changes command routing/validation, not Sony metadata interpretation, rendering coefficients or the engine version. **Accepted in the combined implementation at `675ef6212bc063aff6c01193f1a26506e9a0ec1d`.** [CI #40294870010](https://github.com/snailjayxxx/JixelLight/actions/runs/40294870010) passed all three platforms and both actual deployed packages. Tested merge `c3e6474089d86b7a3e24afa05f12c6668327391f` and HEAD share tree `9b8c91fe6bc7e6d52ba9fa9aeabb17fafc7f024e`. PR #16 remains Draft.

## Command contract

Every command object has exactly its documented fields. Unknown fields, wrong JSON types, nonfinite values, unsupported modes/codes and unknown control names reject the command without changing its input state. Arbitrary Look snapshot/LUT objects are not command inputs.

| Command | Fields besides `command` | Behavior |
| --- | --- | --- |
| `look.mode` | `mode`: `off`, `as_shot`, `manual` | Disable while retaining controls; reset RAW as-shot intent; or resume/materialize manual controls. |
| `look.code` | string `code` | Fresh manual approximation, strength 1, empty overrides, discard an existing LUT. |
| `look.strength` | numeric `value` | Finite double clamped to `[0,1]`, retaining fractional precision; does not enable a disabled Look. |
| `look.parameter` | string `parameter`, numeric `value` | Change one allowed control after source-specific materialization if needed. |
| `look.reset` | none | Reset Look only to default off; preserve geometry and other adjustments. |

Wire mode `as_shot` maps to the existing persisted spelling **`as-shot`**. Projects, presets, XMP and histories retain Look schema 1; no migration or duplicate persisted spelling is introduced. `--schema` advertises the commands, canonical codes, supported aliases, normalization, source-context requirement and atomic batch policy.

Canonical codes are **ST, PT, NT, VV, VV2, FL, FL2, FL3, IN, SH, BW and SE**. Supported existing aliases are trimmed/case-normalized to their canonical codes. Inputs over 32 characters or containing control characters, including embedded NUL, reject before normalization. These remain JixelLight's independent approximations, not official Sony profiles or a new camera-fidelity claim.

Parameter values preserve the GUI's previous rule: a finite JSON number is rounded to the nearest integer, with half ties away from zero, then clamped to the allowed range. Boolean/string/null inputs are rejected. Strength is continuous and is not rounded to an integer.

| Control | Stored integer range | Default when absent |
| --- | --- | ---: |
| `contrast` | -9..9 | 0 |
| `highlights` | -9..9 | 0 |
| `shadows` | -9..9 | 0 |
| `fade` | 0..9 | 0 |
| `saturation` | -9..9 | 0 |
| `sharpness` | 0..9 | 0 |
| `sharpnessRange` | 1..5 | 3 |
| `clarity` | 0..9 | 0 |

For example clarity `4.6` becomes 5, contrast `-9.5` becomes -9 after rounding/clamping, and strength `0.625` remains 0.625. The shared Look normalization helpers and command schema explicitly describe these rules.

```json
[
  {"command":"look.code","code":"FL"},
  {"command":"look.strength","value":0.625},
  {"command":"look.parameter","parameter":"fade","value":2},
  {"command":"look.parameter","parameter":"clarity","value":4.6}
]
```

## Trusted source context and editable intent

`CommandRegistry::LookContext` contains a RAW flag and source metadata supplied by the application/CLI, never by command JSON. `look.mode/as_shot` requires a RAW source context. It stores fresh symbolic as-shot intent with default strength 1; unknown, conflicting or ineligible metadata stays inactive through the existing `LookProfiles::resolveAsShot` rules. Explicit as-shot on JPEG/TIFF is rejected, retaining the existing protection against automatic double application. Explicit manual code selection remains available on rendered images.

Switching stored as-shot intent to manual, or editing one of its parameters, requires a context. The registry materializes that source's recorded code and all recorded controls before changing the requested value. It preserves strength and unaffected controls. It does not substitute another catalog photograph's metadata or mutate the recorded camera values. When an explicit manual transition has no supported recorded code, it retains the existing GUI fallback to ST; that fallback is not reported as an identified as-shot Look.

Mode off retains stored controls and a previously validated immutable imported/fitted LUT. Resuming manual retains such a LUT as internal calibrated mode, matching the existing “Manual / imported” GUI choice. Strength/parameter edits can retain a LUT already held by the C++ state, but no portable command can create, supply or replace one. `mode: calibrated`, `lut`, `evidence`, `metadata`, `rawSource` and other extra command fields reject. Existing explicit profile import/calibration and compatible LUT-bearing projects remain supported. Existing hash checks establish LUT payload integrity, not authentic Sony provenance.

The GUI setters now stage edits through the same executor/normalizer and retain their previous `look_strength`/`look_parameter` history merge behavior. Presets and `sony_look` selective transfer continue to copy stored editing intent. Saving symbolic as-shot intent does not permanently materialize the first source's metadata; each later source resolves its own recorded Look. An explicit manual transition deliberately captures the current source's editable Look controls.

## Atomic command arrays and CLI planning

`CommandRegistry::executeBatch` accepts at most 10,000 command objects, copies state once, applies commands in order and commits only after the last succeeds. Errors identify the failing command index. A late invalid command preserves the original state and immutable LUT handle. `PhotoController::executeEditCommands` commits a valid array as one undo entry; invalid arrays create no history or adjustment/render event. Existing single-command replay remains available and uses the same staged executor.

CLI jobs retain command arrays during parsing. All source/destination/codec/color-space paths are validated before applying source-aware commands, and every job's commands must validate before any staging/output begins. `requiresLookContext` avoids new metadata reads for ordinary commands or explicit manual sequences that do not require source intent. When needed, small RAW metadata is read per canonical source and routed with each job's own RAW flag; no extra RAW decode is used for context construction. `--list-catalog` remains read-only and metadata-free.

The existing CLI report's `adjustments` describes the resolved rendering state. A separate `stored_look` records the post-command editing intent, allowing a symbolic as-shot request to remain distinguishable from its effective per-source code/controls. Existing no-overwrite, whole-plan preflight and per-file staging publication behavior is retained.

## Verification coverage

Three new Core slots cover:

- All 12 codes, aliases, eight controls and finite round/clamp behavior; strength precision; strict fields/types/control characters; JSON LUT/evidence injection; preservation or clearing of an existing immutable LUT only as documented.
- Different trusted RAW contexts, preserved seven unaffected recorded controls during an as-shot parameter edit, explicit manual materialization, missing-context rejection, ineligible metadata, RAW-only automatic intent and unchanged source metadata.
- Atomic late batch failure including a calibrated base, command limits/non-object entries, one-entry GUI undo/redo, shared GUI normalization, project/history reopen, named preset, XMP, selective transfer and exact Display P3 PNG16 pixels against an independent direct-state reference. Original bytes remain unchanged.

An additional Sony Look GUI slot loads a validated imported LUT, exercises off/resume/strength/parameter edits through GUI and command APIs, rejects JSON calibrated/LUT payloads without changing state/history, then verifies choosing an approximation discards the LUT and undo restores it.

`ValidateCli.py` independently decodes PNG16 scanlines and ICC chunks and compares command-driven output against manually constructed saved-state catalog references. It exercises all 12 codes, aliases, eight-control normalization, strength clamp, off/reset isolation, explicit manual PNG/JPEG, strict invalid/injected inputs, late-job whole-plan refusal, separate batch sources and per-version catalog base contexts. Existing format, source-integrity, no-overwrite, catalog-selection, staging and read-only listing checks remain mandatory.

The existing real Sony native probe reuses each already-decoded public ARW's 1024-edge sample for **seven** direct-state pixel comparisons: off, as-shot, manual, strength, detail, materialized parameter and reset. It reports exact fingerprints/values and strict atomic/LUT/JPEG refusal checks. For each real source it also writes a small direct-state PNG16 oracle from the full RAW, then the validation driver invokes the actual CLI with `as_shot` -> strength -> parameter commands and a small crop. A native comparison checks decoded RGBA64 pixels and ICC exactly. This exercises real per-source metadata context and CLI RAW decode/export, not just reported JSON. Existing fixture SHA checks, independent fitting/held-out gates, zero-skip real-file requirements and generated-image cleanup remain intact. New report fields are required by `ValidateSonyFixtures.py`.

## Local evidence

| Check | Recorded result |
| --- | --- |
| Separate worktree/build | `/Volumes/MacNAS-Dev/JixelLight-P2-worktree`, `/Volumes/MacNAS-Dev/JixelLight-P2-Build`; Qt 6.8.3 Release on Apple M2/macOS 26.5, macOS 15.4 SDK, existing native dependencies, build parallelism 2. |
| New Core slots | **5 passed / 0 failed / 0 skipped**, 171 ms including init/cleanup. |
| Imported-LUT GUI slot | **3 passed / 0 failed / 0 skipped**, 70 ms including init/cleanup. |
| Expanded CLI synthetic validation | Passed, including independent PNG16/ICC Look references and strict whole-plan refusal. |
| Full local CTest, actual Sony fixtures, DNG and Metal GPU | **10/10 passed in 96.92 s**: **212 Core / 39 performance / 36 real Metal GPU / 21 Sony Look / 9 monitor / 3 GPU display / 3 RAW worker**, plus CLI schema/safety and real Sony driver. Required GPU and real-Sony flags were enabled; zero failed or skipped. |
| Real Sony command/CLI output evidence | All **3 existing public ARW/JPEG scenes** passed **7 command cases each**, exact pixel fingerprints, atomic/injection refusal and actual per-source CLI materialization. Each CLI's **53,298-pixel** cropped PNG16 matched the independently constructed direct-state reference and ICC exactly. Fixture hashes and existing held-out fitting gates passed; generated images were cleaned. |
| Final-tree CI/deployed packages | Accepted at675ef6212bc063aff6c01193f1a26506e9a0ec1d in run40294870010; all three platforms and seven-mode actual packages passed. |

No speedup or Sony in-camera JPEG equivalence is claimed. The original GPU parity, source/scope pixel, active-brush, output-safety and test-timeout gates are unchanged.

## Combined recovery integration

Look GUI adapters and single/batch command entry points require `currentSourceAvailable()`. Missing catalog entries continue to expose stored history/curation, while a Look or mixed command batch cannot change them. The missing-source regression exercises all four Look setters plus a mixed command array and requires unchanged Look/history. A localized grouped-adjustments history title covers accepted command batches. The standalone worktree evidence above does not claim acceptance of this later combined tree.

The integrated missing-source/Look tree passed **12/12 CTest in106.24s**, **212 Core/36 actual Metal GPU/39 performance/21 Sony Look/9 project-relink/9 missing-source**, required public Sony/DNG, zero failures/skips. All three real Sony scenes passed seven command pixel cases plus actual53,298-pixel CLI PNG16/ICC comparisons; generated PNG count after cleanup was zero. Combined serial CPU/native-Metal RAW GUI passed **14,391/14,993ms**, all23 screenshots,10,340,960 final full scopes,QML0 and selected-export checks under the old60,000ms deadline. Native missing-source pointer smoke (offscreen/Cocoa) and the existing mask-canvas pointer smoke also passed. Reports: `build-local/local-combined-look-relink-tests.xml`, `local-combined-gui-{cpu,metal}`, `local-combined-{relink-offscreen,relink-cocoa,mask-canvas}.json`; external build logs use the prefix `combined-look-relink-`. These local measurements preceded the committed-tree/seven-mode package acceptance below.

After the final catalog-generation/identity-copy follow-up, focused project/date/Look Core checks passed10/0/0 in355ms and the full missing-source suite passed9/0/0 in971ms (including init/cleanup). Serial final RAW GUI passed **14,233ms CPU /15,296ms native Metal**, each23/23 saved screenshots,10340960 scopes, selected-export checks and QML0 under60000ms. Reports: `build-local/local-combined-final-gui-{cpu,metal}`; focused logs: external `combined-final-project-look-targeted.txt` and `combined-final-missing-targeted.txt`.

## Final combined-tree acceptance

[Run40294870010](https://github.com/snailjayxxx/JixelLight/actions/runs/40294870010) passed the exact combined tree on Windows x64, macOS arm64 and Linux GPU: **12/12 CTest,212 Core/36 mandatory GPU/39 performance/21 Sony Look/9 database relink/9 missing-source, zero skips**. All three command Core slots and the imported-LUT GUI slot passed. Each platform's three public Sony scenes passed seven exact command-pixel cases and actual53,298-pixel CLI materialization/ICC comparisons, retaining fixture hashes, existing fitting/refusal gates and generated-image cleanup.

The two actual deployed packages passed all seven modes with SDK paths removed, including the expanded `develop-cli` command/oracle suite and new recovery UI. RAW CPU/GPU GUI Windows55,134/37,442ms and macOS31,203/30,568ms retained all23 screenshots,10340960 scopes,QML0 and old60,000ms deadline. Recovery GUI passed on all three platforms and both packages; its OS file picker is explicitly not automated. Exact merge/tree and verified artifact evidence are also recorded in [source recovery acceptance](F1_MISSING_SOURCE_RELINK.md#committed-tree-ci-and-actual-package-acceptance).
