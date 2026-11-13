# F4 M142: keep queued recovery cleanup outside terminal owners

Test-only50db562 reproduces the hypothesized defect on unchanged production.
All six actual queued backend/activity/preview-geometry cases, both before
and after accepted close, clear the recovery object; direct foreign-thread
discard also clears it. The run is2 pass/7 fail/0 skip/0 blacklist in1.564s;
fresh target build41.16s. All20 original files/8,001,875 bytes, source copies
verified against Git blobs, binary and build graphs stay separate and failed.
The original log SHA256 is
`ed1645e1ee0f1fd916c9b51aea81b3fd78278dc3e4451de6cc6186dc37c0bc75`.
The saved-project/source assertions after the failed presence checks were
not reached in that baseline and are not reported as having passed.

The constructor's queued expiry callback now returns without checking or
discarding recovery once `m_closing` is set. Public recovery discard separately
refuses foreign-thread and accepted-close entry before reading or changing
recovery. Live-owner expiration and explicit live discard keep their existing
behavior. The immutable recovery object can remain until ordinary controller
destruction; accepted close no longer publishes cleanup changes after return.

All seven new controlled cases and the original44-case gesture/scheduling91
method bodies remain unchanged. Independent saved state/history, source bytes,
terminal status/revisions and zero post-close recovery notifications must now
execute and pass. No image math, RAW route, source-photo write, shader, codec,
resource limit, worker count, numerical tolerance or deadline changes.

CI441's original Mac failure remains failed, including its exact d60b0f6 log.
Its Linux success and pending Windows result stay independent. CI440's original
Mac no-runner cancellation and later same-head retry are observed separately.
Because the controlled defect exists in the inherited code, acceptance/root
promotion of those unpatched candidates is held; remote runs and raw-log
capture continue. The workspace remains at its prior accepted37c2e4f head.

A fresh physical second build, Python283, mandatory legacy82/native5/Sony
preflight, targeted15 including queued-recovery9, full106/all100/all33 and18
runtime guards are pending. The final source requires its own three-platform
CI, both actual installed88 packages and strict delivery. No older local or
platform result accepts this new repair, and full F0–F6 remains incomplete.
