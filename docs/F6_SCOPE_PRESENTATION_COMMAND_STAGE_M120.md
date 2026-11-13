# F6 scope presentation commands — M120

This private stage follows the separately frozen M119 Source/Working/Display candidate. Its own build/development/full/platform validation is pending. No source or runtime in M119's running frozen qualification is changed.

Three strict desktop presentation commands share the existing scopes adapters:

- `scopes.domain {domain}` accepts exactly `source`, `working` or `display`.
- `scopes.mode {mode}` accepts exactly `histogram`, `waveform`, `parade` or `vectorscope`.
- `scopes.full-resolution {enabled}` requires a JSON boolean.

Missing/unknown fields, wrong types, aliases and unsupported values are rejected without changing the decoded output or controller selection. Owner-thread/closing guards apply to both generic commands and legacy property setters. Valid no-ops are accepted. No Develop/history/catalog/source bytes, settings, image revision or export plan is written. Desktop schema discovery describes the strict fields and asynchronous completion. Offline command sessions/CLI cannot use these presentation commands; whole-plan validation rejects a late scope action before any earlier export is written.

Adapters and generic execution emit the same `scopes_command` trace containing the exact accepted command, frozen image revision/photo epoch and presentation-only completion semantics. Signal callbacks may delete the controller; only frozen local trace data is used afterward. Domain/mode/full-resolution implementation methods retain their existing behavior, with signal-lifetime checks. Disabling exact scopes also restarts a missing Display preview analysis when a rapid toggle cancelled its preceding producer; already populated default Display results keep their established behavior.

One new Core case (expected271) validates strict decoding and schema facts. One new CommandSession case (expected23) checks late refusal/output preservation. The existing scopes scheduling group gains one actual controller case (expected17), checking generic/adapter trace equality, no-op acceptance, malformed/foreign-thread/closing refusal and rapid toggles with complete unchanged histograms/pixels/history/revision. Its owner-deletion case now exercises all three presentation signals and frozen accepted traces. CLI verification adds schema and late-session refusal checks (expected92), retaining complete PNG pixels/ICC comparisons and all previous checks.

Normal native App smoke requires exactly seven accepted commands for the existing Source/Working/Display pointer/keyboard sequence: full=false, domain=source, mode=waveform, domain=working, mode=histogram, domain=display, full=true. A unique bounded trace marker prevents a missing/wrapped trace from producing a false pass. All existing exact bins/ranges, current waveforms, immutable preview/Develop/history/revision and original60s/native precision/resource limits remain. Native-FP32 RAW decoding, physical tablet calibration and A7R VI real-shot acceptance are not claimed. Main and protected PR15 remain unmerged.
