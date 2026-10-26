# F4 M4.6A — canonical active brush persistence and commands

Development branch `design/lightcraft-fusion-20261008`; PR #16 remains Draft. Candidate based on accepted7d76c54/CI40374144853. Local checks below passed; own three-platform/package acceptance is pending. This stage supports stored active effects; pointer painting and a completed brush-editing UI remain future work.

## Behavior and compatibility

Canonical schema1 brush documents may now retain enabled layers through adjustment JSON, project snapshots/history, virtual versions, named/portable presets, XMP and selective masks transfer. Existing render/cache/GPU-display/queue/CLI/full-statistics consumers use the previously introduced explicit admission paths. Ordinary documents still omit the absent brush field and disabled layers still render identically to no brush. Project/XMP/preset reader strictness is otherwise unchanged: unknown/future/null/empty/noncanonical data refuses atomically; invalid typed C++ intent cannot disappear silently from a write. Older binaries which supported disabled drafts refuse an enabled document rather than render it incorrectly.

The existing caps remain16 stable-ID layers,64 strokes per layer,4096 dabs per stroke and16,384 total dabs, normalized finite coordinates, radius[.002,.5], hardness/opacity/pressure[0,1] and layer exposure[-3,3]EV. This does not expand the32Mi-pixel/3GiB operation admission or8Mi-tile/96Mi-total candidate work. A valid saved document can still receive a visible rendering/admission failure on an unsupported source or excessive work; serialization is not a certificate of rendering cost. Production recovery/error paths remain required.

The shared strict command registry adds:

| Command | Fields | Effect |
| --- | --- | --- |
| `mask.brush.replace` | `document` | Replace one fully validated canonical document atomically. Empty documents use reset instead. |
| `mask.brush.enable` | `layer`, `enabled` | Toggle an exact existing stable ID without losing strokes; boolean only. |
| `mask.brush.exposure` | `layer`, `value` | Set finite[-3,3]EV on an exact existing layer; reject out-of-range values. |
| `mask.brush.reset` | none | Remove only brushes, preserving gradients and other controls. |

Whole command arrays retain the existing atomic staging and actual-source Look context. Unknown IDs never select an arbitrary layer. Commands cannot inject camera metadata or load files. The existing1MiB CLI command-file cap remains. The C++-only `replaceBrushMaskDraft` explicitly retains its disabled-only contract; active workflows use the registry. No QML painting gesture or default active mask is introduced here.

For example, save this canonical command array as `brush.json`:

```json
[{"command":"mask.brush.replace","document":{"schema":1,"layers":[
  {"id":"local_light","enabled":true,"exposure":0.7,"strokes":[
    {"radius":0.08,"hardness":0.4,"opacity":0.75,"dabs":[[0.5,0.5,1]]}
  ]}
]}}]
```

Then run `JixelLightCli --commands brush.json --space display-p3 source.ARW new-output.tif`. Coordinates refer to the complete post-geometry/crop frame, and radius is a fraction of its short edge. Existing destination/source protection and actual-source as-shot resolution apply.

## Verification

Five new120-second shards exercise public routes, bringing CTest to39 suites:

- Canonical active/disabled layer round-trip, history Undo/Redo, masks-only transfer with unrelated controls unchanged, portable preset and XMP round-trip, invalid duplicate/typed/future state preservation, layer-ID/range/field rejection, batch rollback and isolated reset.
- Actual controller command batch makes one Undo entry, full histogram/plot completes on2,659,021 pixels, active project reopens, two virtual versions retain independent brush exposure/history, actual controller TIFF export matches independent legal strips/ICC, and an actual read-only CLI catalog export produces two exact PNG16 versions without changing database or source hashes.
- Four separately bounded actual external CLI exports consume fresh checksum-pinned public Sony07 ARW69,120,000 bytes, full7028×4688=32,947,264 nativeRGBA64 pixels without a source-image override. Actual `--commands` installs the active document, exposure and crop/straighten/rotation, producing5,085,762 output pixels, above the legacy2Mi research cap. PNG16/TIFF16 Display P3 pixels/ICC exactly match an independent legal-strip reference. WebP92/sRGB decoded pixels match an independently encoded reference. JPEG92/sRGB decoded pixels match a separate libjpeg encoder consuming independent rendered reference pixels, with ICC separately checked. These codec92 checks do not change the existing natural-photo JPEG1005/255 or GPU40/65535 thresholds.

The PNG CLI case also appends an invalid layer-ID command and requires whole-plan rejection before output publication. All four pin the original source hash `aa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d` and unchanged decoded-source fingerprint. They are actual public RAW exports, not generated photo substitutes or evidence for native FP32 RAW decoding/native GPU brush computation.

Initial focused checks passed Core212/GPU36 and the first three real CLI formats. The new JPEG oracle initially lacked an ICC tag, causing QImage equality to fail despite matching encoded pixels; explicitly assigning reference metadata without pixel conversion fixed it. The final focused persistence/catalog+JPEG checks passed2/2 in20.86s. The original failed result/XML/log is retained. Two new test compilation errors (ambiguous JSON initializer and mixed-type `auto` declaration) were corrected without modifying application behavior or relaxing checks; failed/corrected build logs remain in the physical build parent.

Final full working-tree **39/39 CTest passed400.13s**, actual Metal/public Sony/DNG, zero failures/skips:212 Core/36 GPU/39 performance/21 Sony Look/9 DB/9 missing-source/17 admission, all nine large CPU/four admitted GPU/four preview-export/four full-statistics-recovery shards plus the five new persistence/CLI shards. Source report `7d76c54` identifies the precommit base, not this candidate's exact committed acceptance. Reports: physical build `brush-persistence-results.txt`, `brush-cli-sony-*-results.txt`, `local-brush-persistence-{first,fixed,full-tests}.xml`; parent `brush-persistence-*-tests.log` and `brush-persistence-*-build*.log`; machine summaries `/tmp/jixellight-brush-persistence-local-summary.json` and physical build `brush-persistence-local-summary.json`. Own CI remains pending.

Existing QML GUI passed CPU/offscreen13,951ms and actual Metal/cocoa14,846ms, both23 screenshots,10,340,960 full scope pixels, all UI/selected-export checks and zero QML errors within the original60,000ms deadline. Native mask917ms/source recovery461ms passed all required conditions and recovery screenshots (`file_picker_automated:false`). Reports are in physical build `local-brush-persistence-gui/`, parent `brush-persistence-gui-validation.log`. These protect existing UI flows; they do not validate a new pointer-painting interface.

Next: actual pointer stroke creation with bounded resampling, stable layer selection, cancellation/source/version/crop/ROI guards, one completed gesture per Undo, native QML pointer validation, saved-project reopen and painted scopes/four-format export acceptance. Imported/stored effects working is not a completed painting UI or completion of F0–F6.
