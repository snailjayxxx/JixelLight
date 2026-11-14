# M162: actual normal-window-frame capture proof

Accepted parent b91de16 has local106, all three original CI454 jobs and both
shipping packages accepted. Windows controlled CPU pressure remains close to
the unchanged45s deadline:44160/44219ms. Original cancellation guard phases
20/21 take6217/6197ms opaque and6242/6223ms transparent, against308–312ms on
the GPU route. These observations establish a timing risk, not a unique cause.

This stage adds a QA-only normal-window-backbuffer capture helper and two
actual-window checks to the existing native-monitor Qt target. It does not
yet replace the accepted pressure smoke's existing synchronous Qt grabs.
Every normal product renderer, RAW/image/shader algorithm, original test,
deadline, dependency and CMake target remains unchanged.

The helper schedules one AfterRendering job on the actual QQuickWindow,
reads its existing transfer-source swapchain with QRhi, and waits for that
normal frame's frameSwapped/afterFrameEnd signals. The readback and owned image
share a capped64MiB aggregate budget; at most one request is admitted. A nested
GUI event loop excludes user input, refuses reentry and retains only owned
state across timeout/window shutdown. A QA-only QRhi::finish completes the
readback in its render job; there is no pending stack readback after that job.
No cached picture, offscreen item render, desktop capture, backend switch,
new swapchain, SkipPresent frame, or extended product deadline is used.

Tests require two distinct real window revisions, exact full-image agreement
with the existing Qt synchronous window grab, independent asymmetric color
and alpha-blend probes, actual DPR/extent, and byte bounds. They also exercise
hidden/expired/overbudget/reentrant rejection, timeout while a bounded render
job is pending, immediate window destruction and healthy subsequent capture.
These are targeted QA-helper checks; they do not establish full-App ASAN,
physical pen/monitor behavior or Windows performance before own CI evidence.

Primary Qt6.8.3 sources supporting the mechanism:
- [render loop](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgthreadedrenderloop.cpp): normal presentation, transfer-source swapchains and synchronous grabs ending with SkipPresent.
- [RHI support](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgrhisupport.cpp): actual window RHI/swapchain resources and synchronous window-readback formats/orientation.
- [RHI API](https://github.com/qt/qtbase/blob/v6.8.3/src/gui/rhi/qrhi.cpp): null-texture backbuffer readback, finish completion and premultiplied byte formats.

One fresh NAS proof source/head/build is required before pressure integration.
No performance improvement or final acceptance is claimed by this document.
