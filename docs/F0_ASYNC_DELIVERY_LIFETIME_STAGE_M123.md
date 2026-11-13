# F0 asynchronous delivery lifetime — M123

This isolated stage follows M122. Compilation, sanitizer reproduction, development checks, frozen full102/all33 and platform qualification are pending. Running parent source, runtimes and build graphs remain frozen.

Both latest-only and live-frame queues previously invoked their member delivery function and then read queue members. A callback can synchronously destroy the controller that owns that queue: the function object itself and the queue may therefore disappear before the callback returns. Each queue now retains shared ownership of its single delivery callable and a guarded QObject context, returning immediately after a callback destroys that context. The same callable is retained between deliveries, preserving mutable callback state. Existing replacement, cancellation, worker ownership and result retention semantics are preserved.

Four performance regression rows cover callback destruction/captured-state lifetime and reentrant submit/replace/cancel for both queue policies. The live-frame destruction row also holds a pending frame that must never run. Four production-controller rows destroy an actual imported-photo controller from completed Source/Working histogram or plot signals, retaining the original photo hash. Expected performance43, scheduling22, Core276, session23, CLI92 and Python283; CTest remains102. A separate minimal AddressSanitizer probe will compare unchanged parent headers with this candidate before any repair is declared validated.

No render math, RAW decoding, color/ICC, cache/admission capacity, thread budget, GPU precision or deadline changes. Native-FP32 RAW, physical devices and A7R VI real-shot acceptance remain incomplete; main and protected PR15 stay unmerged.
