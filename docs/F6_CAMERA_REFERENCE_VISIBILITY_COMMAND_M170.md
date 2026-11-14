# F6 camera-reference visibility command

This source-only successor starts at candidate `6845833`, whose independent
local/platform/package qualification is still running. The accepted workspace
remains `b91de16`; this change inherits no acceptance from its pending base.

`camera-reference.show` accepts exactly one boolean `enabled` field through the
desktop command registry. The existing `setShowCameraReference(bool)` UI adapter
uses the same implementation. A true return records an accepted visibility
request: loading a missing reference is asynchronous and can remain unavailable.
Hiding retains an already loaded reference. Showing it again reuses those pixels;
a repeated request leaves the existing load and notification behavior intact.

The command changes presentation state. It does not change Develop intent,
history, source bytes, preview revision, scope counts, catalog or preferences.
It does not choose a JPEG, fit a Look or authenticate a RAW/JPEG pair. Existing
manual/metadata-matched/embedded-preview provenance remains visible unchanged.
Offline edit/export sessions reject the whole plan before any output, including
when this desktop command follows an otherwise valid export.

Malformed/extra fields, unknown names, foreign threads and accepted close refuse
before state or trace changes. Accepted commands freeze their revision and photo
epoch and enter Action Trace before notification callbacks. Existing guarded
notification behavior prevents a deleted/closed owner or a superseded reentrant
request from starting old reference work. Reentrant hide therefore records the
outer show before the nested hide and launches no superseded load.

New checks cover strict decoding and atomic output, offline-plan refusal,
actual bounded JPEG loading/reuse with unchanged image/history/source/scopes,
owner destruction/close, reentrant request ordering and foreign-thread refusal.
The ordinary RAW App additionally exercises the legacy hide adapter followed by
canonical show, retains the existing actual-reference completion condition and
records exact command and presentation-invariant evidence. The deployed-package
validator independently checks every flag and exact boolean command order; three
Python tests reject forged/missing evidence, numeric booleans and extra fields.

Expected counts before compilation: Python **305**, Core **278**, command-session
one additional result, brush-scope scheduling **94**; CTest **107**, Qt result
groups **101**, ordinary GPU **85**, native GPU **19**. No shader, RAW, processing
math, precision/resource limit or deadline changes. Fresh compilation, complete
local/native/RAW checks and own platform/package acceptance remain required.
Overall F0–F6 development remains incomplete.
