# M163: capture each controlled pressure check in a normal window frame

Accepted parent b91de16 passes original CI454 on all three platforms, local106,
both desktop archives and actual downloaded macOS flows. Windows owned CPU
pressure still takes44160/44219ms against the original45000ms deadline.
Cancelled held-owner guard phases alone take about6.2s each; they remain fully
checked in this change. No unique cause or measured Windows speedup is claimed.

## Independent helper proof

The isolated M162 source247830e builds the QA-only normal-window capture helper
and two added methods in the existing native-monitor target. Actual Cocoa
DPR1 and DPR2 each pass7/0, including all three original monitor cases. Two
distinct asymmetric/alpha-blended window revisions match the existing Qt grab
over every pixel. Budget, reentry, pending timeout/immediate window destruction
and healthy subsequent capture pass. The proof receipt SHA256 is
46e9ab0a0ba3eb78dd0ff015007cf263701e6877dc96f4cfcbbce792ea161c71.
That is a local helper proof, not full product or cross-platform acceptance.
The helper and complete Qt test source are unchanged in this integration.

The initial external proof-summary regex incorrectly required OK to be the
last output line. Original Python tests exit0 with294 passed followed by two
existing JSON prints. Its original script/log/failure receipt remain. A second
helper accepts that same original log without rerunning tests and performs the
first C++ build/window runs. No original source/App failure is relabelled.

## Pressure integration

Every original phase action, packet, per-packet pixel read, locale/radius/
history/intent guard, independent reference, export and pixel/ICC comparison is
byte-identical to b91de16. Only the grab helper obtains each actual window
backbuffer during a requested normal frame and waits for its normal swap/end.
The original38 logical reads and22 quality85 PNGs remain mandatory. Each run
adds one full-window independent Qt grab of its initial capture; all pixels
and DPR must match, with both canonical pixel hashes recorded. Intent, history,
revision and drawing ownership must also remain unchanged across each wait.

The original50ms timer returns while a capture is waiting, preventing another
controlled packet from reentering that phase. Each wait receives only the
remaining original45000ms budget; final success still requires elapsed<45000.
One pending capture is admitted. Its raw backbuffer and owned image share the
existing64MiB helper bound. The original bounded PNG writer and all save/hash
verification remain. Failed runs retain the original failure-only synchronous
Qt screenshot path; that path is counted separately and must be unused on
success. It cannot replace a failed normal frame or convert failure to success.

Python evidence validation rejects stale/missing frames, alternate capture
sources, failed control comparison, changed PNG quality/count, extra requests
or inconsistent storage. Six added portable cases run through the already
selected TestBrushPressureFailureEvidence.py on all platforms. Full Python
coverage becomes300; CMake106 selections and100 Qt groups remain unchanged.
LegacyGPU85/native19 and all original numeric/alpha/histogram policies remain.

No normal UI/renderer, image math, shader, upload, RAW/float/calibration,
dependency, backend, test selection or deadline is changed. Use a new NAS
source/head/physical Release build; M162 source/build remains frozen. Require
early four pressure variants, original development/targeted/RAW UI checks,
full106/all100/all33/all18/five graphs, retention/private3, all three own
original CI jobs, both desktop archives and actual downloaded macOS checks
before acceptance. Full F0–F6 and full-App ASAN remain incomplete.
