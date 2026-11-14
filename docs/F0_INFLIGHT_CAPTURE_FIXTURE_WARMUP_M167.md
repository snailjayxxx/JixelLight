# M167: complete exposure before arming the in-flight regression

Accepted root b91de16 remains unchanged. M166/34411ef is held: its local
107 CTest selections passed in1127.00s, but original CI455 macOS passed only
106/107. The new capture fixture reports5passed/1failed because its render
gate did not receive the queued GUI release within the original1000ms.
The original log SHA256 is
d840cba3e8fa46b81b71b295a1792ceb9c2cc5c27f37855d724c15c83c7a962b.
The complete failed61,409,741-byte validation artifact is retained on NAS,
with verified provider SHA256 and every ZIP member CRC.

`isExposed()` can become true before the first exposed frame ends. Qt6.8.3's
threaded loop deliberately keeps the GUI blocked through that frame's
submission, whereas an ordinary update wakes the GUI after synchronization.
The regression previously allowed its gate to join that first exposure.
M167 first completes one real normal-window capture, verifies swap/end and the
old expected pixel color, then arms the existing concurrent gate. Its1000ms
gate,5000ms capture,6000ms outer wait, full-image control and new-revision
pixel assertion remain unchanged. Record the successful warmup explicitly.
This makes the required ordinary-update premise observable; it does not prove
which Qt expose branch the original hosted runner took.

Primary implementation reference:
https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgthreadedrenderloop.cpp
(`QSGRenderThread::sync` and `syncAndRender`, especially the expose wakeup).

CI455 Linux passes all107 in1195.73s but its subsequent controlled GPU pressure
flow fails at3057ms/phase1 with `CAPTURE_WAIT_CHANGED_INTENT_HISTORY_OR_OWNER`
during the second preview-language capture. Pending peak remains1; the seed
had5actual frames, last3identical and one exact independent Qt control. This
failure is separately retained and remains unresolved. Add failure-only
before/after Develop/history/drawing/revision/source and canvas-size evidence,
preserving the exact rejection. The timer now also checks the existing outer
capture-pending count, covering the independent synchronous Qt control.

The local followup stopped before cross-RAW launch when actual temporary free
space fell below the existing3GiB guard. Keep that external stop and all
completed outputs; no canonical33/private/package qualification is claimed.
Do not start those deferred M166 scripts. Use a fresh NAS source/head/build
for M167. First verify the fixture at DPR1/2 and a deliberate unsynchronized
negative control; collect Linux state evidence before choosing a repair.
Normal UI, RAW/math/shaders, all original phase bodies,107 selections,101 Qt
groups,19 runtime pins and all product deadlines remain unchanged. The full
F0–F6 plan is incomplete.
