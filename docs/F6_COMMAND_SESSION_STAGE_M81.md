# F6 M81 — ordered offline photo/edit/history/export commands

`JixelLightCli --session plan.json` compiles an ordered, transient photo session before decoding an image or creating an output. It uses the desktop's CommandRegistry and EditHistory for edits and undo/redo. The existing CLI source cache, source-aware Look resolution, CPU renderer, ICC encoders, whole-output preflight and no-overwrite publication execute the frozen export snapshots. It does not modify catalogs or source files.

```json
{
  "schema": 1,
  "engine": "jixellight-linear-v5-base2-look4",
  "commands": [
    {"command": "photo.import", "photo": "portrait", "source": "original.ARW"},
    {"command": "edit.batch", "commands": [
      {"command": "develop.set", "parameter": "exposure", "value": 0.5},
      {"command": "mask.luminance.range", "lower": 0.2, "upper": 1.2, "feather": 0.15},
      {"command": "mask.luminance.set", "parameter": "exposure", "value": 0.4},
      {"command": "mask.luminance.enable", "enabled": true}
    ]},
    {"command": "export.run", "destination": "edited.tiff", "space": "display-p3"},
    {"command": "history.undo"},
    {"command": "export.run", "destination": "original.png"}
  ]
}
```

Paths are relative to the manifest directory. Photo IDs are1–64 ASCII letters/digits/dot/underscore/hyphen, starting with a letter/digit. Import registers/selects a transient source, without copying or entering a catalog; photo.select chooses an existing ID. Each photo has its own history. Every ordinary shared edit command forms a history step if it changes state; edit.batch validates atomically and forms one step. Undo/redo at a history boundary rejects the whole plan. Export.run snapshots the current state at that point; subsequent edits/selection/history cannot change it. Its optional space defaults to srgb. Format follows the existing JPEG/PNG16/TIFF16/WebP8 destination extension.

The manifest has exactly schema/engine/commands, maximum4MiB and1–10000 aggregate commands, including nested edits; at most1000 photos/exports and at least one export are required. A separate64MiB ledger charges canonical state JSON for each import/edit/history result/export, conservatively retaining charges after replacement. It bounds retained state payload, not allocator overhead/RSS or the existing renderer's image budget. Sources/metadata may be read while validating; no image decode/output occurs until the complete command plan and every export path validate. Unknown fields, changed engine, invalid nested commands/IDs/paths, missing sources, unsafe/occupied output paths and incompatible mode flags reject the whole plan. Runtime decode/export failure stops, retaining completed exports, matching existing batch behavior.

`--schema` exposes session commands, limits, validation and history semantics alongside the shared edit schema. The final JSON report includes source commit/engine, per-export photo ID and adjustments, command/photo counts, charged state bytes and SHA-256 of the exact input manifest snapshot. JSON reads themselves are bounded to limit+1 bytes, including a file that grows after its size check. Session mode cannot combine with positional paths, --batch, --catalog, --commands or global export settings. This is offline command replay; it does not yet route every desktop file/menu action or implement the optional MCP transport, and is not full F6/F0–F6 completion.

Development validation passes:8 Qt results with zero failures/skips in628ms, all245 Python methods and all80 extended CLI checks. Actual public Sony07 RAW metadata materialization, fade2.6→3, whole-batch undo and independently decoded PNG16/ICC compare exactly against the existing CLI; original SHA staysaa12ae9ef42a8254afc11daddb706dee7d2f0cd7b869f44f377c4470fd7fdc4d. This private2.5% crop session takes1506.235ms (two independent single-export oracles1449.337/1430.332ms) on the M2; it is not full-image throughput. Development binaries identify parent580ac9a; frozen exact-head/full100/platform/package acceptance is still required. New Qt cases cover ordered immutable exports/per-photo history, batch undo/common mask semantics, unchanged caller output on late failure, strict shapes and invalid history, source-specific Sony as-shot materialization and aggregate/retained-payload bounds. Installed CLI validation compares independently decoded PNG16/ICC bytes with existing CLI oracles, checks relative paths and undo/redo, mode conflicts, late invalid commands, original/manifest hashes and whole-plan occupied-destination refusal. Full CTest becomes100 with95 Qt result groups; Core220/GPU82 and88 package contexts remain. No image algorithm, engine, deadline, tolerance, source decoder or GUI changes.
