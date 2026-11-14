# M166: observe a stable initial window before the independent Qt control

Accepted root b91de16 remains frozen. M165/7b5904c is held with its original
transparent-GPU seed mismatch. The post-request synchronization fix passes
the deterministic in-flight regression at Cocoa DPR1/2 (5/0 each). Opaque CPU,
opaque GPU and transparent CPU pressure pass7380/6983/7053ms. The fourth,
transparent GPU, fails atphase50/1201ms, with a valid post-request sync marker.

Both actual failed frames and all canonical pixel hashes/PNG CRCs verify.
Exactly386 pixels differ, all within[2209,202,2247,240], the photo canvas's
loading indicator. Maximum RGBA8 difference is[2,2,2,0]; every other pixel is
identical. PhotoCanvas.qml's indicator runs while loading/rendering changes.
No new pixel tolerance or excluded image region is introduced.

The held manifest is
17062906ad95838a0a3a05659a6e3350d127303ea0d4ac068bd5605a7c238878;
it retains131 files/109,757,550 bytes plus complete source,19 runtime copies,
five graphs and all1400 original build-file hashes. The independent pixel
analysis SHA256 is
da88400dae523fcffcd08439f566ba3a03097f68eb7fa4d1e656db5f849ad652.
Every prior failure and source/build remains in place and unaccepted.

## Initial readiness, with the exact control preserved

Only the first seed waits for three consecutive actual normal-window frames
whose complete canonical pixel hashes match. Every observation goes through
the post-request scene synchronization and normal swap/end path. At most64
frames are observed, each using only the remaining original45000ms budget.
No sleep, poll interval, backend, animation setting or product deadline changes.
The helper retains earlier hashes/times, never earlier full-frame images.
Its raw/owned image bound remains64MiB with one readback at a time.

The first independent synchronous Qt control still must match every pixel and
DPR exactly. It is performed once, after readiness; it is not retried until
it matches. The original38 logical per-packet reads,22 quality85 PNGs and all
original phase actions, input/history/locale/radius/reference/export/ICC checks
remain. Actual readback counts include all additional initial observations:
normal reads = logical reads + seed observations -1. Every observation's hash,
time and post-request synchronization marker is reported and validated. Linux
CPU retains the original offscreen path, with zero readiness observations.

The capture fixture adds a case that changes a real item's GUI state between
frames and requires observing an earlier different frame, three matching final
frames, the final expected color and a complete exact Qt control. The previous
three cases remain; expected capture Qt counts become6/0. One portable evidence
case rejects incomplete/unstable traces; Python becomes302. Every original
CTest selection remains:107/all101 with19 pinned runtime files.

Use this new NAS source/head/physical Release build; no held build is reused.
Normal product UI, RAW/image/shader algorithms and dependencies remain unchanged.
Require capture DPR1/2 and all four pressure variants first, then the full
development, original107/all101/all33/runtime/graph/retention/private, own three
original CI and shipping checks. No Windows speedup, full-App ASAN, photographic
calibration, physical-input or full F0–F6 completion is claimed here.
