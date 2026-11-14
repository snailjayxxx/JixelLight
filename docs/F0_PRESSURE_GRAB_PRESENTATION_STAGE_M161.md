# M161: present between controlled pressure screenshots

The accepted root26ebacc remains unchanged. Held native-source candidate
a30c5ee passes local106/all100/all33/eighteen runtimes/five graphs/private3,
Linux and macOS CI453 and actual downloaded macOS CPU/Metal/diagnostic/native
source checks. Original Windows CI453 passes full106/2094.18s and preflight/
180.45s, but its installed transparent-brush-pressure-cpu reaches46152ms at
phase16 against the unchanged45000ms deadline. The other87 package checks pass.

## Original failure retained

The original13,446,329-byte log has SHA256
0c8a2d46b56ee84fbf28c7603d352c80b54f31d8e65278a5376d6b13a8cfbfd1.
The177,836,576-byte original package-validation archive has SHA256
4f023aa1ce627c1ea9dedc4e3c73513d28825a323cc4d013576aa32ca798f97d.
Its1482 members pass CRC/path checks. The original manifest agrees with the
separately derived failed-log analysis:88 checks, one failed. Source and1077
files/1,418,450,606 bytes plus complete source tar remain frozen; held manifest:
233a48bd98df91167487f9afb7500ebc4a367c66a4e235802b2aad5ed3ce61e4.
No original failure is relabelled or replaced by a retry.

The failed flow makes38 synchronous window grabs/15,767.49ms. One PNG save is
2304.37ms; aggregate saves2898.94ms. The prior accepted Windows run passes at
44364ms with38 grabs/15,947.49ms and726.05ms aggregate saves. This establishes
limited timing margin, not a unique cause of the original delay.

## Bounded scheduling repair

Only the controlled pressure smoke's grab helper queues QQuickWindow::update()
after obtaining the same synchronous QImage. A normal presented frame can run
between event turns even when the scene remains unchanged. Every original
grab, per-packet pixel check, capture/PNG quality, bounded worker, independent
reference/export, intent/history/locale guard and45000ms acceptance stays.
No sleep/poll interval, backend, test selection, package88, full106 or deadline
changes. No normal product UI, image kernel, shader, native-source diagnostic,
uniform, admission, cache or dependency change is introduced.

Qt6.8.3 [WM_Grab](https://github.com/qt/qtdeclarative/blob/v6.8.3/src/quick/scenegraph/qsgthreadedrenderloop.cpp)
ends with SkipPresent; its [D3D11 beginFrame](https://github.com/qt/qtbase/blob/v6.8.3/src/gui/rhi/qrhid3d11.cpp)
waits up to1000ms on frame latency. [QQuickWindow::update](https://doc.qt.io/qt-6.8/qquickwindow.html#update)
schedules a repaint even when scene graph contents did not change. These support
the scheduling hypothesis; Windows speedup is unmeasured before own CI.
No Qt implementation code is copied and no backend wait/security setting changes.

## Acceptance pending

Use one fresh NAS source/head/physical Release build. Required checks retain
Python294, legacy85/native19, original numeric584GPU/132CPU/56policies under
RGB40/exact alpha, controlled pressure variants, targeted15/default reference,
actual RAW App/pan/proof/relink/Move, full106/all100/all33/all18/five graphs,
retention43/private3 and all three own CI jobs. Both desktop shipping packages
and actual downloaded macOS flows remain required. Native-source a30 is held;
its ancestor26 is the independently accepted parent. Full F0-F6, physical
input, float RAW/calibration and full-App/new diagnostic ASAN remain open.
