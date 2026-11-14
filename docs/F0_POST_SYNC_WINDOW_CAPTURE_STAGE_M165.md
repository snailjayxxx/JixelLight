# M165: bind a window readback to synchronization after its request

Accepted root b91de16 remains frozen. M163/e460454 and M164/8c5e64e remain held.
Their original first GPU pressure failure is preserved. M164 records both
actual3080×1840/DPR2 seed images; all PNG CRCs and original canonical pixel
hashes independently verify. There are17497 differing pixels and no alpha
differences. The normal frame visibly has the older radius8%/opacity80% and
updating-statistics UI; the immediate Qt control has radius20%/opacity100% and
GPU statistics. The requested phase already set the latter values. History0
and revision6 remain unchanged. These are actual older/newer UI states, not
evidence of a RAW or pixel-processing regression.

The original e460454 held manifest is
a946fcff05678d0d9f8433cdd6ade0d5d9942feb63a7635c41dd4e821cabc97d.
The diagnostic outcome is
2c40ca5f1379fddf315ad01257f4ff4485b2cf87cd2cd4132fff5c4d13cd03fc;
its byte analysis is
8a6b42f8a4d81471aebd94c2c7cee28c6b1b69f7d27fb842c0c67402690f669a.
The independently verified diagnostic hold manifest is
24833e11740dcb61d9d5d755c55ded3d8fd2a2f8bd82d339e61d4dd8a635538f.
Both source archives, original logs/frames, runtime/build inventories and
objects remain in place. The diagnostic collector's exit0 only means evidence
was collected; the original App exit1 and failed exact comparison remain.

## Bounded ordering fix

An AfterRendering job registered directly from the GUI can join an in-flight
frame whose scene was synchronized before the request. The QA helper now
posts an AfterSynchronizing job and only registers its readback job from that
job. Successful results require this post-request marker and normal frame
swap/end, with one pending request and the same64MiB raw/owned image budget.
Qt releases the job-list mutex before running jobs, allowing this registration
to target the later rendering stage of that synchronized frame.

The actual-frame readback, format/orientation handling and QA-only finish are
unchanged. The original38 logical reads and22 quality85 PNGs remain; there is
no stability retry, cached frame, extra backend, sleep or relaxed deadline.
The first independent Qt full-window control must still match exactly. Every
original phase action/packet/pixel/history/locale/reference/export branch is
byte-identical to b91de16. Each normal capture also records its post-request
sync observation. Linux CPU retains the original offscreen protocol.

## Deterministic regression and acceptance

The standalone capture target gains a third case. It holds an older frame
after scene synchronization, changes the item from the GUI, and requests a
capture while that older render is still in flight. A high-priority owned
event releases the render only after capture jobs are registered. The captured
new color and entire image must match the Qt control, and the post-request
marker must be present. The fixture explicitly uses the threaded loop; all
original test environments and normal product settings remain unchanged.
The original two capture cases remain. Expected capture Qt counts become5/0;
Python301, CTest107/all101, legacy85/native19 and19 runtime pins remain.

Diagnostic pairs are saved only when the exact seed comparison fails; their
original failure logic is retained. No normal UI, image/RAW/shader algorithm,
dependency, test selection or product deadline changes. Use this fresh NAS
source/head/physical Release build. Require both capture DPR contexts, four
pressure variants, original development/RAW checks, full107/all101/all33,
runtime/graph/retention/private checks and all own original CI/shipping gates.
Windows performance, physical input and full F0–F6 remain unqualified here.

Primary Qt6.8.3 sources:
- [QQuickWindow](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/items/qquickwindow.cpp): synchronization/render stages and unlocked job execution.
- [threaded render loop](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgthreadedrenderloop.cpp): synchronization before rendering, normal swap and synchronous grab behavior.
